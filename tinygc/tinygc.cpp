#include <chrono>
#include "tinygc.h"

namespace TinyGC
{
    inline intptr_t GCMasterAsInt(GarbageCollector *master) noexcept {
        return reinterpret_cast<intptr_t>(master);
    }
    inline GarbageCollector* IntAsGCMaster(intptr_t master) noexcept {
        return reinterpret_cast<GarbageCollector *>(master);
    }

    inline intptr_t getMark(GarbageCollector* master) noexcept {
        return GCMasterAsInt(master) & static_cast<intptr_t>(1);
    }

    inline GarbageCollector* setMark(GarbageCollector* master) {
        return IntAsGCMaster(GCMasterAsInt(master) | static_cast<intptr_t>(1));
    }

    inline GarbageCollector* clearMark(GarbageCollector* master) {
        return IntAsGCMaster(GCMasterAsInt(master) & ~static_cast<intptr_t>(1));
    }

    // using manual stack avoids overflow when marking long linked lists
    void GCMarker::clearStack() {
        while(this->size > 0) {
            auto sub = this->objects[--(this->size)];
            if(sub != nullptr) {
                sub->GCMaster = setMark(sub->GCMaster);
                sub->GCMarkAllChildren(*this);
            }
        }
    }

    // When GC is triggered, free heap memory may be not enough
    // use recursive function, don't malloc stacks
    // actually do not mark objects but only push it onto the stack
    void GCMarker::markOneObject(GCObject* object) {
        if ((object != nullptr) && (getMark(object->GCMaster) == 0)) {

            if(this->size < MaxSize)  {
                this->objects[(this->size)++] = object;

            } else {
                GCMarker another;
                another.objects[(another.size)++] = object;
                another.clearStack();   // recursive call, very rare case
            }
        }
    }

    // All pointers in marker.objects points to the objects that should be marked but not yet marked
    void GarbageCollector::mark() {
        GCMarker marker;
        auto end = &listHead;
        for(auto i = listHead.next; i != end; i = i->next) {
            auto root_obj = i->ptr;
            if(root_obj != nullptr) {
                root_obj->GCMaster = setMark(root_obj->GCMaster);
                root_obj->GCMarkAllChildren(marker);
                marker.clearStack();
            }
        }
    }

    void GarbageCollector::sweep() {
        auto objectListHead = listHead.ptr;
        auto prev = objectListHead;
        if(prev != nullptr) {
            for(auto curr = prev->GCNextObject; curr != nullptr; ) {
                auto next = curr->GCNextObject;
                if(getMark(curr->GCMaster) != 0) {
                    curr->GCMaster = clearMark(curr->GCMaster);
                    prev = curr;
                } else { // collect
                    prev->GCNextObject = next;
                    destroyObject(curr);
                    --objectNum;
                }
                curr = next;
            }

            if(getMark(objectListHead->GCMaster) != 0) {
                objectListHead->GCMaster = clearMark(objectListHead->GCMaster);
            } else {
                auto temp = objectListHead->GCNextObject;
                destroyObject(objectListHead);
                --objectNum;
                listHead.ptr = temp;
            }
        }
    }

    GarbageCollector::~GarbageCollector() {
        auto objectListHead = listHead.ptr;
        for(auto p = objectListHead; p != nullptr;) {
            auto next = p->GCNextObject;
            destroyObject(p);
            p = next;
        }
    }

    typedef std::chrono::steady_clock Clock;
    typedef std::chrono::time_point<Clock> TimePoint;
    typedef decltype(std::declval<TimePoint>() - std::declval<TimePoint>()) Duration;

    void GarbageCollector::collect() {
        auto totalNum = objectNum;
        auto start = Clock::now();

        mark();
        sweep();

        auto end = Clock::now();
        auto notCollected = objectNum;

        lastGC.elapsedTime = (end - start).count();
        lastGC.endTime = end.time_since_epoch().count();
        lastGC.collected =  totalNum - notCollected; 
        lastGC.notCollected =  notCollected;
        lastGC.hasValue =  true;
    }

    bool GarbageCollector::shouldCollect() const {
        return !lastGC.hasValue || (lastGC.elapsedTime * lastGC.notCollected 
                < (Clock::now().time_since_epoch().count() - lastGC.endTime) 
                * lastGC.collected);
    }

    bool GarbageCollector::checkPoint(){
        if (shouldCollect()) {
            collect();
            return true;
        }
        return false;
    }
}

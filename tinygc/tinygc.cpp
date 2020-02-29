#include <chrono>
#include "tinygc.h"

namespace TinyGC
{
    GarbageCollector::~GarbageCollector() {
        for(auto p = objectListHead; p != nullptr;) {
            auto next = p->GCNextObject;
            destroyObject(p);
            p = next;
        }
    }

    void GarbageCollector::mark() {
        auto end = &rootListHead;
        for(auto i = rootListHead.next; i != end; i = i->next) {
            auto root_obj = i->ptr;
            if(root_obj != nullptr) {
                root_obj->GCMark();
            }
        }
    }

    void GarbageCollector::sweep() {
        auto prev = objectListHead;
        if(prev != nullptr) {
            for(auto curr = prev->GCNextObject; curr != nullptr; ) {
                auto next = curr->GCNextObject;
                if(curr->GCGetMark() != 0) {
                    curr->GCClearMark();
                    prev = curr;
                } else { // collect
                    prev->GCNextObject = next;
                    destroyObject(curr);
                    --objectNum;
                }
                curr = next;
            }

            if(objectListHead->GCGetMark() != 0) {
                objectListHead->GCClearMark();
            } else {
                auto temp = objectListHead->GCNextObject;
                destroyObject(objectListHead);
                objectListHead = temp;
            }
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

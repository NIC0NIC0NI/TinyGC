#include "tinygc.h"

namespace TinyGC
{
	void GC::mark() {
		for(auto prev = observerList.before_begin(), 
					cur = observerList.begin(),
					end = observerList.end(); cur != end; ) {

			auto root_pp = cur->get();
			if(root_pp != nullptr) {  // if false, observer invalidated
				auto root = root_pp->ptr;
				if(root != nullptr) {  // if false, GCRootPtr is null
					root->GCMark();
				}
				++prev, ++cur;
			} else { // remove invalidated observer
				cur = observerList.erase_after(prev);
			}
		}
	}

	void GC::sweep() {
		GCObject* notCollectedHead = nullptr; // Not collected Objects
		GCObject* next;
		for(GCObject* p = objectListHead; p != nullptr; p = next){
			next = p->_next;
			if (p->_mark) {
				p->_mark = false;
				p->_next = notCollectedHead;
				notCollectedHead = p;
			} else {
				destroyDeallocate(p);
				--objectNum;
			}
		}
		objectListHead = notCollectedHead;
	}

	void GC::collect() {
		auto totalNum = objectNum;
		auto start = GCStatistics::Clock::now();

		mark();
		sweep();

		auto end = GCStatistics::Clock::now();
		auto notCollected = objectNum;

		lastGC.elapsedTime = end - start;
		lastGC.endTime = end;
		lastGC.collected =  totalNum - notCollected; 
		lastGC.notCollected =  notCollected;
		lastGC.hasValue =  true;
	}

	bool GC::shouldCollect() const {
		return !lastGC.hasValue || (lastGC.elapsedTime.count() * lastGC.notCollected 
				< (GCStatistics::Clock::now() - lastGC.endTime).count() 
				* lastGC.collected);
	}

	bool GC::mayCollect(){
		if (shouldCollect()) {
			collect();
			return true;
		}
		return false;
	}
}

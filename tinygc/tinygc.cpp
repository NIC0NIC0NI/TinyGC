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
			next = p->_Next;
			if (p->_Mark) {
				p->_Mark = false;
				p->_Next = notCollectedHead;
				notCollectedHead = p;
			} else {
				delete p;
			}
		}
		objectListHead = notCollectedHead;
	}
}

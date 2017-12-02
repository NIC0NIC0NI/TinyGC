#ifndef _TINYGC_H_
#define _TINYGC_H_
#include <type_traits>
#include <iostream>
#include <forward_list>

namespace TinyGC
{
	//===================================
	// * Forward declarations
	//===================================
	class GCObject;
	template <typename T>
	class GCValue;
	class GC;
	template <typename _GCTy>
	class GCRootPtr;

	namespace details {
		class GCRootPtrBase;
		class GCRootObserver;
	}


	//===================================
	// * Type checking macros
	//===================================
#define CHECK_POINTER_CONVERTIBLE(From, To) \
	static_assert(std::is_convertible<From*, To*>::value, \
				"Invalid pointer conversion from "#From"* to "#To"*")

#define CHECK_GCOBJECT_TYPE(Type) \
	static_assert(std::is_base_of<GCObject, Type>::value, \
				#Type" is not a subclass of GCObject")

	//===================================
	// * Class GCObject
	//===================================
	class GCObject
	{
	public:
		GCObject() : _Mark(false) {}
		virtual ~GCObject() {}

		
	protected:
		static void GCMarkSub(GCObject* sub) noexcept {
			if(sub != nullptr){
				sub->GCMark();
			}
		}
		virtual void GCMarkAllSub() {}
	private:
		void GCMark() {
			if (!this->_Mark) {
				this->_Mark = true;
				GCMarkAllSub();
			}
		}

		bool _Mark;
		GCObject *_Next;

		friend class GC;
	};

	//===================================
	// * Class GCValue
	//===================================
	template <typename T>
	class GCValue : public GCObject
	{
	public:
		//GCValue() = default;  // can't compile in MSVC, but OK in GCC
		~GCValue() = default;

		template <typename... Args>
		explicit GCValue(Args &&... args)
			: data(std::forward<Args>(args)...) {}

		GCValue& operator=(const T &o) {
			this->data = o;
			return *this;
		}
		operator T&()  noexcept { return this->get(); }
		operator const T&() const noexcept { return this->get(); }
		T& get() noexcept { return data; }
		const T& get() const noexcept { return data; }

	private:
		T data;
	};

	

	//===================================
	// * Class GC
	//===================================
	class GC
	{
	public:
		GC() : objectListHead(nullptr) {}
		~GC() {
			sweep();
		}

		void collect() {
			mark();
			sweep();
		}

		template <typename T, typename... Args>
		GCRootPtr<T> newObject(Args &&... args) {
			CHECK_GCOBJECT_TYPE(T);
			T *p = new T(std::forward<Args>(args)...);
			addObject(p);
			return GCRootPtr<T>(p, this);
		}

		template <typename T, typename... Args>
		GCRootPtr<GCValue<T>> newValue(Args &&... args) {
			return newObject<GCValue<T>>(std::forward<Args>(args)...);
		}

		details::GCRootObserver& addRoot(const details::GCRootPtrBase* p) {
			observerList.emplace_front(p, this);
			return observerList.front();
		}
	private:
		void addObject(GCObject *p) {
			p->_Next = objectListHead;
			objectListHead = p;
		}
		
		GCObject* objectListHead;
		std::forward_list<details::GCRootObserver> observerList;

		void mark();
		void sweep();
	};

	namespace details {
		//===================================
		// * Class GCRootObserver
		// * Observes a GCRootPtr
		//===================================
		class GCRootObserver {
			const GCRootPtrBase* observed;
			GC *master;
		public:
			GCRootObserver(const GCRootPtrBase* p, GC *master) noexcept :
				observed(p), master(master){
			}

			void reset(const GCRootPtrBase* newRoot = nullptr) noexcept {
				observed = newRoot;
			}

			const GCRootPtrBase* get() const noexcept {
				return observed;
			}

			GCRootObserver& createNew(const GCRootPtrBase* newRoot){
				return master->addRoot(newRoot);
			}
		};

		//===================================
		// * Class GCRootPtrBase
		// * Not a template class
		// * Does not guarantee type safety
		//===================================
		class GCRootPtrBase {
		protected:
			friend class ::TinyGC::GC;

			GCObject* ptr;
			GCRootObserver* observer;

			GCRootPtrBase() = delete;
			
			GCRootPtrBase(GCObject *ptr, GC *master): 
				ptr(ptr), observer(&master->addRoot(this)) {}
			
			GCRootPtrBase(const GCRootPtrBase & root): 
				ptr(root.ptr), observer(&root.observer->createNew(this)){}
				
			// move construction, does not have to create a new observer again
			GCRootPtrBase(GCRootPtrBase && root) noexcept: 
				ptr(root.ptr), observer(root.observer) {
				root.observer = nullptr;
				observer->reset(this);
			}

			~GCRootPtrBase() {
				if(observer != nullptr) {
					observer->reset();
				}
			}
		};
	}
	//===================================
	// * Class GCRootPtr
	// * Template class, object type is specified 
	// * supposed to guarantee type safety
	//===================================
	template <typename _GCTy = GCObject>
	class GCRootPtr: private details::GCRootPtrBase
	{
		CHECK_GCOBJECT_TYPE(_GCTy);
	public:
		GCRootPtr(GC *master)
			: GCRootPtrBase(nullptr, master) {}
		GCRootPtr(_GCTy *ptr, GC *master)
			: GCRootPtrBase(ptr, master) {}

		GCRootPtr() = delete;
		GCRootPtr(const GCRootPtr<_GCTy> & gcrp)
			: GCRootPtrBase(gcrp) {}

		template <typename Object>
		GCRootPtr(const GCRootPtr<Object> & gcrp)
			: GCRootPtrBase(gcrp) {
			CHECK_POINTER_CONVERTIBLE(Object, _GCTy);
		}
		GCRootPtr(GCRootPtr<_GCTy> && gcrp)  noexcept
			: GCRootPtrBase(std::move(gcrp)) {}

		template <typename Object>
		GCRootPtr(GCRootPtr<Object> && gcrp)  noexcept
			: GCRootPtrBase(std::move(gcrp)) {
			CHECK_POINTER_CONVERTIBLE(Object, _GCTy);
		}


		GCRootPtr<_GCTy>& operator=(std::nullptr_t) noexcept {
			this->ptr = nullptr;
			return *this;
		}

		template <typename Object>
		GCRootPtr<_GCTy>& operator=(const GCRootPtr<Object> & gcrp) noexcept {
			CHECK_POINTER_CONVERTIBLE(Object, _GCTy);
			this->ptr = gcrp.ptr;
			return *this;
		}

		// NOTICE: *objp MUST be allocated by the same GC
		template <typename Object>
		GCRootPtr<_GCTy>& operator=(Object* objp) noexcept {
			CHECK_POINTER_CONVERTIBLE(Object, _GCTy);
			this->ptr = objp;
			return *this;
		}

		template <typename Object>
		void reset(Object* objp) noexcept {
			CHECK_POINTER_CONVERTIBLE(Object, _GCTy);
			this->ptr = objp;
		}

		void reset() noexcept {  this->ptr = nullptr;  }

		void swap(GCRootPtr<_GCTy>& r) noexcept {
			auto tmp = r.ptr;
			r.ptr = this->ptr;
			this->ptr = tmp;
		}

		/* Does not propagate `const` by default */
		_GCTy* get() const noexcept { return reinterpret_cast<_GCTy*>(ptr); }
		_GCTy* operator->() const noexcept { return get(); }
		_GCTy& operator*() const noexcept { return *get(); }
		operator _GCTy*() const noexcept { return get(); }
	};

	template<typename ObjectType>
	inline std::ostream& operator<<(std::ostream& out, const GCRootPtr<ObjectType>& p) {
		return out << (*p);
	}

#define DEFINE_OPERATOR(type, op) \
	template<typename Left, typename Right>\
	inline type operator op(const GCRootPtr<Left>& left, const GCRootPtr<Right>& right) noexcept {\
		return left.get() op right.get();\
	}\
	template<typename Left, typename Right>\
	inline type operator op(const GCRootPtr<Left>& left, Right *right) noexcept {\
		return left.get() op right;\
	}\
	template<typename Left, typename Right>\
	inline type operator op(Left *left, const GCRootPtr<Right>& right) noexcept {\
		return left op right.get();\
	}\
	template<typename Left>\
	inline type operator op(const GCRootPtr<Left>& left, std::nullptr_t) noexcept {\
		return left.get() op nullptr; \
	}\
	template<typename Right>\
	inline type operator op(std::nullptr_t, const GCRootPtr<Right>& right) noexcept {\
		return nullptr op right.get(); \
	}

	DEFINE_OPERATOR(bool, ==)
	DEFINE_OPERATOR(bool, !=)
	DEFINE_OPERATOR(bool, >)
	DEFINE_OPERATOR(bool, <)
	DEFINE_OPERATOR(bool, >=)
	DEFINE_OPERATOR(bool, <=)
	DEFINE_OPERATOR(std::ptrdiff_t, - )

#undef DEFINE_OPERATOR
#undef CHECK_POINTER_CONVERTIBLE
#undef CHECK_GCOBJECT_TYPE
}

#endif

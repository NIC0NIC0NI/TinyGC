#ifndef _TINYGC_H_
#define _TINYGC_H_
#include <type_traits>
#include <ostream>
#include <chrono>
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
		GCObject() : _mark(false) {}
		virtual ~GCObject() {}

	private:
		GCObject *_next;
		bool _mark;

		friend class GC;

		void GCMark() {
			if (!this->_mark) {
				this->_mark = true;
				this->GCMarkAllSub();
			}
		}
		static void GCMarkSubHelper1(GCObject* sub) {  // check nullptr
			if(sub != nullptr){
				sub->GCMark();
			}
		}
		template<typename T>
		static void GCMarkSubHelper2(T* sub) {  // check type
			CHECK_GCOBJECT_TYPE(T);
			GCMarkSubHelper1(const_cast<typename std::remove_cv<T>::type*>(sub));
		}
	
	protected:
		template<typename ... T>
		static void GCMarkSub(T *... sub) {
			auto forceEvaluate = { (GCMarkSubHelper2<T>(sub), 0) ... };
		}

		virtual void GCMarkAllSub() {}

#define GCOBJECT(Type, Base, ...) \
		void GCMarkAllSub() override { \
			static_assert(std::is_base_of<Base, Type>::value, \
				#Type" is not a subclass of "#Base); \
			Base::GCMarkAllSub();\
			GCMarkSub(__VA_ARGS__);\
		}
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

		template<typename ArgType>
		GCValue& operator=(ArgType&& o) {
			this->data = std::forward<ArgType>(o);
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
	// * Struct GCStatistics
	//===================================
	struct GCStatistics {
		typedef std::chrono::steady_clock Clock;
		typedef std::chrono::time_point<Clock> TimePoint;
		typedef decltype(std::declval<TimePoint>() - std::declval<TimePoint>()) Duration;

		GCStatistics(): hasValue(false) {}

		Duration elapsedTime;
		TimePoint endTime;
		std::size_t collected;
		std::size_t notCollected;
		bool hasValue;
	};

	//===================================
	// * Class GC
	//===================================
	class GC
	{
	public:
		GC() : objectListHead(nullptr), objectNum(0) {}
		~GC() {
			sweep();
		}

		bool checkPoint() {
			return mayCollect();
		}

		template <typename T, typename... Args>
		GCRootPtr<T> newObject(Args &&... args) {
			CHECK_GCOBJECT_TYPE(T);
			auto p = allocateConstruct<T>(std::forward<Args>(args)...);
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
			p->_next = objectListHead;
			objectListHead = p;
			++objectNum;
		}

		// reserved to implement allocators
		template <typename T, typename... Args>
		T* allocateConstruct(Args &&... args) {
			return new T(std::forward<Args>(args)...);
		}

		// reserved to implement allocators
		void destroyDeallocate(GCObject *obj) {
			delete obj;
		}

		GCObject* objectListHead;
		std::size_t objectNum;

		std::forward_list<details::GCRootObserver> observerList;

		GCStatistics lastGC;

		void mark();
		void sweep();	
		void collect();
		bool shouldCollect() const ;
		bool mayCollect();
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
	
	template<typename ObjectType, typename CharT, typename Traits>
	std::basic_ostream<CharT, Traits>&
    operator<<( std::basic_ostream<CharT, Traits>& out, const GCRootPtr<ObjectType>& p) {
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
}

#undef DEFINE_OPERATOR
#undef CHECK_POINTER_CONVERTIBLE
#undef CHECK_GCOBJECT_TYPE

#endif

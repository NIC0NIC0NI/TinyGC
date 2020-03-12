#ifndef _TINYGC_H_
#define _TINYGC_H_
#include <utility>
#include <type_traits>

namespace TinyGC
{
    //===================================
    // * Forward declarations
    //===================================
    class GCObject;
    template <typename T>
    class GCValue;
    class GCReachableSet;
    class GarbageCollector;
    template <typename Ty>
    class GCRootPtr;

    namespace details {
        class GCRootPtrBase;
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
    // * Class GCMarker
    //===================================
    class GCMarker {
        enum { MaxSize = 1024 };
        GCObject* objects[MaxSize];
        std::size_t size;

        void clearStack();
        void markOneObject(GCObject* object);
        friend class GarbageCollector;
    public:
        GCMarker() : size(0) {}

        template<typename T>
        inline void markObject(T* sub) {
            CHECK_GCOBJECT_TYPE(T);  // check type
            markOneObject(static_cast<GCObject*>(const_cast<typename std::remove_cv<T>::type*>(sub)));
        }
        
        template<typename ... T>
        inline void markObjects(T *... sub) {
            auto forceEvaluate = { (markObject<T>(sub), 0) ... };
        }

        template<typename Iter>
        inline void markRange(Iter begin, Iter end) {
            for(; begin != end; ++begin) {
                markObject(*begin);
            }
        }
    };

    //===================================
    // * Class GCObject
    //===================================
    class GCObject {
    private:
        GCObject *GCNextObject;      // this field may be modified
        GarbageCollector *GCMaster;  // this field is not modified after construction, compressed with mark

        friend class GarbageCollector;
        friend class GCMarker;
    protected:
        virtual void GCMarkAllChildren(GCMarker &marker) {}

#define GCOBJECT(Type, Base, ...) \
        void GCMarkAllChildren(TinyGC::GCMarker &marker) override { \
            static_assert(std::is_base_of<Base, Type>::value, \
                #Type" is not a subclass of "#Base); \
            Base::GCMarkAllChildren(marker);\
            marker.markObjects(__VA_ARGS__);\
        }
    public:
        GCObject() : GCMaster(nullptr) {}
        virtual ~GCObject() {}

        // should not be called while collecting garbage
        GarbageCollector * GCGetMaster() const noexcept {
            return GCMaster; // usually it is not marked.
        }
        
        // should not be called while collecting garbage
        void GCSetMaster(GarbageCollector *master) {
            GCMaster = master; // usually it is not marked.
        }
    };

    //===================================
    // * Class GCValue
    //===================================
    template <typename T>
    class GCValue : public GCObject
    {
    public:
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
    // * Class GCContainer
    //===================================
    template <typename C>
    class GCContainer : public GCValue<C> {
    public:
        template <typename... Args>
        explicit GCContainer(Args &&... args)
            : GCValue<C>(std::forward<Args>(args)...) {}

        template<typename ArgType>
        GCContainer& operator=(ArgType&& o) {
            this->get() = std::forward<ArgType>(o);
            return *this;
        }

        virtual void GCMarkAllChildren(GCMarker &marker) override {
            marker.markRange(std::begin(this->get()), std::end(this->get()));
        }
    };

    //===================================
    // * Struct GCStatistics
    //===================================
    struct GCStatistics {
        GCStatistics(): hasValue(false) {}
        std::size_t elapsedTime;
        std::size_t endTime;
        std::size_t collected;
        std::size_t notCollected;
        bool hasValue;
    };

    
    namespace details {
        //===================================
        // * Class GCRootPtrBase
        // * Not a template class
        // * Does not guarantee type safety
        //===================================
        class GCRootPtrBase {
        protected:
            friend class ::TinyGC::GarbageCollector;

            GCObject* ptr;
            GCRootPtrBase *prev;
            GCRootPtrBase *next;

            GCRootPtrBase() :ptr(nullptr), prev(this), next(this) {}

            template<typename Collector>
            GCRootPtrBase(GCObject *p, Collector *master): ptr(p) {
                master->addRoot(this);
            }
            
            // copy is relatively more expensive than raw pointers
            // if used as return value, it relies on compiler optimizations to eliminate copy
            GCRootPtrBase(const GCRootPtrBase & root) :
                ptr(root.ptr){
                insert_into(root.next->prev, root.next);
            }
        
            void insert_into(GCRootPtrBase *p, GCRootPtrBase *n) {
                this->prev = p;
                this->next = n;
                p->next = this;
                n->prev = this;
            }

            ~GCRootPtrBase() {
                this->next->prev = this->prev;
                this->prev->next = this->next;
            }
        };
    }

    //===================================
    // * Class GarbageCollector
    //===================================
    class GarbageCollector
    {
    public:
        bool checkPoint();
        ~GarbageCollector();

        GarbageCollector() : objectNum(0) {}

        template <typename T, typename... Args>
        T* newObject(Args &&... args) {
            CHECK_GCOBJECT_TYPE(T);
            auto p = allocateObject<T>(std::forward<Args>(args)...);
            addObject(p);
            p->GCSetMaster(this);
            return p;
        }

        template <typename T, typename... Args>
        GCValue<T> *newValue(Args &&... args) {
            return newObject<GCValue<T>>(std::forward<Args>(args)...);
        }
        
        template <typename C, typename... Args>
        GCContainer<C> *newContainer(Args &&... args) {
            return newObject<GCContainer<C>>(std::forward<Args>(args)...);
        }

        void addRoot(details::GCRootPtrBase* p) {
            p->insert_into(&listHead, listHead.next);
        }

    private:
        void addObject(GCObject *p) {
            p->GCNextObject = listHead.ptr;
            listHead.ptr = p;
            ++objectNum;
        }

        // reserved to implement allocators
        template <typename T, typename... Args>
        T* allocateObject(Args &&... args) {
            return new T(std::forward<Args>(args)...);
        }

        // reserved to implement allocators
        void destroyObject(GCObject *obj) {
            delete obj;
        }

        // The object `listHead` is the head of root pointers
        // The object `listHead.ptr` points to is the head of all objects;
        details::GCRootPtrBase listHead; 

        std::size_t objectNum;
        GCStatistics lastGC;

        void mark();
        void sweep();    
        void collect();
        bool shouldCollect() const ;
    };

    //===================================
    // * Class GCRootPtr
    // * Template class, object type is specified 
    // * supposed to guarantee type safety
    //===================================
    template <typename Ty = GCObject>
    class GCRootPtr: public details::GCRootPtrBase
    {
        CHECK_GCOBJECT_TYPE(Ty);
    public:
        explicit GCRootPtr(GarbageCollector *master)
            : GCRootPtrBase(nullptr, master) {}
        GCRootPtr(Ty *ptr)
            : GCRootPtrBase(ptr, ptr->GCGetMaster()) {}

        GCRootPtr() = delete;
        GCRootPtr(const GCRootPtr<Ty> & gcrp)
            : GCRootPtrBase(gcrp) {}

        template <typename Object>
        GCRootPtr(const GCRootPtr<Object> & gcrp)
            : GCRootPtrBase(gcrp) {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
        }

        GCRootPtr<Ty>& operator=(std::nullptr_t) noexcept {
            this->ptr = nullptr;
            return *this;
        }

        GCRootPtr<Ty>& operator=(const GCRootPtr<Ty> & gcrp) noexcept {
            this->ptr = gcrp.get();
            return *this;
        }

        template <typename Object>
        GCRootPtr<Ty>& operator=(const GCRootPtr<Object> & gcrp) noexcept {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
            this->ptr = gcrp.get();
            return *this;
        }

        // NOTICE: *objp MUST be allocated by the same GarbageCollector
        template <typename Object>
        GCRootPtr<Ty>& operator=(Object* objp) noexcept {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
            this->ptr = objp;
            return *this;
        }

        template <typename Object>
        void reset(Object* objp) noexcept {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
            this->ptr = objp;
        }

        void reset() noexcept {  this->ptr = nullptr;  }

        void swap(GCRootPtr<Ty>& r) noexcept {
            auto tmp = r.ptr;
            r.ptr = this->ptr;
            this->ptr = tmp;
        }

        /* Does not propagate `const` by default */
        Ty* get() const noexcept { return reinterpret_cast<Ty*>(ptr); }
        Ty* operator->() const noexcept { return get(); }
        Ty& operator*() const noexcept { return *get(); }
        operator Ty*() const noexcept { return get(); }
    };
    
    // relies on compiler optimizations to eliminate copy
    // recomment C++17 which guarantees no copy
    template<typename T>
    GCRootPtr<T> make_root_ptr(T *ptr) {
        return ptr;
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

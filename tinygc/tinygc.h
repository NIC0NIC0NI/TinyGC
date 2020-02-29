#ifndef _TINYGC_H_
#define _TINYGC_H_
#include <type_traits>
#include <ostream>

namespace TinyGC
{
    //===================================
    // * Forward declarations
    //===================================
    class GCObject;
    template <typename T>
    class GCValue;
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

    inline intptr_t GCMasterAsInt(GarbageCollector *master) noexcept {
        return reinterpret_cast<intptr_t>(master);
    }
    inline GarbageCollector* IntAsGCMaster(intptr_t master) noexcept {
        return reinterpret_cast<GarbageCollector *>(master);
    }

    //===================================
    // * Class GCObject
    //===================================
    class GCObject {
    private:
        GCObject *GCNextObject;      // this field may be modified
        GarbageCollector *GCMaster;  // this field is not modified after construction, compressed with mark

        friend class GarbageCollector;        

        intptr_t GCGetMark() const noexcept {
            return GCMasterAsInt(GCMaster) & static_cast<intptr_t>(1);
        }

        void GCSetMark() {
            GCMaster = IntAsGCMaster(GCMasterAsInt(GCMaster) | static_cast<intptr_t>(1));
        }

        void GCClearMark() {
            GCMaster = IntAsGCMaster(GCMasterAsInt(GCMaster) & ~static_cast<intptr_t>(1));
        }

        void GCMark() {
            if (this->GCGetMark() == 0) {
                this->GCSetMark();
                this->GCMarkAllChildren();
            }
        }
        
        template<typename T>
        static inline void GCMark1Child(T* sub) {
            CHECK_GCOBJECT_TYPE(T);  // check type
            GCObject* p = static_cast<GCObject*>(const_cast<typename std::remove_cv<T>::type*>(sub));
            if(p != nullptr){  // check nullptr
                p->GCMark();
            }
        }
    
    protected:
        template<typename ... T>
        static inline void GCMarkMultiple(T *... sub) {
            auto forceEvaluate = { (GCMark1Child<T>(sub), 0) ... };
        }

        template<typename Iter>
        static inline void GCMarkRange(Iter begin, Iter end) {
            for(; begin != end; ++begin) {
                GCMark1Child(*begin);
            }
        }

        virtual void GCMarkAllChildren() {}

#define GCOBJECT(Type, Base, ...) \
        void GCMarkAllChildren() override { \
            static_assert(std::is_base_of<Base, Type>::value, \
                #Type" is not a subclass of "#Base); \
            Base::GCMarkAllChildren();\
            GCMarkMultiple(__VA_ARGS__);\
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

        GarbageCollector() : objectListHead(nullptr), objectNum(0) {}

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

        void addRoot(details::GCRootPtrBase* p) {
            p->insert_into(&rootListHead, rootListHead.next);
        }

    private:
        void addObject(GCObject *p) {
            p->GCNextObject = objectListHead;
            objectListHead = p;
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

        GCObject* objectListHead;
        std::size_t objectNum;
        details::GCRootPtrBase rootListHead;

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
    class GCRootPtr: private details::GCRootPtrBase
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
        GCRootPtr(GCRootPtr<Ty> && gcrp)  noexcept
            : GCRootPtrBase(std::move(gcrp)) {}

        template <typename Object>
        GCRootPtr(GCRootPtr<Object> && gcrp)  noexcept
            : GCRootPtrBase(std::move(gcrp)) {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
        }


        GCRootPtr<Ty>& operator=(std::nullptr_t) noexcept {
            this->ptr = nullptr;
            return *this;
        }

        template <typename Object>
        GCRootPtr<Ty>& operator=(const GCRootPtr<Object> & gcrp) noexcept {
            CHECK_POINTER_CONVERTIBLE(Object, Ty);
            this->ptr = gcrp.ptr;
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
    template<typename T>
    GCRootPtr<T> make_root_ptr(T *ptr) {
        return ptr;
    }

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

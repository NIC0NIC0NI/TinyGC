# TinyGC

A **light-weighted GC** in C++ (C++11) based on **mark-and-sweep** algorithm.

## Features

- Accurate GC
- Controllable collection
- Low memory consumption, compatible with other memory managements 
- Allow multiple instances of `GarbageCollector`
- Not thread-safe for now. Each instance of `GarbageCollector` should be only used in a single thread, do not pass the reference to its allocated objects to another thread.

## Use

1. Create `TinyGC::GarbageCollector` object.
2. Call `newObject` method of `GarbageCollector` to create collectable object whose type is a subclass of `TinyGC::GCObject`. If the object holds references or pointers to other `GCObject`, they must override the virtual function `GCObject::GCMarkAllChildren`. The macro `GCOBJECT` helps to generate the function.
3. Call `newContainer` method of `GarbageCollector` to create collectable object of C++ STL containers of `GCObject*` or its subclass pointers. The wrapper class is `TinyGC::GCContainer<C>`
4. Call `newValue` method of `GarbageCollector` to create collectable object of other C++ classes. The wrapper class is `TinyGC::GCValue<T>`
5. Call `TinyGC::make_root_ptr` create a root pointer as a local or static variable.
6. Call `checkPoint` method of `GarbageCollector` to collect garbage if required.

### Examples

Java:

```Java
class Point
{
    Point(Integer x, Integer y) {
        this.x = x;
        this.y = y;
    }
    Integer x, y;
}

Point p = new Point(5, 6);  // implicitly boxing
```

C++ with TinyGC:

```C++
class Point : public TinyGC::GCObject
{
    using Integer = TinyGC::GCValue<int>*;
public:
    Point(Integer x, Integer y)
        : x(x), y(y) {}
    
    Integer x, y;

private:
    GCOBJECT(Point, TinyGC::GCObject, x, y)
}

int main()
{
    TinyGC::GarbageCollector GC;
    {
        auto p = TinyGC::make_root_ptr(GC.newObject<Point>(
            GC.newValue<int>(5),
            GC.newValue<int>(6)
        ));
    }
    GC.collect();
}
```

## Note

- For TinyGC, `GarbageCollector::newContainer`, `GarbageCollector::newValue` and `GarbageCollector::newObject` is the **only** correct way to create collectable objects。
- All the objects allocated by `GarbageCollector` are owned by the `GarbageCollector` object. It will release all resources once go out of scope, therefore can be used within a function, as a non-static menber of class or as thread local.
- `make_root_ptr` returns a `GCRootPtr` smart pointer that would guarantee the object it points to will not be collected.
- The storage of `GCObject` is made up of three pointers: a pointer to virtual table, a pointer to the next `GCObject`, and a pointer to `GarbageCollector` who allocates it. While collecting garbage, the mark bit is compressed into the lowest bit of pointer. The storage of `GCRootPtr` is made up of three pointers, a pointer to `GCObject` and two pointers to the previous and next `GCRootPtr`.


## License

Apache License 2.0
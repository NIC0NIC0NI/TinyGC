# TinyGC

A **light-weighted GC** in C++ (C++11) based on **mark-and-sweep** algorithm.

## Features

- Accurate GC
- Controllable collection
- Low memory consumption, compatible with other memory managements 
- Allow multiple instances of `GarbageCollector`
- Not thread-safe for now. Use it in a single thread

## Use

1. Create `TinyGC::GarbageCollector` object.
2. Call `newObject` method of `TinyGC::GarbageCollector` to create collectable object whose type is a subclass of `GCObject`.
3. Call `newValue` method of `TinyGC::GarbageCollector` to create collectable object of other C++ classes.
4. Call `TinyGC::make_root_ptr` create a root pointer as a local or static variable.
5. Call `collect` method of `TinyGC::GarbageCollector` to collect garbage.

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

- For TinyGC, `GarbageCollector::newValue` and `GarbageCollector::newObject` is the **only** correct way to create collectable objects。
- All the objects allocated by `GarbageCollector` are owned by the `GarbageCollector` object. It will release all resources once go out of scope, therefore can be used within a function, as a non-static menber of class or as thread local.
- `make_root_ptr` returns a `GCRootPtr` smart pointer that would guarantee the object it points to will not be collected.
- The storage of `GCObject` is made up of three pointers: a pointer to virtual table, a pointer to the next `GCObject`, and a pointer to `GarbageCollector` who allocates it. While collecting garbage, the mark bit is compressed into the lowest bit of pointer. The storage of `GCRootPtr` is made up of three pointers, a pointer to `GCObject` and two pointers to the previous and next `GCRootPtr`.


## License

Apache License 2.0
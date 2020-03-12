# TinyGC

基于 **标记·清除** 算法，配合C++的 **轻量级GC**（使用C++11）

## 特征

- 准确式GC
- 可控的垃圾回收
- 不独占内存（内存占用小），与其他内存管理方式兼容
- 允许拥有多个 `GarbageCollector` 实例
- 目前是线程不安全的，每个 `GarbageCollector` 实例只能用于一个线程内，不要将它分配的对象的引用传给其它线程。

## 使用方法

1. 创建 `TinyGC::GarbageCollector` 对象。
2. 使用 `newObject` 方法 创建 `TinyGC::GCObject` 子类的可回收对象。如果该对象持有其它 `GCObject` 的指针，则应该重载虚函数 `GCObject::GCMarkAllChildren`。使用 `GCOBJECT` 宏可以方便地生成重载函数。
3. 使用 `newContainer` 方法 创建 C++ STL 中 `GCObject*` 或其子类指针的容器。其类型为 `TinyGC::GCContainer<C>`。
3. 使用 `newValue` 方法 创建其它C++类的可回收对象。其类型为 `TinyGC::GCValue<T>`。
4. 使用 `TinyGC::make_root_ptr` 创建局部或静态的根引用。
5. 使用 `checkPoint` 方法 在需要的时候进行垃圾回收。

### 示例

Java 代码：

```Java
class Point
{
    Point(Integer x, Integer y) {
        this.x = x;
        this.y = y;
    }
    Integer x, y;
}

Point p = new Point(5, 6);  // 隐式装箱
```

C++ with TinyGC 代码：

```C++
class Point : public TinyGC::GCObject
{
    using Integer = TinyGC::GCValue<int>*;
public:
    Point(Int x, Int y)
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


## 备注

- 对于TinyGC来说，`GarbageCollector::newContainer`，`GarbageCollector::newValue` 和 `GarbageCollector::newObject` 是**唯一**正确的创建可回收对象的方式。
- 资源所有权归 `GarbageCollector` 对象。该对象会在生命周期结束后自动回收所有对象，因此可在函数作用域内建立 `TinyGC::GarbageCollector` 对象，作为其它类的成员，或作为`thread_local`
- `make_root_ptr` 会返回一个 `GCRootPtr` 智能指针，在整个生命周期内为根引用。
- `GCObject` 占用空间由三个指针构成：虚函数表指针、下一个 `GCObject` 对象指针，以及 `GarbageCollector` 对象指针，在标记被引用的对象时，标记位压缩在指针的最低位。`GCRootPtr`占用空间由三个指针构成，指向 `GCObject` 的指针，以及指向上一个和下一个 `GCRootPtr` 的指针。

## 许可

Apache Licene 2.0
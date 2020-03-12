#include <iostream>
#include <string>
#include <vector>
#include "tinygc.h"

template<typename T>
void println(T && msg) {
    std::cout << msg << std::endl;
}

struct Point : public TinyGC::GCObject
{
    Point(TinyGC::GCValue<int> *x, TinyGC::GCValue<int> *y)
        : x(x), y(y) {
        println("New Point " + to_string());
    }
    Point(const Point *another)
        : x(another->x), y(another->y) {
        println("New Point " + to_string());
    }
    ~Point() {
        println("Delete Point");
    }

    std::string to_string() const {
        return "(" + std::to_string(*x) + ", " + std::to_string(*y) + ")";
    }

    TinyGC::GCValue<int> *x, *y;

protected:
    GCOBJECT(Point, TinyGC::GCObject, x, y)
};

struct LineSegment : public TinyGC::GCObject
{
    LineSegment(Point *p0, Point *p1)
        : p0(p0), p1(*p1) {
        println("New LineSegment " + to_string());
    }
    ~LineSegment() {
        println("Delete LineSegment");
    }

    std::string to_string() const {
        return "(" + p0->to_string() + ", " + p1.to_string() + ")";
    }

    Point *p0;
    Point &p1;

protected:
    GCOBJECT(LineSegment, TinyGC::GCObject, p0, std::addressof(p1)) // take address of reference
};

struct AnotherLineSegment : public Point
{
    AnotherLineSegment(Point *p0, Point *p1)
        : Point(p0), p1(p1) {
        println("New AnotherLineSegment " + to_string());
    }
    ~AnotherLineSegment() {
        println("Delete AnotherLineSegment");
    }

    std::string to_string() const {
        return "(" + Point::to_string() + ", " + p1->to_string() + ")";
    }

    Point *p1;

protected:
    GCOBJECT(AnotherLineSegment, Point, p1) // Tag the base class Point
};

struct PODPoint {
	int x, y;
    PODPoint(int xx, int yy): x(xx), y(yy) {}
    std::string to_string() const {
        return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
};

struct PODLineSegment {
	PODPoint p0, p1;
    PODLineSegment(const PODPoint &pp0, const PODPoint &pp1): p0(pp0), p1(pp1) {
        println("New PODLineSegment " + to_string());
    }
    ~PODLineSegment() {
        println("Delete PODLineSegment");
    }
    std::string to_string() const {
        return "(" + p0.to_string() + ", " + p1.to_string() + ")";
    }
};

struct CircularRef : public TinyGC::GCObject {
    CircularRef *first;
    CircularRef *second;
    CircularRef(CircularRef *f = nullptr, CircularRef *s = nullptr) : first(f), second(s) {
        println("New CircularRef");
    }
    ~CircularRef() {
        println("Delete CircularRef");
    }
protected:
    GCOBJECT(CircularRef, TinyGC::GCObject, first, second)
};


//===================================
// * Point -> x (GCValue<int>)
// *       -> y (GCValue<int>)
// * LineSegment -> p0 (Point)
// *             -> p1 (Point)
// * AnotherLineSegment : Point -> x (GCValue<int>)
// *                            -> y (GCValue<int>)
// *                            -> p1 (Point)
// * CircularRef -> first  (CircularRef)
// *             -> second (CircularRef)
// *
// * PODPoint and PODLineSegment are POD
//===================================

CircularRef* create_circular_ref(TinyGC::GarbageCollector &gc) {
    // these will be discarded
    auto x = gc.newObject<CircularRef>();
    auto y = gc.newObject<CircularRef>(x);
    auto z = gc.newObject<CircularRef>(x, y);
    x->first = y;
    x->second = z;
    y->second = z;
    return x;
}

template<typename C, typename ... Args>
TinyGC::GCRootPtr<TinyGC::GCContainer<C>> make_root_container(TinyGC::GarbageCollector &gc, Args && ... args) {
    auto il = {args ... };
    return gc.newContainer<C>(il);
}

Point* make_point(TinyGC::GarbageCollector &gc, int x, int y) {
    return gc.newObject<Point>(gc.newValue<int>(x), gc.newValue<int>(y));
}

TinyGC::GCRootPtr<Point> make_root_point(TinyGC::GarbageCollector &gc, int x, int y) {
    return make_point(gc, x, y);
}

int main(int argc, char **argv)
{
    using TinyGC::GCRootPtr;
    using TinyGC::GCObject;
    using TinyGC::GCValue;
    using TinyGC::make_root_ptr;
    {
        TinyGC::GarbageCollector gc;

        // GCRootPtr<int> x = gc.newValue<int>(100);
        GCRootPtr<GCValue<int>> x = gc.newValue<int>(100);

        auto p1 = make_root_point(gc, 1, 2);
        auto p2 = make_root_point(gc, 3, 4);
        auto p3 = make_root_point(gc, 5, 6);
        auto p4 = make_root_ptr(gc.newObject<Point>(p1->y, p3->x));
        auto p5 = make_root_ptr(gc.newObject<Point>(p2->x, x));

        auto l1 = make_root_ptr(gc.newObject<LineSegment>(p1, p2));
        auto l2 = make_root_ptr(gc.newObject<LineSegment>(p5, p3));
        auto l3 = make_root_ptr(gc.newObject<AnotherLineSegment>(p3, p1));     // copies point p3

        auto vector = make_root_container<std::vector<Point*>>(gc, 
            make_point(gc, 200, 201), make_point(gc, 215, 261), make_point(gc, 268, 237),
            make_point(gc, 205, 207), make_point(gc, 210, 271), make_point(gc, 240, 206));

        create_circular_ref(gc); // discarded
        auto circular = make_root_ptr(create_circular_ref(gc));

        {
            GCRootPtr<GCObject> obj = \
                gc.newObject<AnotherLineSegment>(make_point(gc, 7, 8), make_point(gc, 9, 10));
            auto pod = make_root_ptr(gc.newValue<PODLineSegment>(PODPoint(1, 3), PODPoint(2, 4)));


            // loss reachability to l1, p2, p4, vector[4], vector[5], obj, obj->p0, obj->p1
            p1 = nullptr;
            p2 = p5;
            p4 = l2->p0;
            l1 = nullptr;
            obj = p5;
            p3 = l3;
            p5 = nullptr;

            vector->get().pop_back();
            vector->get().pop_back();

            // should delete original not_a_root, l1, p2, p4, vector[4], vector[5], obj, obj::base, obj->p0, obj->p1
            if(gc.checkPoint()){  // collect
                println("Garbage Collector triggerred");
            } else {
                println("Garbage Collector not triggerred");
            } 
            println("l3 = " + l2->to_string());
            println("l3 = " + l3->to_string());
            println("pod = " + pod->get().to_string());
            println("vector:");
            int i = 0;
            for(auto p : vector->get()) {
                println("[" + std::to_string(i++) + "] = " + p->to_string());
            }
        }
        if(gc.checkPoint()){
            println("Garbage Collector triggerred");
        } else {
            println("Garbage Collector not triggerred");
        } 

        println("p2 = " + p2->to_string());
        println("p3 = " + p3->to_string());  // to_string is not virtual
        println("p4 = " + p4->to_string());
    }
    return 0;
}

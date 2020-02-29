#include <iostream>
#include <string>
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
        : p0(p0), p1(p1) {
        println("New LineSegment " + to_string());
    }
    ~LineSegment() {
        println("Delete LineSegment");
    }

    std::string to_string() const {
        return "(" + p0->to_string() + ", " + p1->to_string() + ")";
    }

    Point *p0, *p1;

protected:
    GCOBJECT(LineSegment, TinyGC::GCObject, p0, p1)
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
    GCOBJECT(AnotherLineSegment, Point, p1)
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


TinyGC::GCRootPtr<Point> make_point(TinyGC::GarbageCollector &GarbageCollector, int x, int y) {
    return GarbageCollector.newObject<Point>(
            GarbageCollector.newValue<int>(x),
            GarbageCollector.newValue<int>(y));
}

int main(void)
{
    {
        TinyGC::GarbageCollector GarbageCollector;

        auto p1 = make_point(GarbageCollector, 1, 2);
        auto p2 = make_point(GarbageCollector, 3, 4);
        auto p3 = make_point(GarbageCollector, 5, 6);

        auto dp = TinyGC::make_root_ptr(GarbageCollector.newObject<LineSegment>(p1, p2));

        {
            auto tdp = TinyGC::make_root_ptr(GarbageCollector.newObject<AnotherLineSegment>(p3, p1));
            auto tdp2 = tdp;
            {
                auto ttdp = TinyGC::make_root_ptr(GarbageCollector.newValue<PODLineSegment>(PODPoint(1, 3), PODPoint(2, 4)));
            }
            p1 = nullptr;
            p2 = nullptr;
            p3 = nullptr;

            if(GarbageCollector.checkPoint()){  // collect
                println("Garbage Collector triggerred");
            } else {
                println("Garbage Collector not triggerred");
            } 
        }
        if(GarbageCollector.checkPoint()){
            println("Garbage Collector triggerred");
        } else {
            println("Garbage Collector not triggerred");
        } 
    }
	std::cout << sizeof(TinyGC::GCValue<int>) << std::endl;
    return 0;
}

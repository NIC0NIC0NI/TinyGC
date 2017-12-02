#include <cstdio>
#include <string>
#include "../tinygc/tinygc.h"

void println(const std::string &msg) {
	std::printf("%s\n", msg.c_str());
}

struct Point : public TinyGC::GCObject
{
	Point(TinyGC::GCValue<int> *x, TinyGC::GCValue<int> *y)
		: x(x), y(y) {
		println("New Point");
	}
	Point(const Point &another)
		: x(another.x), y(another.y) {
		println("New Point");
	}
	~Point() {
		println("Release Point");
	}

	std::string to_string() const {
		return "(" + std::to_string(*x) + ", " + std::to_string(*y) + ")";
	}

	TinyGC::GCValue<int> *x, *y;

protected:
	void GCMarkAllSub() override  {
		GCMarkSub(x);
		GCMarkSub(y);
	}
};

struct DoublePoint : public TinyGC::GCObject
{
	DoublePoint(Point *p0, Point *p1)
		: p0(p0), p1(p1) {
		println("New DoublePoint");
	}
	~DoublePoint() {
		println("Release DoublePoint");
	}

	std::string to_string() const {
		return "(" + p0->to_string() + ", " + p1->to_string() + ")";
	}

	Point *p0, *p1;

protected:
	void GCMarkAllSub() override {
		GCMarkSub(p0);
		GCMarkSub(p1);
	}
};

struct AnotherDoublePoint : public Point
{
	AnotherDoublePoint(Point *p0, Point *p1)
		: Point(*p0), p1(p1) {
		println("New AnotherDoublePoint");
	}
	~AnotherDoublePoint() {
		println("Release AnotherDoublePoint");
	}

	std::string to_string() const {
		return "(" + Point::to_string() + ", " + p1->to_string() + ")";
	}

	Point *p1;

protected:
	void GCMarkAllSub() override {
		Point::GCMarkAllSub();
		GCMarkSub(p1);
	}
};

struct Test
{
	Test() {
		println("New Test");
	}
	~Test() {
		println("Release Test");
	}
};

TinyGC::GCRootPtr<Point> make_point(TinyGC::GC &GC, int x, int y) {
	return GC.newObject<Point>(
			GC.newValue<int>(x),
			GC.newValue<int>(y));
}

int main(void)
{
	{
		TinyGC::GC GC;

		auto p1 = make_point(GC, 1, 2);
		auto p2 = make_point(GC, 3, 4);
		auto p3 = make_point(GC, 5, 6);

		auto dp = GC.newObject<DoublePoint>(p1, p2);

		{
			auto tdp = GC.newObject<AnotherDoublePoint>(p3, p1);
			auto tdp2 = tdp;
			{
				auto ttdp = GC.newValue<Test>();
			}
			p1 = nullptr;
			p2 = nullptr;
			p3 = nullptr;
			if (p3 == nullptr) {
				println("GC Start");
				GC.collect();
				println("GC End");
			}
		}

		println("GC Start");
		GC.collect();
		println("GC End");
	}
	return 0;
}

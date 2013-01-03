/**
 * $Id$
 * Copyright (C) 2008 - 2011 Nils Asmussen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <esc/common.h>
#include <esc/test.h>
#include <stdlib.h>
#include <memory>

using namespace std;

struct A {
	virtual ~A() {}
};
struct B : public A {};

/* forward declarations */
static void test_smartptr(void);
static void test_shared_basic(void);
static void test_shared_modifiers(void);
static void test_shared_casts(void);
static void test_unique_basic(void);

/* our test-module */
sTestModule tModSmartPtr = {
	"Smart Pointer",
	&test_smartptr
};

static void test_smartptr(void) {
	test_shared_basic();
	test_shared_modifiers();
	test_shared_casts();
	test_unique_basic();
}

static void test_shared_basic(void) {
	size_t before,after;
	test_caseStart("Testing basic operations of shared_ptr");

	before = heapspace();
	{
		shared_ptr<int> a;
		test_assertFalse((bool)a);
		test_assertLInt(a.use_count(),0);
		test_assertPtr(a.get(),nullptr);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		shared_ptr<int> a(new int(3));
		test_assertTrue((bool)a);
		test_assertLInt(a.use_count(),1);
		test_assertInt(*a.get(),3);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		shared_ptr<int> a(new int(3));
		shared_ptr<int> b(a);
		shared_ptr<int> c;
		shared_ptr<int> d(shared_ptr<int>(new int(4)));
		c = b;
		test_assertLInt(a.use_count(),3);
		test_assertInt(*a.get(),3);
		test_assertLInt(b.use_count(),3);
		test_assertInt(*b.get(),3);
		test_assertLInt(c.use_count(),3);
		test_assertInt(*c.get(),3);
		test_assertLInt(d.use_count(),1);
		test_assertInt(*d.get(),4);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		B *aptr = new B;
		shared_ptr<B> b(aptr);
		shared_ptr<A> a1(b);
		shared_ptr<A> a2;
		shared_ptr<A> a3(shared_ptr<B>(new B));
		a2 = b;
		test_assertLInt(b.use_count(),3);
		test_assertPtr(b.get(),aptr);
		test_assertLInt(a1.use_count(),3);
		test_assertPtr(a1.get(),aptr);
		test_assertLInt(a2.use_count(),3);
		test_assertPtr(a2.get(),aptr);
		test_assertLInt(a3.use_count(),1);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		A *obj1 = new A;
		A *obj2 = new A;
		shared_ptr<A> a(obj1);
		shared_ptr<A> b(a);
		shared_ptr<A> c(obj2);
		test_assertTrue(a == b);
		test_assertFalse(a != b);
		if(obj1 < obj2) {
			test_assertTrue(a < c);
			test_assertFalse(a >= c);
			test_assertFalse(a > c);
		}
		else {
			test_assertFalse(a < c);
			test_assertTrue(a >= c);
			test_assertTrue(a > c);
		}
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		shared_ptr<int> a = shared_ptr<int>(unique_ptr<int>());
		shared_ptr<int> b = shared_ptr<int>(unique_ptr<int>(new int(3)));
		shared_ptr<int> c;
		c = unique_ptr<int>();
		shared_ptr<int> d;
		d = unique_ptr<int>(new int(4));
		test_assertLInt(a.use_count(),0);
		test_assertPtr(a.get(),nullptr);
		test_assertLInt(b.use_count(),1);
		test_assertInt(*b.get(),3);
		test_assertLInt(c.use_count(),0);
		test_assertPtr(c.get(),nullptr);
		test_assertLInt(d.use_count(),1);
		test_assertInt(*d.get(),4);
	}
	after = heapspace();
	test_assertSize(after,before);

	test_caseSucceeded();
}

static void test_shared_modifiers(void) {
	size_t before,after;
	test_caseStart("Testing modifiers of shared_ptr");

	before = heapspace();
	{
		shared_ptr<int> a(new int(3));
		shared_ptr<int> b(new int(4));
		a.swap(b);
		test_assertLInt(a.use_count(),1);
		test_assertInt(*a.get(),4);
		test_assertLInt(b.use_count(),1);
		test_assertInt(*b.get(),3);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		shared_ptr<B> b(new B);
		shared_ptr<A> a(new A);
		b.reset(new B);
		a.reset(new B);
		test_assertLInt(b.use_count(),1);
		test_assertLInt(a.use_count(),1);
	}
	after = heapspace();
	test_assertSize(after,before);

	test_caseSucceeded();
}

static void test_shared_casts(void) {
	size_t before,after;
	test_caseStart("Testing casts of shared_ptr");

	before = heapspace();
	{
		shared_ptr<A> a(new A);
		shared_ptr<B> b = static_pointer_cast<B>(a);
		shared_ptr<const A> a1(new A);
		shared_ptr<A> a2 = const_pointer_cast<A>(a1);
		shared_ptr<B> b2 = dynamic_pointer_cast<B>(a);
		test_assertLInt(a.use_count(),2);
		test_assertLInt(b.use_count(),2);
		test_assertLInt(a1.use_count(),2);
		test_assertLInt(a2.use_count(),2);
		test_assertPtr(b2.get(),nullptr);
		test_assertLInt(b2.use_count(),0);
	}
	after = heapspace();
	test_assertSize(after,before);

	test_caseSucceeded();
}

static void test_unique_basic(void) {
	size_t before,after;
	test_caseStart("Testing basic operations of unique_ptr");

	before = heapspace();
	{
		unique_ptr<int> a = unique_ptr<int>(unique_ptr<int>());
		unique_ptr<int> b;
		b = unique_ptr<int>();
		test_assertFalse((bool)a);
		test_assertPtr(a.get(),nullptr);
		test_assertFalse((bool)b);
		test_assertPtr(b.get(),nullptr);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		unique_ptr<int> a = unique_ptr<int>(unique_ptr<int>(new int(3)));
		unique_ptr<int> b;
		b = unique_ptr<int>(new int(4));
		test_assertTrue((bool)a);
		test_assertInt(*a.get(),3);
		test_assertTrue((bool)b);
		test_assertInt(*b.get(),4);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		unique_ptr<A> a = unique_ptr<B>(unique_ptr<B>(new B));
		unique_ptr<A> b;
		b = unique_ptr<B>(new B);
		test_assertTrue((bool)a);
		test_assertTrue((bool)b);
	}
	after = heapspace();
	test_assertSize(after,before);

	before = heapspace();
	{
		A *obj1 = new A;
		A *obj2 = new A;
		unique_ptr<A> a(obj1);
		unique_ptr<A> b(obj1);
		unique_ptr<A> c(obj2);
		test_assertTrue(a == b);
		test_assertFalse(a != b);
		if(obj1 < obj2) {
			test_assertTrue(a < c);
			test_assertFalse(a >= c);
			test_assertFalse(a > c);
		}
		else {
			test_assertFalse(a < c);
			test_assertTrue(a >= c);
			test_assertTrue(a > c);
		}
		b.release();
	}
	after = heapspace();
	test_assertSize(after,before);

	test_caseSucceeded();
}

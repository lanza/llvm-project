// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test APValue emission for member pointers with CIR, LLVM lowering,
// and comparison to original CodeGen.

struct S {
  void foo();
  virtual void bar();
};

// Test 1: Non-virtual member function pointer
// CIR: cir.global external @pmf1 = #cir.method<@_ZN1S3fooEv>
// LLVM: @pmf1 = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1S3fooEv to i64), i64 0 }
// OGCG: @pmf1 = global { i64, i64 } { i64 ptrtoint (ptr @_ZN1S3fooEv to i64), i64 0 }
extern void (S::*pmf1)();
void (S::*pmf1)() = &S::foo;

// Test 2: Virtual member function pointer
// CIR: cir.global external @pmf2 = #cir.method<vtable_offset = {{[0-9]+}}>
// LLVM: @pmf2 = global { i64, i64 } { i64 {{[0-9]+}}, i64 0 }
// OGCG: @pmf2 = global { i64, i64 } { i64 {{[0-9]+}}, i64 0 }
extern void (S::*pmf2)();
void (S::*pmf2)() = &S::bar;

// Test 3: Null member function pointer
// CIR: cir.global external @pmf3 = #cir.method<null>
// LLVM: @pmf3 = global { i64, i64 } zeroinitializer
// OGCG: @pmf3 = global { i64, i64 } zeroinitializer
extern void (S::*pmf3)();
void (S::*pmf3)() = nullptr;

// Use a simple struct without vtable for data member pointer tests.
// Data member pointers to structs with vtables is a pre-existing bug
// (field index doesn't account for vtable pointer).
struct Point {
  int x;
  int y;
};

// Test 4: Data member pointer to first field
// CIR: cir.global external @pdm1 = #cir.data_member<0>
// LLVM: @pdm1 = global i64 0
// OGCG: @pdm1 = global i64 0
extern int Point::*pdm1;
int Point::*pdm1 = &Point::x;

// Test 5: Data member pointer to second field
// CIR: cir.global external @pdm2 = #cir.data_member<1>
// LLVM: @pdm2 = global i64 4
// OGCG: @pdm2 = global i64 4
extern int Point::*pdm2;
int Point::*pdm2 = &Point::y;

// Test 6: Null data member pointer
// CIR: cir.global external @pdm3 = #cir.data_member<null>
// LLVM: @pdm3 = global i64 -1
// OGCG: @pdm3 = global i64 -1
extern int Point::*pdm3;
int Point::*pdm3 = nullptr;

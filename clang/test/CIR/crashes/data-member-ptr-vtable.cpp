// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
//
// XFAIL: *
//
// Data member pointer initialization for structs with vtables crashes.
//
// The issue is that `fieldDecl->getFieldIndex()` returns the AST field index,
// but for classes with vtables, the CIR record type has the vtable pointer
// as an implicit first element. This causes a mismatch:
//
//   - AST field index for 'x': 0
//   - CIR record element index for 'x': 1 (vtable ptr is at 0)
//
// This causes DataMemberAttr verification to fail because element[0] is
// the vtable pointer, not the 'int' type expected.
//
// Error: member type of a #cir.data_member attribute must match the attribute type

struct S {
  virtual void foo();
  int x;
  int y;
};

// CHECK: cir.global external @pdm = #cir.data_member<1>
extern int S::*pdm;
int S::*pdm = &S::x;

; RUN: nl %s -passes=foldaddconstant | FileCheck %s

define i32 @foo() {
  %a = add nsw i32 2, 1
  ret i32 %a
}

;      CHECK: define i32 @foo() {
; CHECK-NEXT:   %a = add nsw i32 2, 1
; CHECK-NEXT:   ret i32 3

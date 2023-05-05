; RUN: nl %s -passes=deadinsn | FileCheck %s

define i32 @foo() {
  %a = add nsw i32 2, 1
  %b = add nsw i32 3, 0
  ret i32 %a
}

;      CHECK: define i32 @foo() {
; CHECK-NEXT:   %a = add nsw i32 2, 1
; CHECK-NEXT:   ret i32 %a

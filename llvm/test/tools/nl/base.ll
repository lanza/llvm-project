; RUN: nl %s | FileCheck %s

define i32 @foo() {
  %a = add nsw i32 2, 1
  %b = add nsw i32 %a, 0
  %c = add nsw i32 %a, %b
  ret i32 %c
}

;      CHECK: define i32 @foo() {
; CHECK-NEXT:   ret i32 6

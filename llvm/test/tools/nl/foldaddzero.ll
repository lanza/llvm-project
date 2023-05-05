; RUN: nl %s -passes=foldaddzero | FileCheck %s

define i32 @foo() {
  %a = add nsw i32 2, 1
  %b = add nsw i32 %a, 0
  ret i32 %b
}

;      CHECK: define i32 @foo() {
; CHECK-NEXT:   %a = add nsw i32 2, 1
; CHECK-NEXT:   %b = add nsw i32 %a, 0
; CHECK-NEXT:   ret i32 %a

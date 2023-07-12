define i32 @bar(i32 noundef %b) {
entry:
  %b.addr = alloca i32, align 4
  store i32 %b, i32* %b.addr, align 4
  %0 = load i32, i32* %b.addr, align 4
  %mul = mul nsw i32 %0, 2
  ret i32 %mul
}

define i32 @foo(i32 noundef %b) {
entry:
  %b.addr = alloca i32, align 4
  store i32 %b, i32* %b.addr, align 4
  %0 = load i32, i32* %b.addr, align 4
  %call = call i32 @bar(i32 noundef %0)
  %mul = mul nsw i32 %call, 4
  ret i32 %mul
}

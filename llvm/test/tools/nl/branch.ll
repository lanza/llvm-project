define i32 @foo(i32 %b) {
entry:
  %retval = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %b, i32* %b.addr, align 4
  %0 = load i32, i32* %b.addr, align 4
  %tobool = icmp ne i32 %0, 0
  br i1 %tobool, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  store i32 1, i32* %retval, align 4
  br label %return

if.else:                                          ; preds = %entry
  store i32 2, i32* %retval, align 4
  br label %return

return:                                           ; preds = %if.else, %if.then
  %1 = load i32, i32* %retval, align 4
  ret i32 %1
}

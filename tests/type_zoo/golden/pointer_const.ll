
@0 = private unnamed_addr constant [10 x i8] c"%d %d %d\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @sum(ptr %p, i32 %n) {
entry:
  %i = alloca i32, align 4
  %s = alloca i32, align 4
  %n2 = alloca i32, align 4
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  store i32 %n, ptr %n2, align 4
  store i32 0, ptr %s, align 4
  store i32 0, ptr %i, align 4
  br label %for

for:                                              ; preds = %for_step, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %n2, align 4
  %2 = icmp slt i32 %0, %1
  br i1 %2, label %for_body, label %for_exit

for_body:                                         ; preds = %for
  %3 = load i32, ptr %s, align 4
  %4 = load i32, ptr %i, align 4
  %5 = load ptr, ptr %p1, align 8
  %6 = getelementptr i32, ptr %5, i32 %4
  %7 = load i32, ptr %6, align 4
  %8 = add i32 %3, %7
  store i32 %8, ptr %s, align 4
  br label %for_step

for_step:                                         ; preds = %for_body
  %9 = load i32, ptr %i, align 4
  %10 = add i32 %9, 1
  store i32 %10, ptr %i, align 4
  br label %for

for_exit:                                         ; preds = %for
  %11 = load i32, ptr %s, align 4
  ret i32 %11
}

define i32 @main() {
entry:
  %cp = alloca ptr, align 8
  %total = alloca i32, align 4
  %first = alloca i32, align 4
  %q = alloca ptr, align 8
  %r = alloca ptr, align 8
  %a = alloca [3 x i32], align 4
  %0 = getelementptr [3 x i32], ptr %a, i64 0, i32 0
  store i32 1, ptr %0, align 4
  %1 = getelementptr [3 x i32], ptr %a, i64 0, i32 1
  store i32 2, ptr %1, align 4
  %2 = getelementptr [3 x i32], ptr %a, i64 0, i32 2
  store i32 3, ptr %2, align 4
  %3 = getelementptr [3 x i32], ptr %a, i64 0, i32 0
  %4 = load i32, ptr %3, align 4
  %5 = getelementptr [3 x i32], ptr %a, i64 0, i32 0
  store ptr %5, ptr %r, align 8
  %6 = getelementptr [3 x i32], ptr %a, i64 0, i32 0
  %7 = load i32, ptr %6, align 4
  %8 = getelementptr [3 x i32], ptr %a, i64 0, i32 0
  store ptr %8, ptr %q, align 8
  %9 = load ptr, ptr %q, align 8
  store ptr %9, ptr %r, align 8
  %10 = load ptr, ptr %r, align 8
  %11 = load i32, ptr %10, align 4
  store i32 %11, ptr %first, align 4
  %12 = load ptr, ptr %q, align 8
  store i32 10, ptr %12, align 4
  %13 = load ptr, ptr %q, align 8
  %14 = call i32 @sum(ptr %13, i32 3)
  store i32 %14, ptr %total, align 4
  %15 = getelementptr [3 x i32], ptr %a, i64 0, i32 1
  %16 = load i32, ptr %15, align 4
  %17 = getelementptr [3 x i32], ptr %a, i64 0, i32 1
  store ptr %17, ptr %cp, align 8
  %18 = load ptr, ptr %cp, align 8
  store i32 20, ptr %18, align 4
  %19 = load i32, ptr %first, align 4
  %20 = load i32, ptr %total, align 4
  %21 = load ptr, ptr %cp, align 8
  %22 = load i32, ptr %21, align 4
  %23 = call i32 (ptr, ...) @printf(ptr @0, i32 %19, i32 %20, i32 %22)
  ret i32 0
}


%List_int = type { ptr, i32, i32 }

@0 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

declare i32 @printf(ptr, ...)

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @memcpy(ptr, ptr, i32)

declare ptr @memset(ptr, i32, i32)

declare ptr @memmove(ptr, ptr, i32)

declare i32 @memcmp(ptr, ptr, i32)

declare i32 @strlen(ptr)

declare ptr @memchr(ptr, i32, i32)

define i32 @main() {
entry:
  %i = alloca i32, align 4
  %nums = alloca %List_int, align 8
  %0 = load %List_int, ptr %nums, align 8
  call void @List_init_int(ptr %nums, i32 4)
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %while_body, %entry
  %1 = load i32, ptr %i, align 4
  %2 = icmp slt i32 %1, 5
  br i1 %2, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %3 = load %List_int, ptr %nums, align 8
  %4 = load i32, ptr %i, align 4
  %5 = load i32, ptr %i, align 4
  %6 = mul i32 %4, %5
  call void @List_push_int(ptr %nums, i32 %6)
  %7 = load i32, ptr %i, align 4
  %8 = add i32 %7, 1
  store i32 %8, ptr %i, align 4
  br label %while

while_exit:                                       ; preds = %while
  %9 = load %List_int, ptr %nums, align 8
  %10 = call i32 @List_len_int(ptr %nums)
  %11 = call i32 (ptr, ...) @printf(ptr @0, i32 %10)
  %12 = load %List_int, ptr %nums, align 8
  %13 = call i32 @List_get_int(ptr %nums, i32 0)
  %14 = call i32 (ptr, ...) @printf(ptr @1, i32 %13)
  %15 = load %List_int, ptr %nums, align 8
  %16 = call i32 @List_get_int(ptr %nums, i32 4)
  %17 = call i32 (ptr, ...) @printf(ptr @2, i32 %16)
  %18 = load %List_int, ptr %nums, align 8
  call void @List_free_int(ptr %nums)
  ret i32 0
}

define void @List_init_int(ptr %self, i32 %initial_cap) {
entry:
  %cap = alloca i32, align 4
  %initial_cap2 = alloca i32, align 4
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i32 %initial_cap, ptr %initial_cap2, align 4
  %0 = load i32, ptr %initial_cap2, align 4
  store i32 %0, ptr %cap, align 4
  %1 = load i32, ptr %cap, align 4
  %2 = icmp slt i32 %1, 4
  br i1 %2, label %then, label %merge

then:                                             ; preds = %entry
  store i32 4, ptr %cap, align 4
  br label %merge

merge:                                            ; preds = %then, %entry
  %3 = load ptr, ptr %self1, align 8
  %4 = getelementptr inbounds nuw %List_int, ptr %3, i32 0, i32 0
  %5 = load i32, ptr %cap, align 4
  %6 = sext i32 %5 to i64
  %7 = call ptr @alloc_int(i64 %6)
  store ptr %7, ptr %4, align 8
  %8 = load ptr, ptr %self1, align 8
  %9 = getelementptr inbounds nuw %List_int, ptr %8, i32 0, i32 1
  store i32 0, ptr %9, align 4
  %10 = load ptr, ptr %self1, align 8
  %11 = getelementptr inbounds nuw %List_int, ptr %10, i32 0, i32 2
  %12 = load i32, ptr %cap, align 4
  store i32 %12, ptr %11, align 4
  ret void
}

define ptr @alloc_int(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 4
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define void @List_push_int(ptr %self, i32 %item) {
entry:
  %i = alloca i32, align 4
  %new_data = alloca ptr, align 8
  %new_cap = alloca i32, align 4
  %item2 = alloca i32, align 4
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i32 %item, ptr %item2, align 4
  %0 = load ptr, ptr %self1, align 8
  %size = getelementptr inbounds nuw %List_int, ptr %0, i32 0, i32 1
  %size3 = load i32, ptr %size, align 4
  %1 = load ptr, ptr %self1, align 8
  %cap = getelementptr inbounds nuw %List_int, ptr %1, i32 0, i32 2
  %cap4 = load i32, ptr %cap, align 4
  %2 = icmp sge i32 %size3, %cap4
  br i1 %2, label %then, label %merge

then:                                             ; preds = %entry
  %3 = load ptr, ptr %self1, align 8
  %cap5 = getelementptr inbounds nuw %List_int, ptr %3, i32 0, i32 2
  %cap6 = load i32, ptr %cap5, align 4
  %4 = mul i32 %cap6, 2
  store i32 %4, ptr %new_cap, align 4
  %5 = load i32, ptr %new_cap, align 4
  %6 = icmp slt i32 %5, 4
  br i1 %6, label %then7, label %merge8

merge:                                            ; preds = %while_exit, %entry
  %7 = load ptr, ptr %self1, align 8
  %size14 = getelementptr inbounds nuw %List_int, ptr %7, i32 0, i32 1
  %size15 = load i32, ptr %size14, align 4
  %8 = load ptr, ptr %self1, align 8
  %data16 = getelementptr inbounds nuw %List_int, ptr %8, i32 0, i32 0
  %data17 = load ptr, ptr %data16, align 8
  %9 = getelementptr i32, ptr %data17, i32 %size15
  %10 = load i32, ptr %item2, align 4
  store i32 %10, ptr %9, align 4
  %11 = load ptr, ptr %self1, align 8
  %12 = getelementptr inbounds nuw %List_int, ptr %11, i32 0, i32 1
  %13 = load ptr, ptr %self1, align 8
  %size18 = getelementptr inbounds nuw %List_int, ptr %13, i32 0, i32 1
  %size19 = load i32, ptr %size18, align 4
  %14 = add i32 %size19, 1
  store i32 %14, ptr %12, align 4
  ret void

then7:                                            ; preds = %then
  store i32 4, ptr %new_cap, align 4
  br label %merge8

merge8:                                           ; preds = %then7, %then
  %15 = load i32, ptr %new_cap, align 4
  %16 = sext i32 %15 to i64
  %17 = call ptr @alloc_int(i64 %16)
  store ptr %17, ptr %new_data, align 8
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %while_body, %merge8
  %18 = load i32, ptr %i, align 4
  %19 = load ptr, ptr %self1, align 8
  %size9 = getelementptr inbounds nuw %List_int, ptr %19, i32 0, i32 1
  %size10 = load i32, ptr %size9, align 4
  %20 = icmp slt i32 %18, %size10
  br i1 %20, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %21 = load i32, ptr %i, align 4
  %22 = load ptr, ptr %new_data, align 8
  %23 = getelementptr i32, ptr %22, i32 %21
  %24 = load i32, ptr %i, align 4
  %25 = load ptr, ptr %self1, align 8
  %data = getelementptr inbounds nuw %List_int, ptr %25, i32 0, i32 0
  %data11 = load ptr, ptr %data, align 8
  %26 = getelementptr i32, ptr %data11, i32 %24
  %27 = load i32, ptr %26, align 4
  store i32 %27, ptr %23, align 4
  %28 = load i32, ptr %i, align 4
  %29 = add i32 %28, 1
  store i32 %29, ptr %i, align 4
  br label %while

while_exit:                                       ; preds = %while
  %30 = load ptr, ptr %self1, align 8
  %data12 = getelementptr inbounds nuw %List_int, ptr %30, i32 0, i32 0
  %data13 = load ptr, ptr %data12, align 8
  call void @free(ptr %data13)
  %31 = load ptr, ptr %self1, align 8
  %32 = getelementptr inbounds nuw %List_int, ptr %31, i32 0, i32 0
  %33 = load ptr, ptr %new_data, align 8
  store ptr %33, ptr %32, align 8
  %34 = load ptr, ptr %self1, align 8
  %35 = getelementptr inbounds nuw %List_int, ptr %34, i32 0, i32 2
  %36 = load i32, ptr %new_cap, align 4
  store i32 %36, ptr %35, align 4
  br label %merge
}

define i32 @List_len_int(ptr %self) {
entry:
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  %0 = load ptr, ptr %self1, align 8
  %size = getelementptr inbounds nuw %List_int, ptr %0, i32 0, i32 1
  %size2 = load i32, ptr %size, align 4
  ret i32 %size2
}

define i32 @List_get_int(ptr %self, i32 %i) {
entry:
  %i2 = alloca i32, align 4
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i32 %i, ptr %i2, align 4
  %0 = load i32, ptr %i2, align 4
  %1 = load ptr, ptr %self1, align 8
  %data = getelementptr inbounds nuw %List_int, ptr %1, i32 0, i32 0
  %data3 = load ptr, ptr %data, align 8
  %2 = getelementptr i32, ptr %data3, i32 %0
  %3 = load i32, ptr %2, align 4
  ret i32 %3
}

define void @List_free_int(ptr %self) {
entry:
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  %0 = load ptr, ptr %self1, align 8
  %data = getelementptr inbounds nuw %List_int, ptr %0, i32 0, i32 0
  %data2 = load ptr, ptr %data, align 8
  call void @free(ptr %data2)
  %1 = load ptr, ptr %self1, align 8
  %2 = getelementptr inbounds nuw %List_int, ptr %1, i32 0, i32 1
  store i32 0, ptr %2, align 4
  %3 = load ptr, ptr %self1, align 8
  %4 = getelementptr inbounds nuw %List_int, ptr %3, i32 0, i32 2
  store i32 0, ptr %4, align 4
  ret void
}

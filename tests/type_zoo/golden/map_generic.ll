
%Pt = type { i32, i32 }
%HashMap_Pt_int = type { ptr, ptr, ptr, i32, i32, { ptr, ptr }, { ptr, ptr } }
%HashMap_int_int = type { ptr, ptr, ptr, i32, i32, { ptr, ptr }, { ptr, ptr } }

@0 = private unnamed_addr constant [7 x i8] c"%d %d\0A\00", align 1
@1 = private unnamed_addr constant [7 x i8] c"%d %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @memcpy(ptr, ptr, i32)

declare ptr @memset(ptr, i32, i32)

declare ptr @memmove(ptr, ptr, i32)

declare i32 @memcmp(ptr, ptr, i32)

declare i32 @strlen(ptr)

declare ptr @memchr(ptr, i32, i32)

declare i32 @strlen.1(ptr)

declare i32 @printf(ptr, ...)

define i32 @map_hash(ptr %s) {
entry:
  %i = alloca i32, align 4
  %h = alloca i32, align 4
  %s1 = alloca ptr, align 8
  store ptr %s, ptr %s1, align 8
  store i32 5381, ptr %h, align 4
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %while_body, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load ptr, ptr %s1, align 8
  %2 = getelementptr i8, ptr %1, i32 %0
  %3 = load i8, ptr %2, align 1
  %4 = sext i8 %3 to i32
  %5 = icmp ne i32 %4, 0
  br i1 %5, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %6 = load i32, ptr %h, align 4
  %7 = mul i32 %6, 33
  %8 = load i32, ptr %i, align 4
  %9 = load ptr, ptr %s1, align 8
  %10 = getelementptr i8, ptr %9, i32 %8
  %11 = load i8, ptr %10, align 1
  %12 = sext i8 %11 to i32
  %13 = and i32 %12, 255
  %14 = add i32 %7, %13
  store i32 %14, ptr %h, align 4
  %15 = load i32, ptr %i, align 4
  %16 = add i32 %15, 1
  store i32 %16, ptr %i, align 4
  br label %while

while_exit:                                       ; preds = %while
  %17 = load i32, ptr %h, align 4
  %18 = icmp slt i32 %17, 0
  br i1 %18, label %then, label %merge

then:                                             ; preds = %while_exit
  %19 = load i32, ptr %h, align 4
  %20 = sub i32 0, %19
  store i32 %20, ptr %h, align 4
  br label %merge

merge:                                            ; preds = %then, %while_exit
  %21 = load i32, ptr %h, align 4
  ret i32 %21
}

define i32 @map_streq(ptr %a, ptr %b) {
entry:
  %i = alloca i32, align 4
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load ptr, ptr %a1, align 8
  %2 = getelementptr i8, ptr %1, i32 %0
  %3 = load i8, ptr %2, align 1
  %4 = zext i8 %3 to i32
  %5 = icmp ne i32 %4, 0
  %6 = load i32, ptr %i, align 4
  %7 = load ptr, ptr %b2, align 8
  %8 = getelementptr i8, ptr %7, i32 %6
  %9 = load i8, ptr %8, align 1
  %10 = sext i8 %9 to i32
  %11 = icmp ne i32 %10, 0
  %12 = select i1 %5, i1 %11, i1 false
  br i1 %12, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %13 = load i32, ptr %i, align 4
  %14 = load ptr, ptr %a1, align 8
  %15 = getelementptr i8, ptr %14, i32 %13
  %16 = load i8, ptr %15, align 1
  %17 = load i32, ptr %i, align 4
  %18 = load ptr, ptr %b2, align 8
  %19 = getelementptr i8, ptr %18, i32 %17
  %20 = load i8, ptr %19, align 1
  %21 = icmp ne i8 %16, %20
  br i1 %21, label %then, label %merge

while_exit:                                       ; preds = %while
  %22 = load i32, ptr %i, align 4
  %23 = load ptr, ptr %a1, align 8
  %24 = getelementptr i8, ptr %23, i32 %22
  %25 = load i8, ptr %24, align 1
  %26 = load i32, ptr %i, align 4
  %27 = load ptr, ptr %b2, align 8
  %28 = getelementptr i8, ptr %27, i32 %26
  %29 = load i8, ptr %28, align 1
  %30 = icmp ne i8 %25, %29
  br i1 %30, label %then3, label %merge4

then:                                             ; preds = %while_body
  ret i32 0

merge:                                            ; preds = %while_body
  %31 = load i32, ptr %i, align 4
  %32 = add i32 %31, 1
  store i32 %32, ptr %i, align 4
  br label %while

then3:                                            ; preds = %while_exit
  ret i32 0

merge4:                                           ; preds = %while_exit
  ret i32 1
}

define ptr @map_dup(ptr %s) {
entry:
  %i = alloca i32, align 4
  %d = alloca ptr, align 8
  %n = alloca i32, align 4
  %s1 = alloca ptr, align 8
  store ptr %s, ptr %s1, align 8
  %0 = load ptr, ptr %s1, align 8
  %1 = call i32 @strlen(ptr %0)
  store i32 %1, ptr %n, align 4
  %2 = load i32, ptr %n, align 4
  %3 = add i32 %2, 1
  %4 = sext i32 %3 to i64
  %5 = call ptr @alloc_char(i64 %4)
  store ptr %5, ptr %d, align 8
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %while_body, %entry
  %6 = load i32, ptr %i, align 4
  %7 = load i32, ptr %n, align 4
  %8 = icmp slt i32 %6, %7
  br i1 %8, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %9 = load i32, ptr %i, align 4
  %10 = load ptr, ptr %d, align 8
  %11 = getelementptr i8, ptr %10, i32 %9
  %12 = load i32, ptr %i, align 4
  %13 = load ptr, ptr %s1, align 8
  %14 = getelementptr i8, ptr %13, i32 %12
  %15 = load i8, ptr %14, align 1
  store i8 %15, ptr %11, align 1
  %16 = load i32, ptr %i, align 4
  %17 = add i32 %16, 1
  store i32 %17, ptr %i, align 4
  br label %while

while_exit:                                       ; preds = %while
  %18 = load i32, ptr %n, align 4
  %19 = load ptr, ptr %d, align 8
  %20 = getelementptr i8, ptr %19, i32 %18
  store i8 0, ptr %20, align 1
  %21 = load ptr, ptr %d, align 8
  ret ptr %21
}

define i64 @int_hash(ptr %k) {
entry:
  %k1 = alloca ptr, align 8
  store ptr %k, ptr %k1, align 8
  %0 = load ptr, ptr %k1, align 8
  %1 = getelementptr i32, ptr %0, i32 0
  %2 = load i32, ptr %1, align 4
  %3 = sext i32 %2 to i64
  %4 = mul i64 %3, -1640531535
  ret i64 %4
}

define i32 @int_eq(ptr %a, ptr %b) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = load ptr, ptr %a1, align 8
  %1 = getelementptr i32, ptr %0, i32 0
  %2 = load i32, ptr %1, align 4
  %3 = load ptr, ptr %b2, align 8
  %4 = getelementptr i32, ptr %3, i32 0
  %5 = load i32, ptr %4, align 4
  %6 = icmp eq i32 %2, %5
  br i1 %6, label %then, label %merge

then:                                             ; preds = %entry
  ret i32 1

merge:                                            ; preds = %entry
  ret i32 0
}

define i64 @pt_hash(ptr %p) {
entry:
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  %0 = load ptr, ptr %p1, align 8
  %x = getelementptr inbounds nuw %Pt, ptr %0, i32 0, i32 0
  %x2 = load i32, ptr %x, align 4
  %1 = mul i32 %x2, 31
  %2 = load ptr, ptr %p1, align 8
  %y = getelementptr inbounds nuw %Pt, ptr %2, i32 0, i32 1
  %y3 = load i32, ptr %y, align 4
  %3 = add i32 %1, %y3
  %4 = sext i32 %3 to i64
  ret i64 %4
}

define i32 @pt_eq(ptr %a, ptr %b) {
entry:
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = load ptr, ptr %a1, align 8
  %x = getelementptr inbounds nuw %Pt, ptr %0, i32 0, i32 0
  %x3 = load i32, ptr %x, align 4
  %1 = load ptr, ptr %b2, align 8
  %x4 = getelementptr inbounds nuw %Pt, ptr %1, i32 0, i32 0
  %x5 = load i32, ptr %x4, align 4
  %2 = icmp eq i32 %x3, %x5
  %3 = load ptr, ptr %a1, align 8
  %y = getelementptr inbounds nuw %Pt, ptr %3, i32 0, i32 1
  %y6 = load i32, ptr %y, align 4
  %4 = load ptr, ptr %b2, align 8
  %y7 = getelementptr inbounds nuw %Pt, ptr %4, i32 0, i32 1
  %y8 = load i32, ptr %y7, align 4
  %5 = icmp eq i32 %y6, %y8
  %6 = select i1 %2, i1 %5, i1 false
  br i1 %6, label %then, label %merge

then:                                             ; preds = %entry
  ret i32 1

merge:                                            ; preds = %entry
  ret i32 0
}

define i32 @main() {
entry:
  %got = alloca i32, align 4
  %q = alloca %Pt, align 8
  %s2 = alloca ptr, align 8
  %s1 = alloca ptr, align 8
  %c8 = alloca i32, align 4
  %p2 = alloca %Pt, align 8
  %p1 = alloca %Pt, align 8
  %fnptr.fat6 = alloca { ptr, ptr }, align 8
  %fnptr.fat4 = alloca { ptr, ptr }, align 8
  %pm = alloca %HashMap_Pt_int, align 8
  %v = alloca i32, align 4
  %s = alloca ptr, align 8
  %c = alloca i32, align 4
  %k = alloca i32, align 4
  %i = alloca i32, align 4
  %fnptr.fat1 = alloca { ptr, ptr }, align 8
  %fnptr.fat = alloca { ptr, ptr }, align 8
  %m = alloca %HashMap_int_int, align 8
  %0 = load %HashMap_int_int, ptr %m, align 8
  %1 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat, i32 0, i32 0
  store ptr @__fnptr_int_hash, ptr %1, align 8
  %2 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat, i32 0, i32 1
  store ptr null, ptr %2, align 8
  %fnptr.fat.val = load { ptr, ptr }, ptr %fnptr.fat, align 8
  %3 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat1, i32 0, i32 0
  store ptr @__fnptr_int_eq, ptr %3, align 8
  %4 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat1, i32 0, i32 1
  store ptr null, ptr %4, align 8
  %fnptr.fat.val2 = load { ptr, ptr }, ptr %fnptr.fat1, align 8
  call void @HashMap_init_int_int(ptr %m, i32 8, { ptr, ptr } %fnptr.fat.val, { ptr, ptr } %fnptr.fat.val2)
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %5 = load i32, ptr %i, align 4
  %6 = icmp slt i32 %5, 50
  br i1 %6, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %7 = load i32, ptr %i, align 4
  %8 = srem i32 %7, 10
  store i32 %8, ptr %k, align 4
  store i32 0, ptr %c, align 4
  %9 = load %HashMap_int_int, ptr %m, align 8
  %10 = load i32, ptr %k, align 4
  %11 = load i32, ptr %c, align 4
  %12 = call ptr @HashMap_at_int_int(ptr %m, i32 %10, ptr %c)
  store ptr %12, ptr %s, align 8
  %13 = load i32, ptr %c, align 4
  %14 = icmp eq i32 %13, 1
  br i1 %14, label %then, label %merge

while_exit:                                       ; preds = %while
  store i32 0, ptr %v, align 4
  %15 = load %HashMap_int_int, ptr %m, align 8
  %16 = load i32, ptr %v, align 4
  %17 = call i32 @HashMap_get_int_int(ptr %m, i32 3, ptr %v)
  %18 = load i32, ptr %v, align 4
  %count = getelementptr inbounds nuw %HashMap_int_int, ptr %m, i32 0, i32 4
  %count3 = load i32, ptr %count, align 4
  %19 = call i32 (ptr, ...) @printf(ptr @0, i32 %18, i32 %count3)
  %20 = load %HashMap_Pt_int, ptr %pm, align 8
  %21 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat4, i32 0, i32 0
  store ptr @__fnptr_pt_hash, ptr %21, align 8
  %22 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat4, i32 0, i32 1
  store ptr null, ptr %22, align 8
  %fnptr.fat.val5 = load { ptr, ptr }, ptr %fnptr.fat4, align 8
  %23 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat6, i32 0, i32 0
  store ptr @__fnptr_pt_eq, ptr %23, align 8
  %24 = getelementptr inbounds nuw { ptr, ptr }, ptr %fnptr.fat6, i32 0, i32 1
  store ptr null, ptr %24, align 8
  %fnptr.fat.val7 = load { ptr, ptr }, ptr %fnptr.fat6, align 8
  call void @HashMap_init_Pt_int(ptr %pm, i32 8, { ptr, ptr } %fnptr.fat.val5, { ptr, ptr } %fnptr.fat.val7)
  %25 = getelementptr inbounds nuw %Pt, ptr %p1, i32 0, i32 0
  store i32 1, ptr %25, align 4
  %26 = getelementptr inbounds nuw %Pt, ptr %p1, i32 0, i32 1
  store i32 2, ptr %26, align 4
  %27 = getelementptr inbounds nuw %Pt, ptr %p2, i32 0, i32 0
  store i32 3, ptr %27, align 4
  %28 = getelementptr inbounds nuw %Pt, ptr %p2, i32 0, i32 1
  store i32 4, ptr %28, align 4
  store i32 0, ptr %c8, align 4
  %29 = load %HashMap_Pt_int, ptr %pm, align 8
  %30 = load %Pt, ptr %p1, align 4
  %31 = load i32, ptr %c8, align 4
  %32 = call ptr @HashMap_at_Pt_int(ptr %pm, %Pt %30, ptr %c8)
  store ptr %32, ptr %s1, align 8
  %33 = load ptr, ptr %s1, align 8
  %34 = getelementptr i32, ptr %33, i32 0
  store i32 100, ptr %34, align 4
  %35 = load %HashMap_Pt_int, ptr %pm, align 8
  %36 = load %Pt, ptr %p2, align 4
  %37 = load i32, ptr %c8, align 4
  %38 = call ptr @HashMap_at_Pt_int(ptr %pm, %Pt %36, ptr %c8)
  store ptr %38, ptr %s2, align 8
  %39 = load ptr, ptr %s2, align 8
  %40 = getelementptr i32, ptr %39, i32 0
  store i32 200, ptr %40, align 4
  %41 = getelementptr inbounds nuw %Pt, ptr %q, i32 0, i32 0
  store i32 1, ptr %41, align 4
  %42 = getelementptr inbounds nuw %Pt, ptr %q, i32 0, i32 1
  store i32 2, ptr %42, align 4
  store i32 0, ptr %got, align 4
  %43 = load %HashMap_Pt_int, ptr %pm, align 8
  %44 = load %Pt, ptr %q, align 4
  %45 = load i32, ptr %got, align 4
  %46 = call i32 @HashMap_get_Pt_int(ptr %pm, %Pt %44, ptr %got)
  %47 = load i32, ptr %got, align 4
  %count9 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %pm, i32 0, i32 4
  %count10 = load i32, ptr %count9, align 4
  %48 = call i32 (ptr, ...) @printf(ptr @1, i32 %47, i32 %count10)
  %49 = load %HashMap_int_int, ptr %m, align 8
  call void @HashMap_free_int_int(ptr %m)
  %50 = load %HashMap_Pt_int, ptr %pm, align 8
  call void @HashMap_free_Pt_int(ptr %pm)
  ret i32 0

then:                                             ; preds = %while_body
  %51 = load ptr, ptr %s, align 8
  %52 = getelementptr i32, ptr %51, i32 0
  store i32 0, ptr %52, align 4
  br label %merge

merge:                                            ; preds = %then, %while_body
  %53 = load ptr, ptr %s, align 8
  %54 = getelementptr i32, ptr %53, i32 0
  %55 = load ptr, ptr %s, align 8
  %56 = getelementptr i32, ptr %55, i32 0
  %57 = load i32, ptr %56, align 4
  %58 = add i32 %57, 1
  store i32 %58, ptr %54, align 4
  %59 = load i32, ptr %i, align 4
  %60 = add i32 %59, 1
  store i32 %60, ptr %i, align 4
  br label %while
}

define ptr @alloc_char(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 1
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define void @HashMap_init_int_int(ptr %m, i32 %cap, { ptr, ptr } %hash, { ptr, ptr } %eq) {
entry:
  %eq4 = alloca { ptr, ptr }, align 8
  %hash3 = alloca { ptr, ptr }, align 8
  %cap2 = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store i32 %cap, ptr %cap2, align 4
  store { ptr, ptr } %hash, ptr %hash3, align 8
  store { ptr, ptr } %eq, ptr %eq4, align 8
  %0 = load i32, ptr %cap2, align 4
  %1 = icmp slt i32 %0, 8
  br i1 %1, label %then, label %merge

then:                                             ; preds = %entry
  store i32 8, ptr %cap2, align 4
  br label %merge

merge:                                            ; preds = %then, %entry
  %2 = load ptr, ptr %m1, align 8
  %3 = getelementptr inbounds nuw %HashMap_int_int, ptr %2, i32 0, i32 0
  %4 = load i32, ptr %cap2, align 4
  %5 = sext i32 %4 to i64
  %6 = call ptr @alloc_int(i64 %5)
  store ptr %6, ptr %3, align 8
  %7 = load ptr, ptr %m1, align 8
  %8 = getelementptr inbounds nuw %HashMap_int_int, ptr %7, i32 0, i32 1
  %9 = load i32, ptr %cap2, align 4
  %10 = sext i32 %9 to i64
  %11 = call ptr @alloc_int(i64 %10)
  store ptr %11, ptr %8, align 8
  %12 = load ptr, ptr %m1, align 8
  %13 = getelementptr inbounds nuw %HashMap_int_int, ptr %12, i32 0, i32 2
  %14 = load i32, ptr %cap2, align 4
  %15 = sext i32 %14 to i64
  %16 = call ptr @alloc_uint8(i64 %15)
  store ptr %16, ptr %13, align 8
  %17 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_int_int, ptr %17, i32 0, i32 2
  %used5 = load ptr, ptr %used, align 8
  %18 = load i32, ptr %cap2, align 4
  %19 = call ptr @memset(ptr %used5, i32 0, i32 %18)
  %20 = load ptr, ptr %m1, align 8
  %21 = getelementptr inbounds nuw %HashMap_int_int, ptr %20, i32 0, i32 3
  %22 = load i32, ptr %cap2, align 4
  store i32 %22, ptr %21, align 4
  %23 = load ptr, ptr %m1, align 8
  %24 = getelementptr inbounds nuw %HashMap_int_int, ptr %23, i32 0, i32 4
  store i32 0, ptr %24, align 4
  %25 = load ptr, ptr %m1, align 8
  %26 = getelementptr inbounds nuw %HashMap_int_int, ptr %25, i32 0, i32 5
  %27 = load { ptr, ptr }, ptr %hash3, align 8
  store { ptr, ptr } %27, ptr %26, align 8
  %28 = load ptr, ptr %m1, align 8
  %29 = getelementptr inbounds nuw %HashMap_int_int, ptr %28, i32 0, i32 6
  %30 = load { ptr, ptr }, ptr %eq4, align 8
  store { ptr, ptr } %30, ptr %29, align 8
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

define ptr @alloc_uint8(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 1
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define internal i64 @__fnptr_int_hash(ptr %0, ptr %1) {
entry:
  %2 = call i64 @int_hash(ptr %1)
  ret i64 %2
}

define internal i32 @__fnptr_int_eq(ptr %0, ptr %1, ptr %2) {
entry:
  %3 = call i32 @int_eq(ptr %1, ptr %2)
  ret i32 %3
}

define ptr @HashMap_at_int_int(ptr %m, i32 %key, ptr %created) {
entry:
  %i = alloca i32, align 4
  %h = alloca i64, align 8
  %eqfn = alloca { ptr, ptr }, align 8
  %hashfn = alloca { ptr, ptr }, align 8
  %created3 = alloca ptr, align 8
  %key2 = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store i32 %key, ptr %key2, align 4
  store ptr %created, ptr %created3, align 8
  %0 = load ptr, ptr %m1, align 8
  %count = getelementptr inbounds nuw %HashMap_int_int, ptr %0, i32 0, i32 4
  %count4 = load i32, ptr %count, align 4
  %1 = add i32 %count4, 1
  %2 = mul i32 %1, 4
  %3 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_int_int, ptr %3, i32 0, i32 3
  %cap5 = load i32, ptr %cap, align 4
  %4 = mul i32 %cap5, 3
  %5 = icmp sge i32 %2, %4
  br i1 %5, label %then, label %merge

then:                                             ; preds = %entry
  %6 = load ptr, ptr %m1, align 8
  call void @HashMap_grow_int_int(ptr %6)
  br label %merge

merge:                                            ; preds = %then, %entry
  %7 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_int_int, ptr %7, i32 0, i32 5
  %hash6 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash6, ptr %hashfn, align 8
  %8 = load ptr, ptr %m1, align 8
  %eq = getelementptr inbounds nuw %HashMap_int_int, ptr %8, i32 0, i32 6
  %eq7 = load { ptr, ptr }, ptr %eq, align 8
  store { ptr, ptr } %eq7, ptr %eqfn, align 8
  %9 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %9, 0
  %env.ptr = extractvalue { ptr, ptr } %9, 1
  %10 = load i32, ptr %key2, align 4
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %key2)
  store i64 %fn.call, ptr %h, align 8
  %11 = load i64, ptr %h, align 8
  %12 = load ptr, ptr %m1, align 8
  %cap8 = getelementptr inbounds nuw %HashMap_int_int, ptr %12, i32 0, i32 3
  %cap9 = load i32, ptr %cap8, align 4
  %13 = sext i32 %cap9 to i64
  %14 = urem i64 %11, %13
  %15 = trunc i64 %14 to i32
  store i32 %15, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge18, %merge
  %16 = load i32, ptr %i, align 4
  %17 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_int_int, ptr %17, i32 0, i32 2
  %used10 = load ptr, ptr %used, align 8
  %18 = getelementptr i8, ptr %used10, i32 %16
  %19 = load i8, ptr %18, align 1
  %20 = zext i8 %19 to i32
  %21 = icmp eq i32 %20, 1
  br i1 %21, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %22 = load { ptr, ptr }, ptr %eqfn, align 8
  %fn.ptr11 = extractvalue { ptr, ptr } %22, 0
  %env.ptr12 = extractvalue { ptr, ptr } %22, 1
  %23 = load i32, ptr %key2, align 4
  %24 = load i32, ptr %i, align 4
  %25 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_int_int, ptr %25, i32 0, i32 0
  %keys13 = load ptr, ptr %keys, align 8
  %26 = getelementptr i32, ptr %keys13, i32 %24
  %27 = load i32, ptr %26, align 4
  %28 = load i32, ptr %i, align 4
  %29 = load ptr, ptr %m1, align 8
  %keys14 = getelementptr inbounds nuw %HashMap_int_int, ptr %29, i32 0, i32 0
  %keys15 = load ptr, ptr %keys14, align 8
  %30 = getelementptr i32, ptr %keys15, i32 %28
  %fn.call16 = call i32 %fn.ptr11(ptr %env.ptr12, ptr %key2, ptr %30)
  %31 = icmp eq i32 %fn.call16, 1
  br i1 %31, label %then17, label %merge18

while_exit:                                       ; preds = %while
  %32 = load i32, ptr %i, align 4
  %33 = load ptr, ptr %m1, align 8
  %used24 = getelementptr inbounds nuw %HashMap_int_int, ptr %33, i32 0, i32 2
  %used25 = load ptr, ptr %used24, align 8
  %34 = getelementptr i8, ptr %used25, i32 %32
  store i8 1, ptr %34, align 1
  %35 = load i32, ptr %i, align 4
  %36 = load ptr, ptr %m1, align 8
  %keys26 = getelementptr inbounds nuw %HashMap_int_int, ptr %36, i32 0, i32 0
  %keys27 = load ptr, ptr %keys26, align 8
  %37 = getelementptr i32, ptr %keys27, i32 %35
  %38 = load i32, ptr %key2, align 4
  store i32 %38, ptr %37, align 4
  %39 = load ptr, ptr %m1, align 8
  %40 = getelementptr inbounds nuw %HashMap_int_int, ptr %39, i32 0, i32 4
  %41 = load ptr, ptr %m1, align 8
  %count28 = getelementptr inbounds nuw %HashMap_int_int, ptr %41, i32 0, i32 4
  %count29 = load i32, ptr %count28, align 4
  %42 = add i32 %count29, 1
  store i32 %42, ptr %40, align 4
  %43 = load ptr, ptr %created3, align 8
  %44 = getelementptr i32, ptr %43, i32 0
  store i32 1, ptr %44, align 4
  %45 = load i32, ptr %i, align 4
  %46 = load ptr, ptr %m1, align 8
  %vals30 = getelementptr inbounds nuw %HashMap_int_int, ptr %46, i32 0, i32 1
  %vals31 = load ptr, ptr %vals30, align 8
  %47 = getelementptr i32, ptr %vals31, i32 %45
  %48 = load i32, ptr %47, align 4
  %49 = load i32, ptr %i, align 4
  %50 = load ptr, ptr %m1, align 8
  %vals32 = getelementptr inbounds nuw %HashMap_int_int, ptr %50, i32 0, i32 1
  %vals33 = load ptr, ptr %vals32, align 8
  %51 = getelementptr i32, ptr %vals33, i32 %49
  ret ptr %51

then17:                                           ; preds = %while_body
  %52 = load ptr, ptr %created3, align 8
  %53 = getelementptr i32, ptr %52, i32 0
  store i32 0, ptr %53, align 4
  %54 = load i32, ptr %i, align 4
  %55 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_int_int, ptr %55, i32 0, i32 1
  %vals19 = load ptr, ptr %vals, align 8
  %56 = getelementptr i32, ptr %vals19, i32 %54
  %57 = load i32, ptr %56, align 4
  %58 = load i32, ptr %i, align 4
  %59 = load ptr, ptr %m1, align 8
  %vals20 = getelementptr inbounds nuw %HashMap_int_int, ptr %59, i32 0, i32 1
  %vals21 = load ptr, ptr %vals20, align 8
  %60 = getelementptr i32, ptr %vals21, i32 %58
  ret ptr %60

merge18:                                          ; preds = %while_body
  %61 = load i32, ptr %i, align 4
  %62 = add i32 %61, 1
  %63 = load ptr, ptr %m1, align 8
  %cap22 = getelementptr inbounds nuw %HashMap_int_int, ptr %63, i32 0, i32 3
  %cap23 = load i32, ptr %cap22, align 4
  %64 = srem i32 %62, %cap23
  store i32 %64, ptr %i, align 4
  br label %while
}

define void @HashMap_grow_int_int(ptr %m) {
entry:
  %j = alloca i32, align 4
  %h = alloca i64, align 8
  %i = alloca i32, align 4
  %hashfn = alloca { ptr, ptr }, align 8
  %nu = alloca ptr, align 8
  %nv = alloca ptr, align 8
  %nk = alloca ptr, align 8
  %newcap = alloca i32, align 4
  %ou = alloca ptr, align 8
  %ov = alloca ptr, align 8
  %ok = alloca ptr, align 8
  %oldcap = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  %0 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_int_int, ptr %0, i32 0, i32 3
  %cap2 = load i32, ptr %cap, align 4
  store i32 %cap2, ptr %oldcap, align 4
  %1 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_int_int, ptr %1, i32 0, i32 0
  %keys3 = load ptr, ptr %keys, align 8
  store ptr %keys3, ptr %ok, align 8
  %2 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_int_int, ptr %2, i32 0, i32 1
  %vals4 = load ptr, ptr %vals, align 8
  store ptr %vals4, ptr %ov, align 8
  %3 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_int_int, ptr %3, i32 0, i32 2
  %used5 = load ptr, ptr %used, align 8
  store ptr %used5, ptr %ou, align 8
  %4 = load i32, ptr %oldcap, align 4
  %5 = mul i32 %4, 2
  store i32 %5, ptr %newcap, align 4
  %6 = load i32, ptr %newcap, align 4
  %7 = sext i32 %6 to i64
  %8 = call ptr @alloc_int(i64 %7)
  store ptr %8, ptr %nk, align 8
  %9 = load i32, ptr %newcap, align 4
  %10 = sext i32 %9 to i64
  %11 = call ptr @alloc_int(i64 %10)
  store ptr %11, ptr %nv, align 8
  %12 = load i32, ptr %newcap, align 4
  %13 = sext i32 %12 to i64
  %14 = call ptr @alloc_uint8(i64 %13)
  store ptr %14, ptr %nu, align 8
  %15 = load ptr, ptr %nu, align 8
  %16 = load i32, ptr %newcap, align 4
  %17 = call ptr @memset(ptr %15, i32 0, i32 %16)
  %18 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_int_int, ptr %18, i32 0, i32 5
  %hash6 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash6, ptr %hashfn, align 8
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %19 = load i32, ptr %i, align 4
  %20 = load i32, ptr %oldcap, align 4
  %21 = icmp slt i32 %19, %20
  br i1 %21, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %22 = load i32, ptr %i, align 4
  %23 = load ptr, ptr %ou, align 8
  %24 = getelementptr i8, ptr %23, i32 %22
  %25 = load i8, ptr %24, align 1
  %26 = zext i8 %25 to i32
  %27 = icmp eq i32 %26, 1
  br i1 %27, label %then, label %merge

while_exit:                                       ; preds = %while
  %28 = load ptr, ptr %ok, align 8
  call void @free(ptr %28)
  %29 = load ptr, ptr %ov, align 8
  call void @free(ptr %29)
  %30 = load ptr, ptr %ou, align 8
  call void @free(ptr %30)
  %31 = load ptr, ptr %m1, align 8
  %32 = getelementptr inbounds nuw %HashMap_int_int, ptr %31, i32 0, i32 0
  %33 = load ptr, ptr %nk, align 8
  store ptr %33, ptr %32, align 8
  %34 = load ptr, ptr %m1, align 8
  %35 = getelementptr inbounds nuw %HashMap_int_int, ptr %34, i32 0, i32 1
  %36 = load ptr, ptr %nv, align 8
  store ptr %36, ptr %35, align 8
  %37 = load ptr, ptr %m1, align 8
  %38 = getelementptr inbounds nuw %HashMap_int_int, ptr %37, i32 0, i32 2
  %39 = load ptr, ptr %nu, align 8
  store ptr %39, ptr %38, align 8
  %40 = load ptr, ptr %m1, align 8
  %41 = getelementptr inbounds nuw %HashMap_int_int, ptr %40, i32 0, i32 3
  %42 = load i32, ptr %newcap, align 4
  store i32 %42, ptr %41, align 4
  ret void

then:                                             ; preds = %while_body
  %43 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %43, 0
  %env.ptr = extractvalue { ptr, ptr } %43, 1
  %44 = load i32, ptr %i, align 4
  %45 = load ptr, ptr %ok, align 8
  %46 = getelementptr i32, ptr %45, i32 %44
  %47 = load i32, ptr %46, align 4
  %48 = load i32, ptr %i, align 4
  %49 = load ptr, ptr %ok, align 8
  %50 = getelementptr i32, ptr %49, i32 %48
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %50)
  store i64 %fn.call, ptr %h, align 8
  %51 = load i64, ptr %h, align 8
  %52 = load i32, ptr %newcap, align 4
  %53 = sext i32 %52 to i64
  %54 = urem i64 %51, %53
  %55 = trunc i64 %54 to i32
  store i32 %55, ptr %j, align 4
  br label %while7

merge:                                            ; preds = %while_exit9, %while_body
  %56 = load i32, ptr %i, align 4
  %57 = add i32 %56, 1
  store i32 %57, ptr %i, align 4
  br label %while

while7:                                           ; preds = %while_body8, %then
  %58 = load i32, ptr %j, align 4
  %59 = load ptr, ptr %nu, align 8
  %60 = getelementptr i8, ptr %59, i32 %58
  %61 = load i8, ptr %60, align 1
  %62 = zext i8 %61 to i32
  %63 = icmp eq i32 %62, 1
  br i1 %63, label %while_body8, label %while_exit9

while_body8:                                      ; preds = %while7
  %64 = load i32, ptr %j, align 4
  %65 = add i32 %64, 1
  %66 = load i32, ptr %newcap, align 4
  %67 = srem i32 %65, %66
  store i32 %67, ptr %j, align 4
  br label %while7

while_exit9:                                      ; preds = %while7
  %68 = load i32, ptr %j, align 4
  %69 = load ptr, ptr %nu, align 8
  %70 = getelementptr i8, ptr %69, i32 %68
  store i8 1, ptr %70, align 1
  %71 = load i32, ptr %j, align 4
  %72 = load ptr, ptr %nk, align 8
  %73 = getelementptr i32, ptr %72, i32 %71
  %74 = load i32, ptr %i, align 4
  %75 = load ptr, ptr %ok, align 8
  %76 = getelementptr i32, ptr %75, i32 %74
  %77 = load i32, ptr %76, align 4
  store i32 %77, ptr %73, align 4
  %78 = load i32, ptr %j, align 4
  %79 = load ptr, ptr %nv, align 8
  %80 = getelementptr i32, ptr %79, i32 %78
  %81 = load i32, ptr %i, align 4
  %82 = load ptr, ptr %ov, align 8
  %83 = getelementptr i32, ptr %82, i32 %81
  %84 = load i32, ptr %83, align 4
  store i32 %84, ptr %80, align 4
  br label %merge
}

define i32 @HashMap_get_int_int(ptr %m, i32 %key, ptr %out) {
entry:
  %n = alloca i32, align 4
  %i = alloca i32, align 4
  %h = alloca i64, align 8
  %eqfn = alloca { ptr, ptr }, align 8
  %hashfn = alloca { ptr, ptr }, align 8
  %out3 = alloca ptr, align 8
  %key2 = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store i32 %key, ptr %key2, align 4
  store ptr %out, ptr %out3, align 8
  %0 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_int_int, ptr %0, i32 0, i32 5
  %hash4 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash4, ptr %hashfn, align 8
  %1 = load ptr, ptr %m1, align 8
  %eq = getelementptr inbounds nuw %HashMap_int_int, ptr %1, i32 0, i32 6
  %eq5 = load { ptr, ptr }, ptr %eq, align 8
  store { ptr, ptr } %eq5, ptr %eqfn, align 8
  %2 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %2, 0
  %env.ptr = extractvalue { ptr, ptr } %2, 1
  %3 = load i32, ptr %key2, align 4
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %key2)
  store i64 %fn.call, ptr %h, align 8
  %4 = load i64, ptr %h, align 8
  %5 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_int_int, ptr %5, i32 0, i32 3
  %cap6 = load i32, ptr %cap, align 4
  %6 = sext i32 %cap6 to i64
  %7 = urem i64 %4, %6
  %8 = trunc i64 %7 to i32
  store i32 %8, ptr %i, align 4
  store i32 0, ptr %n, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %9 = load i32, ptr %i, align 4
  %10 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_int_int, ptr %10, i32 0, i32 2
  %used7 = load ptr, ptr %used, align 8
  %11 = getelementptr i8, ptr %used7, i32 %9
  %12 = load i8, ptr %11, align 1
  %13 = zext i8 %12 to i32
  %14 = icmp eq i32 %13, 1
  %15 = load i32, ptr %n, align 4
  %16 = load ptr, ptr %m1, align 8
  %cap8 = getelementptr inbounds nuw %HashMap_int_int, ptr %16, i32 0, i32 3
  %cap9 = load i32, ptr %cap8, align 4
  %17 = icmp slt i32 %15, %cap9
  %18 = select i1 %14, i1 %17, i1 false
  br i1 %18, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %19 = load { ptr, ptr }, ptr %eqfn, align 8
  %fn.ptr10 = extractvalue { ptr, ptr } %19, 0
  %env.ptr11 = extractvalue { ptr, ptr } %19, 1
  %20 = load i32, ptr %key2, align 4
  %21 = load i32, ptr %i, align 4
  %22 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_int_int, ptr %22, i32 0, i32 0
  %keys12 = load ptr, ptr %keys, align 8
  %23 = getelementptr i32, ptr %keys12, i32 %21
  %24 = load i32, ptr %23, align 4
  %25 = load i32, ptr %i, align 4
  %26 = load ptr, ptr %m1, align 8
  %keys13 = getelementptr inbounds nuw %HashMap_int_int, ptr %26, i32 0, i32 0
  %keys14 = load ptr, ptr %keys13, align 8
  %27 = getelementptr i32, ptr %keys14, i32 %25
  %fn.call15 = call i32 %fn.ptr10(ptr %env.ptr11, ptr %key2, ptr %27)
  %28 = icmp eq i32 %fn.call15, 1
  br i1 %28, label %then, label %merge

while_exit:                                       ; preds = %while
  ret i32 0

then:                                             ; preds = %while_body
  %29 = load ptr, ptr %out3, align 8
  %30 = getelementptr i32, ptr %29, i32 0
  %31 = load i32, ptr %i, align 4
  %32 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_int_int, ptr %32, i32 0, i32 1
  %vals16 = load ptr, ptr %vals, align 8
  %33 = getelementptr i32, ptr %vals16, i32 %31
  %34 = load i32, ptr %33, align 4
  store i32 %34, ptr %30, align 4
  ret i32 1

merge:                                            ; preds = %while_body
  %35 = load i32, ptr %i, align 4
  %36 = add i32 %35, 1
  %37 = load ptr, ptr %m1, align 8
  %cap17 = getelementptr inbounds nuw %HashMap_int_int, ptr %37, i32 0, i32 3
  %cap18 = load i32, ptr %cap17, align 4
  %38 = srem i32 %36, %cap18
  store i32 %38, ptr %i, align 4
  %39 = load i32, ptr %n, align 4
  %40 = add i32 %39, 1
  store i32 %40, ptr %n, align 4
  br label %while
}

define void @HashMap_init_Pt_int(ptr %m, i32 %cap, { ptr, ptr } %hash, { ptr, ptr } %eq) {
entry:
  %eq4 = alloca { ptr, ptr }, align 8
  %hash3 = alloca { ptr, ptr }, align 8
  %cap2 = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store i32 %cap, ptr %cap2, align 4
  store { ptr, ptr } %hash, ptr %hash3, align 8
  store { ptr, ptr } %eq, ptr %eq4, align 8
  %0 = load i32, ptr %cap2, align 4
  %1 = icmp slt i32 %0, 8
  br i1 %1, label %then, label %merge

then:                                             ; preds = %entry
  store i32 8, ptr %cap2, align 4
  br label %merge

merge:                                            ; preds = %then, %entry
  %2 = load ptr, ptr %m1, align 8
  %3 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %2, i32 0, i32 0
  %4 = load i32, ptr %cap2, align 4
  %5 = sext i32 %4 to i64
  %6 = call ptr @alloc_Pt(i64 %5)
  store ptr %6, ptr %3, align 8
  %7 = load ptr, ptr %m1, align 8
  %8 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %7, i32 0, i32 1
  %9 = load i32, ptr %cap2, align 4
  %10 = sext i32 %9 to i64
  %11 = call ptr @alloc_int(i64 %10)
  store ptr %11, ptr %8, align 8
  %12 = load ptr, ptr %m1, align 8
  %13 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %12, i32 0, i32 2
  %14 = load i32, ptr %cap2, align 4
  %15 = sext i32 %14 to i64
  %16 = call ptr @alloc_uint8(i64 %15)
  store ptr %16, ptr %13, align 8
  %17 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_Pt_int, ptr %17, i32 0, i32 2
  %used5 = load ptr, ptr %used, align 8
  %18 = load i32, ptr %cap2, align 4
  %19 = call ptr @memset(ptr %used5, i32 0, i32 %18)
  %20 = load ptr, ptr %m1, align 8
  %21 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %20, i32 0, i32 3
  %22 = load i32, ptr %cap2, align 4
  store i32 %22, ptr %21, align 4
  %23 = load ptr, ptr %m1, align 8
  %24 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %23, i32 0, i32 4
  store i32 0, ptr %24, align 4
  %25 = load ptr, ptr %m1, align 8
  %26 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %25, i32 0, i32 5
  %27 = load { ptr, ptr }, ptr %hash3, align 8
  store { ptr, ptr } %27, ptr %26, align 8
  %28 = load ptr, ptr %m1, align 8
  %29 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %28, i32 0, i32 6
  %30 = load { ptr, ptr }, ptr %eq4, align 8
  store { ptr, ptr } %30, ptr %29, align 8
  ret void
}

define ptr @alloc_Pt(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 8
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define internal i64 @__fnptr_pt_hash(ptr %0, ptr %1) {
entry:
  %2 = call i64 @pt_hash(ptr %1)
  ret i64 %2
}

define internal i32 @__fnptr_pt_eq(ptr %0, ptr %1, ptr %2) {
entry:
  %3 = call i32 @pt_eq(ptr %1, ptr %2)
  ret i32 %3
}

define ptr @HashMap_at_Pt_int(ptr %m, %Pt %key, ptr %created) {
entry:
  %i = alloca i32, align 4
  %h = alloca i64, align 8
  %eqfn = alloca { ptr, ptr }, align 8
  %hashfn = alloca { ptr, ptr }, align 8
  %created3 = alloca ptr, align 8
  %key2 = alloca %Pt, align 8
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store %Pt %key, ptr %key2, align 4
  store ptr %created, ptr %created3, align 8
  %0 = load ptr, ptr %m1, align 8
  %count = getelementptr inbounds nuw %HashMap_Pt_int, ptr %0, i32 0, i32 4
  %count4 = load i32, ptr %count, align 4
  %1 = add i32 %count4, 1
  %2 = mul i32 %1, 4
  %3 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_Pt_int, ptr %3, i32 0, i32 3
  %cap5 = load i32, ptr %cap, align 4
  %4 = mul i32 %cap5, 3
  %5 = icmp sge i32 %2, %4
  br i1 %5, label %then, label %merge

then:                                             ; preds = %entry
  %6 = load ptr, ptr %m1, align 8
  call void @HashMap_grow_Pt_int(ptr %6)
  br label %merge

merge:                                            ; preds = %then, %entry
  %7 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_Pt_int, ptr %7, i32 0, i32 5
  %hash6 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash6, ptr %hashfn, align 8
  %8 = load ptr, ptr %m1, align 8
  %eq = getelementptr inbounds nuw %HashMap_Pt_int, ptr %8, i32 0, i32 6
  %eq7 = load { ptr, ptr }, ptr %eq, align 8
  store { ptr, ptr } %eq7, ptr %eqfn, align 8
  %9 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %9, 0
  %env.ptr = extractvalue { ptr, ptr } %9, 1
  %10 = load %Pt, ptr %key2, align 4
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %key2)
  store i64 %fn.call, ptr %h, align 8
  %11 = load i64, ptr %h, align 8
  %12 = load ptr, ptr %m1, align 8
  %cap8 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %12, i32 0, i32 3
  %cap9 = load i32, ptr %cap8, align 4
  %13 = sext i32 %cap9 to i64
  %14 = urem i64 %11, %13
  %15 = trunc i64 %14 to i32
  store i32 %15, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge18, %merge
  %16 = load i32, ptr %i, align 4
  %17 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_Pt_int, ptr %17, i32 0, i32 2
  %used10 = load ptr, ptr %used, align 8
  %18 = getelementptr i8, ptr %used10, i32 %16
  %19 = load i8, ptr %18, align 1
  %20 = zext i8 %19 to i32
  %21 = icmp eq i32 %20, 1
  br i1 %21, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %22 = load { ptr, ptr }, ptr %eqfn, align 8
  %fn.ptr11 = extractvalue { ptr, ptr } %22, 0
  %env.ptr12 = extractvalue { ptr, ptr } %22, 1
  %23 = load %Pt, ptr %key2, align 4
  %24 = load i32, ptr %i, align 4
  %25 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_Pt_int, ptr %25, i32 0, i32 0
  %keys13 = load ptr, ptr %keys, align 8
  %26 = getelementptr %Pt, ptr %keys13, i32 %24
  %27 = load %Pt, ptr %26, align 4
  %28 = load i32, ptr %i, align 4
  %29 = load ptr, ptr %m1, align 8
  %keys14 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %29, i32 0, i32 0
  %keys15 = load ptr, ptr %keys14, align 8
  %30 = getelementptr %Pt, ptr %keys15, i32 %28
  %fn.call16 = call i32 %fn.ptr11(ptr %env.ptr12, ptr %key2, ptr %30)
  %31 = icmp eq i32 %fn.call16, 1
  br i1 %31, label %then17, label %merge18

while_exit:                                       ; preds = %while
  %32 = load i32, ptr %i, align 4
  %33 = load ptr, ptr %m1, align 8
  %used24 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %33, i32 0, i32 2
  %used25 = load ptr, ptr %used24, align 8
  %34 = getelementptr i8, ptr %used25, i32 %32
  store i8 1, ptr %34, align 1
  %35 = load i32, ptr %i, align 4
  %36 = load ptr, ptr %m1, align 8
  %keys26 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %36, i32 0, i32 0
  %keys27 = load ptr, ptr %keys26, align 8
  %37 = getelementptr %Pt, ptr %keys27, i32 %35
  %38 = load %Pt, ptr %key2, align 4
  store %Pt %38, ptr %37, align 4
  %39 = load ptr, ptr %m1, align 8
  %40 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %39, i32 0, i32 4
  %41 = load ptr, ptr %m1, align 8
  %count28 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %41, i32 0, i32 4
  %count29 = load i32, ptr %count28, align 4
  %42 = add i32 %count29, 1
  store i32 %42, ptr %40, align 4
  %43 = load ptr, ptr %created3, align 8
  %44 = getelementptr i32, ptr %43, i32 0
  store i32 1, ptr %44, align 4
  %45 = load i32, ptr %i, align 4
  %46 = load ptr, ptr %m1, align 8
  %vals30 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %46, i32 0, i32 1
  %vals31 = load ptr, ptr %vals30, align 8
  %47 = getelementptr i32, ptr %vals31, i32 %45
  %48 = load i32, ptr %47, align 4
  %49 = load i32, ptr %i, align 4
  %50 = load ptr, ptr %m1, align 8
  %vals32 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %50, i32 0, i32 1
  %vals33 = load ptr, ptr %vals32, align 8
  %51 = getelementptr i32, ptr %vals33, i32 %49
  ret ptr %51

then17:                                           ; preds = %while_body
  %52 = load ptr, ptr %created3, align 8
  %53 = getelementptr i32, ptr %52, i32 0
  store i32 0, ptr %53, align 4
  %54 = load i32, ptr %i, align 4
  %55 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_Pt_int, ptr %55, i32 0, i32 1
  %vals19 = load ptr, ptr %vals, align 8
  %56 = getelementptr i32, ptr %vals19, i32 %54
  %57 = load i32, ptr %56, align 4
  %58 = load i32, ptr %i, align 4
  %59 = load ptr, ptr %m1, align 8
  %vals20 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %59, i32 0, i32 1
  %vals21 = load ptr, ptr %vals20, align 8
  %60 = getelementptr i32, ptr %vals21, i32 %58
  ret ptr %60

merge18:                                          ; preds = %while_body
  %61 = load i32, ptr %i, align 4
  %62 = add i32 %61, 1
  %63 = load ptr, ptr %m1, align 8
  %cap22 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %63, i32 0, i32 3
  %cap23 = load i32, ptr %cap22, align 4
  %64 = srem i32 %62, %cap23
  store i32 %64, ptr %i, align 4
  br label %while
}

define void @HashMap_grow_Pt_int(ptr %m) {
entry:
  %j = alloca i32, align 4
  %h = alloca i64, align 8
  %i = alloca i32, align 4
  %hashfn = alloca { ptr, ptr }, align 8
  %nu = alloca ptr, align 8
  %nv = alloca ptr, align 8
  %nk = alloca ptr, align 8
  %newcap = alloca i32, align 4
  %ou = alloca ptr, align 8
  %ov = alloca ptr, align 8
  %ok = alloca ptr, align 8
  %oldcap = alloca i32, align 4
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  %0 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_Pt_int, ptr %0, i32 0, i32 3
  %cap2 = load i32, ptr %cap, align 4
  store i32 %cap2, ptr %oldcap, align 4
  %1 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_Pt_int, ptr %1, i32 0, i32 0
  %keys3 = load ptr, ptr %keys, align 8
  store ptr %keys3, ptr %ok, align 8
  %2 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_Pt_int, ptr %2, i32 0, i32 1
  %vals4 = load ptr, ptr %vals, align 8
  store ptr %vals4, ptr %ov, align 8
  %3 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_Pt_int, ptr %3, i32 0, i32 2
  %used5 = load ptr, ptr %used, align 8
  store ptr %used5, ptr %ou, align 8
  %4 = load i32, ptr %oldcap, align 4
  %5 = mul i32 %4, 2
  store i32 %5, ptr %newcap, align 4
  %6 = load i32, ptr %newcap, align 4
  %7 = sext i32 %6 to i64
  %8 = call ptr @alloc_Pt(i64 %7)
  store ptr %8, ptr %nk, align 8
  %9 = load i32, ptr %newcap, align 4
  %10 = sext i32 %9 to i64
  %11 = call ptr @alloc_int(i64 %10)
  store ptr %11, ptr %nv, align 8
  %12 = load i32, ptr %newcap, align 4
  %13 = sext i32 %12 to i64
  %14 = call ptr @alloc_uint8(i64 %13)
  store ptr %14, ptr %nu, align 8
  %15 = load ptr, ptr %nu, align 8
  %16 = load i32, ptr %newcap, align 4
  %17 = call ptr @memset(ptr %15, i32 0, i32 %16)
  %18 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_Pt_int, ptr %18, i32 0, i32 5
  %hash6 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash6, ptr %hashfn, align 8
  store i32 0, ptr %i, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %19 = load i32, ptr %i, align 4
  %20 = load i32, ptr %oldcap, align 4
  %21 = icmp slt i32 %19, %20
  br i1 %21, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %22 = load i32, ptr %i, align 4
  %23 = load ptr, ptr %ou, align 8
  %24 = getelementptr i8, ptr %23, i32 %22
  %25 = load i8, ptr %24, align 1
  %26 = zext i8 %25 to i32
  %27 = icmp eq i32 %26, 1
  br i1 %27, label %then, label %merge

while_exit:                                       ; preds = %while
  %28 = load ptr, ptr %ok, align 8
  call void @free(ptr %28)
  %29 = load ptr, ptr %ov, align 8
  call void @free(ptr %29)
  %30 = load ptr, ptr %ou, align 8
  call void @free(ptr %30)
  %31 = load ptr, ptr %m1, align 8
  %32 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %31, i32 0, i32 0
  %33 = load ptr, ptr %nk, align 8
  store ptr %33, ptr %32, align 8
  %34 = load ptr, ptr %m1, align 8
  %35 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %34, i32 0, i32 1
  %36 = load ptr, ptr %nv, align 8
  store ptr %36, ptr %35, align 8
  %37 = load ptr, ptr %m1, align 8
  %38 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %37, i32 0, i32 2
  %39 = load ptr, ptr %nu, align 8
  store ptr %39, ptr %38, align 8
  %40 = load ptr, ptr %m1, align 8
  %41 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %40, i32 0, i32 3
  %42 = load i32, ptr %newcap, align 4
  store i32 %42, ptr %41, align 4
  ret void

then:                                             ; preds = %while_body
  %43 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %43, 0
  %env.ptr = extractvalue { ptr, ptr } %43, 1
  %44 = load i32, ptr %i, align 4
  %45 = load ptr, ptr %ok, align 8
  %46 = getelementptr %Pt, ptr %45, i32 %44
  %47 = load %Pt, ptr %46, align 4
  %48 = load i32, ptr %i, align 4
  %49 = load ptr, ptr %ok, align 8
  %50 = getelementptr %Pt, ptr %49, i32 %48
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %50)
  store i64 %fn.call, ptr %h, align 8
  %51 = load i64, ptr %h, align 8
  %52 = load i32, ptr %newcap, align 4
  %53 = sext i32 %52 to i64
  %54 = urem i64 %51, %53
  %55 = trunc i64 %54 to i32
  store i32 %55, ptr %j, align 4
  br label %while7

merge:                                            ; preds = %while_exit9, %while_body
  %56 = load i32, ptr %i, align 4
  %57 = add i32 %56, 1
  store i32 %57, ptr %i, align 4
  br label %while

while7:                                           ; preds = %while_body8, %then
  %58 = load i32, ptr %j, align 4
  %59 = load ptr, ptr %nu, align 8
  %60 = getelementptr i8, ptr %59, i32 %58
  %61 = load i8, ptr %60, align 1
  %62 = zext i8 %61 to i32
  %63 = icmp eq i32 %62, 1
  br i1 %63, label %while_body8, label %while_exit9

while_body8:                                      ; preds = %while7
  %64 = load i32, ptr %j, align 4
  %65 = add i32 %64, 1
  %66 = load i32, ptr %newcap, align 4
  %67 = srem i32 %65, %66
  store i32 %67, ptr %j, align 4
  br label %while7

while_exit9:                                      ; preds = %while7
  %68 = load i32, ptr %j, align 4
  %69 = load ptr, ptr %nu, align 8
  %70 = getelementptr i8, ptr %69, i32 %68
  store i8 1, ptr %70, align 1
  %71 = load i32, ptr %j, align 4
  %72 = load ptr, ptr %nk, align 8
  %73 = getelementptr %Pt, ptr %72, i32 %71
  %74 = load i32, ptr %i, align 4
  %75 = load ptr, ptr %ok, align 8
  %76 = getelementptr %Pt, ptr %75, i32 %74
  %77 = load %Pt, ptr %76, align 4
  store %Pt %77, ptr %73, align 4
  %78 = load i32, ptr %j, align 4
  %79 = load ptr, ptr %nv, align 8
  %80 = getelementptr i32, ptr %79, i32 %78
  %81 = load i32, ptr %i, align 4
  %82 = load ptr, ptr %ov, align 8
  %83 = getelementptr i32, ptr %82, i32 %81
  %84 = load i32, ptr %83, align 4
  store i32 %84, ptr %80, align 4
  br label %merge
}

define i32 @HashMap_get_Pt_int(ptr %m, %Pt %key, ptr %out) {
entry:
  %n = alloca i32, align 4
  %i = alloca i32, align 4
  %h = alloca i64, align 8
  %eqfn = alloca { ptr, ptr }, align 8
  %hashfn = alloca { ptr, ptr }, align 8
  %out3 = alloca ptr, align 8
  %key2 = alloca %Pt, align 8
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  store %Pt %key, ptr %key2, align 4
  store ptr %out, ptr %out3, align 8
  %0 = load ptr, ptr %m1, align 8
  %hash = getelementptr inbounds nuw %HashMap_Pt_int, ptr %0, i32 0, i32 5
  %hash4 = load { ptr, ptr }, ptr %hash, align 8
  store { ptr, ptr } %hash4, ptr %hashfn, align 8
  %1 = load ptr, ptr %m1, align 8
  %eq = getelementptr inbounds nuw %HashMap_Pt_int, ptr %1, i32 0, i32 6
  %eq5 = load { ptr, ptr }, ptr %eq, align 8
  store { ptr, ptr } %eq5, ptr %eqfn, align 8
  %2 = load { ptr, ptr }, ptr %hashfn, align 8
  %fn.ptr = extractvalue { ptr, ptr } %2, 0
  %env.ptr = extractvalue { ptr, ptr } %2, 1
  %3 = load %Pt, ptr %key2, align 4
  %fn.call = call i64 %fn.ptr(ptr %env.ptr, ptr %key2)
  store i64 %fn.call, ptr %h, align 8
  %4 = load i64, ptr %h, align 8
  %5 = load ptr, ptr %m1, align 8
  %cap = getelementptr inbounds nuw %HashMap_Pt_int, ptr %5, i32 0, i32 3
  %cap6 = load i32, ptr %cap, align 4
  %6 = sext i32 %cap6 to i64
  %7 = urem i64 %4, %6
  %8 = trunc i64 %7 to i32
  store i32 %8, ptr %i, align 4
  store i32 0, ptr %n, align 4
  br label %while

while:                                            ; preds = %merge, %entry
  %9 = load i32, ptr %i, align 4
  %10 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_Pt_int, ptr %10, i32 0, i32 2
  %used7 = load ptr, ptr %used, align 8
  %11 = getelementptr i8, ptr %used7, i32 %9
  %12 = load i8, ptr %11, align 1
  %13 = zext i8 %12 to i32
  %14 = icmp eq i32 %13, 1
  %15 = load i32, ptr %n, align 4
  %16 = load ptr, ptr %m1, align 8
  %cap8 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %16, i32 0, i32 3
  %cap9 = load i32, ptr %cap8, align 4
  %17 = icmp slt i32 %15, %cap9
  %18 = select i1 %14, i1 %17, i1 false
  br i1 %18, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %19 = load { ptr, ptr }, ptr %eqfn, align 8
  %fn.ptr10 = extractvalue { ptr, ptr } %19, 0
  %env.ptr11 = extractvalue { ptr, ptr } %19, 1
  %20 = load %Pt, ptr %key2, align 4
  %21 = load i32, ptr %i, align 4
  %22 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_Pt_int, ptr %22, i32 0, i32 0
  %keys12 = load ptr, ptr %keys, align 8
  %23 = getelementptr %Pt, ptr %keys12, i32 %21
  %24 = load %Pt, ptr %23, align 4
  %25 = load i32, ptr %i, align 4
  %26 = load ptr, ptr %m1, align 8
  %keys13 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %26, i32 0, i32 0
  %keys14 = load ptr, ptr %keys13, align 8
  %27 = getelementptr %Pt, ptr %keys14, i32 %25
  %fn.call15 = call i32 %fn.ptr10(ptr %env.ptr11, ptr %key2, ptr %27)
  %28 = icmp eq i32 %fn.call15, 1
  br i1 %28, label %then, label %merge

while_exit:                                       ; preds = %while
  ret i32 0

then:                                             ; preds = %while_body
  %29 = load ptr, ptr %out3, align 8
  %30 = getelementptr i32, ptr %29, i32 0
  %31 = load i32, ptr %i, align 4
  %32 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_Pt_int, ptr %32, i32 0, i32 1
  %vals16 = load ptr, ptr %vals, align 8
  %33 = getelementptr i32, ptr %vals16, i32 %31
  %34 = load i32, ptr %33, align 4
  store i32 %34, ptr %30, align 4
  ret i32 1

merge:                                            ; preds = %while_body
  %35 = load i32, ptr %i, align 4
  %36 = add i32 %35, 1
  %37 = load ptr, ptr %m1, align 8
  %cap17 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %37, i32 0, i32 3
  %cap18 = load i32, ptr %cap17, align 4
  %38 = srem i32 %36, %cap18
  store i32 %38, ptr %i, align 4
  %39 = load i32, ptr %n, align 4
  %40 = add i32 %39, 1
  store i32 %40, ptr %n, align 4
  br label %while
}

define void @HashMap_free_int_int(ptr %m) {
entry:
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  %0 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_int_int, ptr %0, i32 0, i32 0
  %keys2 = load ptr, ptr %keys, align 8
  call void @free(ptr %keys2)
  %1 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_int_int, ptr %1, i32 0, i32 1
  %vals3 = load ptr, ptr %vals, align 8
  call void @free(ptr %vals3)
  %2 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_int_int, ptr %2, i32 0, i32 2
  %used4 = load ptr, ptr %used, align 8
  call void @free(ptr %used4)
  %3 = load ptr, ptr %m1, align 8
  %4 = getelementptr inbounds nuw %HashMap_int_int, ptr %3, i32 0, i32 0
  store ptr null, ptr %4, align 8
  %5 = load ptr, ptr %m1, align 8
  %6 = getelementptr inbounds nuw %HashMap_int_int, ptr %5, i32 0, i32 1
  store ptr null, ptr %6, align 8
  %7 = load ptr, ptr %m1, align 8
  %8 = getelementptr inbounds nuw %HashMap_int_int, ptr %7, i32 0, i32 2
  store ptr null, ptr %8, align 8
  %9 = load ptr, ptr %m1, align 8
  %10 = getelementptr inbounds nuw %HashMap_int_int, ptr %9, i32 0, i32 3
  store i32 0, ptr %10, align 4
  %11 = load ptr, ptr %m1, align 8
  %12 = getelementptr inbounds nuw %HashMap_int_int, ptr %11, i32 0, i32 4
  store i32 0, ptr %12, align 4
  ret void
}

define void @HashMap_free_Pt_int(ptr %m) {
entry:
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  %0 = load ptr, ptr %m1, align 8
  %keys = getelementptr inbounds nuw %HashMap_Pt_int, ptr %0, i32 0, i32 0
  %keys2 = load ptr, ptr %keys, align 8
  call void @free(ptr %keys2)
  %1 = load ptr, ptr %m1, align 8
  %vals = getelementptr inbounds nuw %HashMap_Pt_int, ptr %1, i32 0, i32 1
  %vals3 = load ptr, ptr %vals, align 8
  call void @free(ptr %vals3)
  %2 = load ptr, ptr %m1, align 8
  %used = getelementptr inbounds nuw %HashMap_Pt_int, ptr %2, i32 0, i32 2
  %used4 = load ptr, ptr %used, align 8
  call void @free(ptr %used4)
  %3 = load ptr, ptr %m1, align 8
  %4 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %3, i32 0, i32 0
  store ptr null, ptr %4, align 8
  %5 = load ptr, ptr %m1, align 8
  %6 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %5, i32 0, i32 1
  store ptr null, ptr %6, align 8
  %7 = load ptr, ptr %m1, align 8
  %8 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %7, i32 0, i32 2
  store ptr null, ptr %8, align 8
  %9 = load ptr, ptr %m1, align 8
  %10 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %9, i32 0, i32 3
  store i32 0, ptr %10, align 4
  %11 = load ptr, ptr %m1, align 8
  %12 = getelementptr inbounds nuw %HashMap_Pt_int, ptr %11, i32 0, i32 4
  store i32 0, ptr %12, align 4
  ret void
}

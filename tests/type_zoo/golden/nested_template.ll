
@0 = private unnamed_addr constant [33 x i8] c"uint8=%lld int=%lld double=%lld\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @memcpy(ptr, ptr, i32)

declare ptr @memset(ptr, i32, i32)

declare ptr @memmove(ptr, ptr, i32)

declare i32 @memcmp(ptr, ptr, i32)

declare i32 @strlen(ptr)

declare ptr @memchr(ptr, i32, i32)

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %0 = call i64 @esz_uint8(i64 2)
  %1 = call i64 @esz_int(i64 2)
  %2 = call i64 @esz_double(i64 2)
  %3 = call i32 (ptr, ...) @printf(ptr @0, i64 %0, i64 %1, i64 %2)
  ret i32 0
}

define i64 @esz_uint8(i64 %n) {
entry:
  %p = alloca ptr, align 8
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = call ptr @alloc_uint8(i64 %0)
  store ptr %1, ptr %p, align 8
  %2 = load ptr, ptr %p, align 8
  call void @free(ptr %2)
  ret i64 1
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

define i64 @esz_int(i64 %n) {
entry:
  %p = alloca ptr, align 8
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = call ptr @alloc_int(i64 %0)
  store ptr %1, ptr %p, align 8
  %2 = load ptr, ptr %p, align 8
  call void @free(ptr %2)
  ret i64 4
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

define i64 @esz_double(i64 %n) {
entry:
  %p = alloca ptr, align 8
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = call ptr @alloc_double(i64 %0)
  store ptr %1, ptr %p, align 8
  %2 = load ptr, ptr %p, align 8
  call void @free(ptr %2)
  ret i64 8
}

define ptr @alloc_double(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 8
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

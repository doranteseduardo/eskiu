
%Option_int = type { i32, [1 x i64] }

@0 = private unnamed_addr constant [7 x i8] c"%d %d\0A\00", align 1
@1 = private unnamed_addr constant [7 x i8] c"%d %d\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @main() {
entry:
  %variant.tmp1 = alloca %Option_int, align 8
  %b = alloca %Option_int, align 8
  %variant.tmp = alloca %Option_int, align 8
  %a = alloca %Option_int, align 8
  %0 = getelementptr inbounds nuw %Option_int, ptr %variant.tmp, i32 0, i32 0
  store i32 1, ptr %0, align 4
  %1 = getelementptr inbounds nuw %Option_int, ptr %variant.tmp, i32 0, i32 1
  %2 = getelementptr inbounds nuw { i32 }, ptr %1, i32 0, i32 0
  store i32 7, ptr %2, align 4
  %variant.val = load %Option_int, ptr %variant.tmp, align 8
  store %Option_int %variant.val, ptr %a, align 8
  %3 = getelementptr inbounds nuw %Option_int, ptr %variant.tmp1, i32 0, i32 0
  store i32 0, ptr %3, align 4
  %variant.val2 = load %Option_int, ptr %variant.tmp1, align 8
  store %Option_int %variant.val2, ptr %b, align 8
  %4 = load %Option_int, ptr %a, align 8
  %5 = call i1 @opt_is_some_int(ptr %a)
  %6 = load %Option_int, ptr %b, align 8
  %7 = call i1 @opt_is_some_int(ptr %b)
  %8 = zext i1 %5 to i32
  %9 = zext i1 %7 to i32
  %10 = call i32 (ptr, ...) @printf(ptr @0, i32 %8, i32 %9)
  %11 = load %Option_int, ptr %a, align 8
  %12 = call i32 @opt_unwrap_or_int(ptr %a, i32 -1)
  %13 = load %Option_int, ptr %b, align 8
  %14 = call i32 @opt_unwrap_or_int(ptr %b, i32 -1)
  %15 = call i32 (ptr, ...) @printf(ptr @1, i32 %12, i32 %14)
  ret i32 0
}

define i1 @opt_is_some_int(ptr %o) {
entry:
  %v3 = alloca i32, align 4
  %match.subj = alloca %Option_int, align 8
  %o1 = alloca ptr, align 8
  store ptr %o, ptr %o1, align 8
  %0 = load ptr, ptr %o1, align 8
  %1 = load %Option_int, ptr %0, align 8
  store %Option_int %1, ptr %match.subj, align 8
  %2 = getelementptr inbounds nuw %Option_int, ptr %match.subj, i32 0, i32 0
  %match.tag = load i32, ptr %2, align 4
  switch i32 %match.tag, label %match.end [
    i32 1, label %match.arm
    i32 0, label %match.arm2
  ]

match.end:                                        ; preds = %entry
  ret i1 false

match.arm:                                        ; preds = %entry
  %3 = getelementptr inbounds nuw %Option_int, ptr %match.subj, i32 0, i32 1
  %4 = getelementptr inbounds nuw { i32 }, ptr %3, i32 0, i32 0
  %v = load i32, ptr %4, align 4
  store i32 %v, ptr %v3, align 4
  ret i1 true

match.arm2:                                       ; preds = %entry
  ret i1 false
}

define i32 @opt_unwrap_or_int(ptr %o, i32 %dflt) {
entry:
  %v4 = alloca i32, align 4
  %match.subj = alloca %Option_int, align 8
  %dflt2 = alloca i32, align 4
  %o1 = alloca ptr, align 8
  store ptr %o, ptr %o1, align 8
  store i32 %dflt, ptr %dflt2, align 4
  %0 = load ptr, ptr %o1, align 8
  %1 = load %Option_int, ptr %0, align 8
  store %Option_int %1, ptr %match.subj, align 8
  %2 = getelementptr inbounds nuw %Option_int, ptr %match.subj, i32 0, i32 0
  %match.tag = load i32, ptr %2, align 4
  switch i32 %match.tag, label %match.end [
    i32 1, label %match.arm
    i32 0, label %match.arm3
  ]

match.end:                                        ; preds = %entry
  ret i32 0

match.arm:                                        ; preds = %entry
  %3 = getelementptr inbounds nuw %Option_int, ptr %match.subj, i32 0, i32 1
  %4 = getelementptr inbounds nuw { i32 }, ptr %3, i32 0, i32 0
  %v = load i32, ptr %4, align 4
  store i32 %v, ptr %v4, align 4
  %5 = load i32, ptr %v4, align 4
  ret i32 %5

match.arm3:                                       ; preds = %entry
  %6 = load i32, ptr %dflt2, align 4
  ret i32 %6
}

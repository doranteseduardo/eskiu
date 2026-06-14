
%__va_list = type { ptr, ptr, ptr, i32, i32 }

@0 = private unnamed_addr constant [10 x i8] c"%d %d %d\0A\00", align 1
@1 = private unnamed_addr constant [6 x i8] c"%.1f\0A\00", align 1

declare i32 @printf(ptr, ...)

define i32 @sumv(i32 %n, ...) {
entry:
  %i = alloca i32, align 4
  %total = alloca i32, align 4
  %ap = alloca %__va_list, align 8
  %n1 = alloca i32, align 4
  store i32 %n, ptr %n1, align 4
  call void @llvm.va_start.p0(ptr %ap)
  store i32 0, ptr %total, align 4
  store i32 0, ptr %i, align 4
  br label %for

for:                                              ; preds = %for_step, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %n1, align 4
  %2 = icmp slt i32 %0, %1
  br i1 %2, label %for_body, label %for_exit

for_body:                                         ; preds = %for
  %3 = load i32, ptr %total, align 4
  %va.arg = va_arg ptr %ap, i32
  %4 = add i32 %3, %va.arg
  store i32 %4, ptr %total, align 4
  br label %for_step

for_step:                                         ; preds = %for_body
  %5 = load i32, ptr %i, align 4
  %6 = add i32 %5, 1
  store i32 %6, ptr %i, align 4
  br label %for

for_exit:                                         ; preds = %for
  call void @llvm.va_end.p0(ptr %ap)
  %7 = load i32, ptr %total, align 4
  ret i32 %7
}

define double @avg(i32 %n, ...) {
entry:
  %i = alloca i32, align 4
  %s = alloca double, align 8
  %ap = alloca %__va_list, align 8
  %n1 = alloca i32, align 4
  store i32 %n, ptr %n1, align 4
  call void @llvm.va_start.p0(ptr %ap)
  store double 0.000000e+00, ptr %s, align 8
  store i32 0, ptr %i, align 4
  br label %for

for:                                              ; preds = %for_step, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %n1, align 4
  %2 = icmp slt i32 %0, %1
  br i1 %2, label %for_body, label %for_exit

for_body:                                         ; preds = %for
  %3 = load double, ptr %s, align 8
  %va.arg = va_arg ptr %ap, double
  %4 = fadd double %3, %va.arg
  store double %4, ptr %s, align 8
  br label %for_step

for_step:                                         ; preds = %for_body
  %5 = load i32, ptr %i, align 4
  %6 = add i32 %5, 1
  store i32 %6, ptr %i, align 4
  br label %for

for_exit:                                         ; preds = %for
  call void @llvm.va_end.p0(ptr %ap)
  %7 = load double, ptr %s, align 8
  %8 = load i32, ptr %n1, align 4
  %9 = sitofp i32 %8 to double
  %10 = fdiv double %7, %9
  ret double %10
}

define i32 @main() {
entry:
  %0 = call i32 (i32, ...) @sumv(i32 3, i32 10, i32 20, i32 30)
  %1 = call i32 (i32, ...) @sumv(i32 4, i32 1, i32 2, i32 3, i32 4)
  %2 = call i32 (i32, ...) @sumv(i32 0)
  %3 = call i32 (ptr, ...) @printf(ptr @0, i32 %0, i32 %1, i32 %2)
  %4 = call double (i32, ...) @avg(i32 4, double 2.000000e+00, double 4.000000e+00, double 6.000000e+00, double 8.000000e+00)
  %5 = call i32 (ptr, ...) @printf(ptr @1, double %4)
  ret i32 0
}

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start.p0(ptr) #0

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end.p0(ptr) #0

attributes #0 = { nocallback nofree nosync nounwind willreturn }


%Speaker_vtable = type { ptr }
%Dog = type { i32 }
%Cat = type { i32 }
%Speaker_fat = type { ptr, ptr }

@0 = private unnamed_addr constant [8 x i8] c"dog=%d\0A\00", align 1
@Speaker_vtable_Dog = private constant %Speaker_vtable { ptr @Dog_sound }
@1 = private unnamed_addr constant [8 x i8] c"cat=%d\0A\00", align 1
@Speaker_vtable_Cat = private constant %Speaker_vtable { ptr @Cat_sound }
@2 = private unnamed_addr constant [8 x i8] c"sum=%d\0A\00", align 1
@Speaker_vtable_Dog.1 = private constant %Speaker_vtable { ptr @Dog_sound }
@Speaker_vtable_Cat.2 = private constant %Speaker_vtable { ptr @Cat_sound }

declare i32 @printf(ptr, ...)

define i32 @Dog_sound(ptr %self) {
entry:
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  %0 = load ptr, ptr %self1, align 8
  %volume = getelementptr inbounds nuw %Dog, ptr %0, i32 0, i32 0
  %volume2 = load i32, ptr %volume, align 4
  %1 = mul i32 %volume2, 2
  ret i32 %1
}

define i32 @Cat_sound(ptr %self) {
entry:
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  %0 = load ptr, ptr %self1, align 8
  %volume = getelementptr inbounds nuw %Cat, ptr %0, i32 0, i32 0
  %volume2 = load i32, ptr %volume, align 4
  %1 = add i32 %volume2, 1
  ret i32 %1
}

define i32 @speak(ptr %s) {
entry:
  %s1 = alloca ptr, align 8
  store ptr %s, ptr %s1, align 8
  %0 = load ptr, ptr %s1, align 8
  %1 = getelementptr inbounds nuw %Speaker_fat, ptr %0, i32 0, i32 0
  %2 = load ptr, ptr %1, align 8
  %3 = getelementptr inbounds nuw %Speaker_fat, ptr %0, i32 0, i32 1
  %4 = load ptr, ptr %3, align 8
  %5 = getelementptr inbounds nuw %Speaker_vtable, ptr %4, i32 0, i32 0
  %6 = load ptr, ptr %5, align 8
  %7 = call i32 %6(ptr %2)
  ret i32 %7
}

define i32 @main() {
entry:
  %Speaker.box3 = alloca %Speaker_fat, align 8
  %Speaker.box2 = alloca %Speaker_fat, align 8
  %Speaker.box1 = alloca %Speaker_fat, align 8
  %Speaker.box = alloca %Speaker_fat, align 8
  %c = alloca %Cat, align 8
  %d = alloca %Dog, align 8
  %0 = getelementptr inbounds nuw %Dog, ptr %d, i32 0, i32 0
  store i32 10, ptr %0, align 4
  %1 = getelementptr inbounds nuw %Cat, ptr %c, i32 0, i32 0
  store i32 10, ptr %1, align 4
  %2 = load %Dog, ptr %d, align 4
  %3 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box, i32 0, i32 0
  store ptr %d, ptr %3, align 8
  %4 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box, i32 0, i32 1
  store ptr @Speaker_vtable_Dog, ptr %4, align 8
  %5 = call i32 @speak(ptr %Speaker.box)
  %6 = call i32 (ptr, ...) @printf(ptr @0, i32 %5)
  %7 = load %Cat, ptr %c, align 4
  %8 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box1, i32 0, i32 0
  store ptr %c, ptr %8, align 8
  %9 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box1, i32 0, i32 1
  store ptr @Speaker_vtable_Cat, ptr %9, align 8
  %10 = call i32 @speak(ptr %Speaker.box1)
  %11 = call i32 (ptr, ...) @printf(ptr @1, i32 %10)
  %12 = load %Dog, ptr %d, align 4
  %13 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box2, i32 0, i32 0
  store ptr %d, ptr %13, align 8
  %14 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box2, i32 0, i32 1
  store ptr @Speaker_vtable_Dog.1, ptr %14, align 8
  %15 = call i32 @speak(ptr %Speaker.box2)
  %16 = load %Cat, ptr %c, align 4
  %17 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box3, i32 0, i32 0
  store ptr %c, ptr %17, align 8
  %18 = getelementptr inbounds nuw %Speaker_fat, ptr %Speaker.box3, i32 0, i32 1
  store ptr @Speaker_vtable_Cat.2, ptr %18, align 8
  %19 = call i32 @speak(ptr %Speaker.box3)
  %20 = add i32 %15, %19
  %21 = call i32 (ptr, ...) @printf(ptr @2, i32 %20)
  ret i32 0
}

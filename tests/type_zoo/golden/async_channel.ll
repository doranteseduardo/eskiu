
%__lambda0.env = type { ptr }
%FutureHdr = type { i32, { ptr, ptr }, { ptr, ptr } }
%__lambda3.env = type { ptr, ptr, ptr }
%__lambda4.env = type { ptr, ptr, ptr }
%__lambda5.env = type { ptr, ptr, ptr }
%__lambda6.env = type { ptr, ptr, ptr }
%__consume3_frame = type { %Future_int, i32, ptr, ptr, i32, ptr, i32, ptr, i32, ptr }
%Future_int = type { i32, { ptr, ptr }, { ptr, ptr }, i32 }
%__lambda7.env = type { ptr }
%__lambda8.env = type { ptr }
%__lambda9.env = type { ptr }
%__lambda11.env = type { ptr }
%Chan_int = type { ptr, i32, i32, i32, ptr }

@PENDING = private global i32 0
@WAITING = private global i32 1
@READY = private global i32 2
@CANCELLED = private global i32 3
@0 = private unnamed_addr constant [13 x i8] c"buffered=%d\0A\00", align 1
@1 = private unnamed_addr constant [11 x i8] c"parked=%d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare ptr @memset(ptr, i32, i64)

declare ptr @memmove(ptr, ptr, i64)

declare i32 @memcmp(ptr, ptr, i64)

declare i64 @strlen(ptr)

declare ptr @memchr(ptr, i32, i64)

declare i32 @printf(ptr, ...)

define void @spawn_hdr(ptr %f) {
entry:
  %__lambda0.fat = alloca { ptr, ptr }, align 8
  %reap = alloca { ptr, ptr }, align 8
  %f1 = alloca ptr, align 8
  store ptr %f, ptr %f1, align 8
  %__lambda0.env.heap = call ptr @malloc(i64 8)
  %f2 = load ptr, ptr %f1, align 8
  %0 = getelementptr inbounds nuw %__lambda0.env, ptr %__lambda0.env.heap, i32 0, i32 0
  store ptr %f2, ptr %0, align 8
  %1 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda0.fat, i32 0, i32 0
  %2 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda0.fat, i32 0, i32 1
  store ptr @__lambda0, ptr %1, align 8
  store ptr %__lambda0.env.heap, ptr %2, align 8
  %__lambda0.fat.val = load { ptr, ptr }, ptr %__lambda0.fat, align 8
  store { ptr, ptr } %__lambda0.fat.val, ptr %reap, align 8
  %3 = load ptr, ptr %f1, align 8
  %4 = getelementptr inbounds nuw %FutureHdr, ptr %3, i32 0, i32 1
  %5 = load { ptr, ptr }, ptr %reap, align 8
  store { ptr, ptr } %5, ptr %4, align 8
  %6 = load ptr, ptr %f1, align 8
  %state = getelementptr inbounds nuw %FutureHdr, ptr %6, i32 0, i32 0
  %state3 = load i32, ptr %state, align 4
  %7 = load ptr, ptr %f1, align 8
  %8 = getelementptr inbounds nuw %FutureHdr, ptr %7, i32 0, i32 0
  %9 = load i32, ptr @PENDING, align 4
  %10 = load i32, ptr @WAITING, align 4
  %11 = cmpxchg ptr %8, i32 %9, i32 %10 acq_rel acquire, align 4
  %atm.cas.ok = extractvalue { i32, i1 } %11, 1
  br i1 %atm.cas.ok, label %then, label %merge

then:                                             ; preds = %entry
  ret void

merge:                                            ; preds = %entry
  %12 = load { ptr, ptr }, ptr %reap, align 8
  %clos.env = extractvalue { ptr, ptr } %12, 1
  call void @free(ptr %clos.env)
  %13 = load ptr, ptr %f1, align 8
  %on_drop = getelementptr inbounds nuw %FutureHdr, ptr %13, i32 0, i32 2
  %on_drop4 = load { ptr, ptr }, ptr %on_drop, align 8
  %clos.env5 = extractvalue { ptr, ptr } %on_drop4, 1
  call void @free(ptr %clos.env5)
  %14 = load ptr, ptr %f1, align 8
  call void @free(ptr %14)
  ret void
}

define ptr @select2_hdr(ptr %a, ptr %b) {
entry:
  %__lambda4.fat = alloca { ptr, ptr }, align 8
  %onB = alloca { ptr, ptr }, align 8
  %__lambda3.fat = alloca { ptr, ptr }, align 8
  %onA = alloca { ptr, ptr }, align 8
  %sel = alloca ptr, align 8
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = call ptr @future_new_int()
  store ptr %0, ptr %sel, align 8
  %__lambda3.env.heap = call ptr @malloc(i64 24)
  %a3 = load ptr, ptr %a1, align 8
  %1 = getelementptr inbounds nuw %__lambda3.env, ptr %__lambda3.env.heap, i32 0, i32 0
  store ptr %a3, ptr %1, align 8
  %b4 = load ptr, ptr %b2, align 8
  %2 = getelementptr inbounds nuw %__lambda3.env, ptr %__lambda3.env.heap, i32 0, i32 1
  store ptr %b4, ptr %2, align 8
  %sel5 = load ptr, ptr %sel, align 8
  %3 = getelementptr inbounds nuw %__lambda3.env, ptr %__lambda3.env.heap, i32 0, i32 2
  store ptr %sel5, ptr %3, align 8
  %4 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda3.fat, i32 0, i32 0
  %5 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda3.fat, i32 0, i32 1
  store ptr @__lambda3, ptr %4, align 8
  store ptr %__lambda3.env.heap, ptr %5, align 8
  %__lambda3.fat.val = load { ptr, ptr }, ptr %__lambda3.fat, align 8
  store { ptr, ptr } %__lambda3.fat.val, ptr %onA, align 8
  %__lambda4.env.heap = call ptr @malloc(i64 24)
  %a6 = load ptr, ptr %a1, align 8
  %6 = getelementptr inbounds nuw %__lambda4.env, ptr %__lambda4.env.heap, i32 0, i32 0
  store ptr %a6, ptr %6, align 8
  %b7 = load ptr, ptr %b2, align 8
  %7 = getelementptr inbounds nuw %__lambda4.env, ptr %__lambda4.env.heap, i32 0, i32 1
  store ptr %b7, ptr %7, align 8
  %sel8 = load ptr, ptr %sel, align 8
  %8 = getelementptr inbounds nuw %__lambda4.env, ptr %__lambda4.env.heap, i32 0, i32 2
  store ptr %sel8, ptr %8, align 8
  %9 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda4.fat, i32 0, i32 0
  %10 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda4.fat, i32 0, i32 1
  store ptr @__lambda4, ptr %9, align 8
  store ptr %__lambda4.env.heap, ptr %10, align 8
  %__lambda4.fat.val = load { ptr, ptr }, ptr %__lambda4.fat, align 8
  store { ptr, ptr } %__lambda4.fat.val, ptr %onB, align 8
  %11 = load ptr, ptr %a1, align 8
  %12 = getelementptr inbounds nuw %FutureHdr, ptr %11, i32 0, i32 1
  %13 = load { ptr, ptr }, ptr %onA, align 8
  store { ptr, ptr } %13, ptr %12, align 8
  %14 = load ptr, ptr %a1, align 8
  %state = getelementptr inbounds nuw %FutureHdr, ptr %14, i32 0, i32 0
  %state9 = load i32, ptr %state, align 4
  %15 = load ptr, ptr %a1, align 8
  %16 = getelementptr inbounds nuw %FutureHdr, ptr %15, i32 0, i32 0
  %17 = load i32, ptr @PENDING, align 4
  %18 = load i32, ptr @WAITING, align 4
  %19 = cmpxchg ptr %16, i32 %17, i32 %18 acq_rel acquire, align 4
  %atm.cas.ok = extractvalue { i32, i1 } %19, 1
  %20 = icmp eq i1 %atm.cas.ok, false
  br i1 %20, label %then, label %merge

then:                                             ; preds = %entry
  %21 = load { ptr, ptr }, ptr %onA, align 8
  %clos.env = extractvalue { ptr, ptr } %21, 1
  call void @free(ptr %clos.env)
  %22 = load { ptr, ptr }, ptr %onB, align 8
  %clos.env10 = extractvalue { ptr, ptr } %22, 1
  call void @free(ptr %clos.env10)
  %23 = load ptr, ptr %b2, align 8
  call void @future_drop(ptr %23)
  %24 = load ptr, ptr %sel, align 8
  call void @future_complete_int(ptr %24, i32 0)
  %25 = load ptr, ptr %sel, align 8
  ret ptr %25

merge:                                            ; preds = %entry
  %26 = load ptr, ptr %b2, align 8
  %27 = getelementptr inbounds nuw %FutureHdr, ptr %26, i32 0, i32 1
  %28 = load { ptr, ptr }, ptr %onB, align 8
  store { ptr, ptr } %28, ptr %27, align 8
  %29 = load ptr, ptr %b2, align 8
  %state11 = getelementptr inbounds nuw %FutureHdr, ptr %29, i32 0, i32 0
  %state12 = load i32, ptr %state11, align 4
  %30 = load ptr, ptr %b2, align 8
  %31 = getelementptr inbounds nuw %FutureHdr, ptr %30, i32 0, i32 0
  %32 = load i32, ptr @PENDING, align 4
  %33 = load i32, ptr @WAITING, align 4
  %34 = cmpxchg ptr %31, i32 %32, i32 %33 acq_rel acquire, align 4
  %atm.cas.ok13 = extractvalue { i32, i1 } %34, 1
  %35 = icmp eq i1 %atm.cas.ok13, false
  br i1 %35, label %then14, label %merge15

then14:                                           ; preds = %merge
  %36 = load { ptr, ptr }, ptr %onB, align 8
  %clos.env16 = extractvalue { ptr, ptr } %36, 1
  call void @free(ptr %clos.env16)
  %37 = load ptr, ptr %a1, align 8
  call void @future_drop(ptr %37)
  %38 = load ptr, ptr %sel, align 8
  call void @future_complete_int(ptr %38, i32 1)
  %39 = load ptr, ptr %sel, align 8
  ret ptr %39

merge15:                                          ; preds = %merge
  %40 = load ptr, ptr %sel, align 8
  ret ptr %40
}

define ptr @join2_hdr(ptr %a, ptr %b) {
entry:
  %__lambda6.fat = alloca { ptr, ptr }, align 8
  %onB = alloca { ptr, ptr }, align 8
  %__lambda5.fat = alloca { ptr, ptr }, align 8
  %onA = alloca { ptr, ptr }, align 8
  %cnt = alloca ptr, align 8
  %j = alloca ptr, align 8
  %b2 = alloca ptr, align 8
  %a1 = alloca ptr, align 8
  store ptr %a, ptr %a1, align 8
  store ptr %b, ptr %b2, align 8
  %0 = call ptr @future_new_int()
  store ptr %0, ptr %j, align 8
  %1 = call ptr @alloc_int(i64 1)
  store ptr %1, ptr %cnt, align 8
  %2 = load ptr, ptr %cnt, align 8
  store i32 0, ptr %2, align 4
  %__lambda5.env.heap = call ptr @malloc(i64 24)
  %a3 = load ptr, ptr %a1, align 8
  %3 = getelementptr inbounds nuw %__lambda5.env, ptr %__lambda5.env.heap, i32 0, i32 0
  store ptr %a3, ptr %3, align 8
  %cnt4 = load ptr, ptr %cnt, align 8
  %4 = getelementptr inbounds nuw %__lambda5.env, ptr %__lambda5.env.heap, i32 0, i32 1
  store ptr %cnt4, ptr %4, align 8
  %j5 = load ptr, ptr %j, align 8
  %5 = getelementptr inbounds nuw %__lambda5.env, ptr %__lambda5.env.heap, i32 0, i32 2
  store ptr %j5, ptr %5, align 8
  %6 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda5.fat, i32 0, i32 0
  %7 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda5.fat, i32 0, i32 1
  store ptr @__lambda5, ptr %6, align 8
  store ptr %__lambda5.env.heap, ptr %7, align 8
  %__lambda5.fat.val = load { ptr, ptr }, ptr %__lambda5.fat, align 8
  store { ptr, ptr } %__lambda5.fat.val, ptr %onA, align 8
  %__lambda6.env.heap = call ptr @malloc(i64 24)
  %b6 = load ptr, ptr %b2, align 8
  %8 = getelementptr inbounds nuw %__lambda6.env, ptr %__lambda6.env.heap, i32 0, i32 0
  store ptr %b6, ptr %8, align 8
  %cnt7 = load ptr, ptr %cnt, align 8
  %9 = getelementptr inbounds nuw %__lambda6.env, ptr %__lambda6.env.heap, i32 0, i32 1
  store ptr %cnt7, ptr %9, align 8
  %j8 = load ptr, ptr %j, align 8
  %10 = getelementptr inbounds nuw %__lambda6.env, ptr %__lambda6.env.heap, i32 0, i32 2
  store ptr %j8, ptr %10, align 8
  %11 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda6.fat, i32 0, i32 0
  %12 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda6.fat, i32 0, i32 1
  store ptr @__lambda6, ptr %11, align 8
  store ptr %__lambda6.env.heap, ptr %12, align 8
  %__lambda6.fat.val = load { ptr, ptr }, ptr %__lambda6.fat, align 8
  store { ptr, ptr } %__lambda6.fat.val, ptr %onB, align 8
  %13 = load ptr, ptr %a1, align 8
  %14 = getelementptr inbounds nuw %FutureHdr, ptr %13, i32 0, i32 1
  %15 = load { ptr, ptr }, ptr %onA, align 8
  store { ptr, ptr } %15, ptr %14, align 8
  %16 = load ptr, ptr %a1, align 8
  %state = getelementptr inbounds nuw %FutureHdr, ptr %16, i32 0, i32 0
  %state9 = load i32, ptr %state, align 4
  %17 = load ptr, ptr %a1, align 8
  %18 = getelementptr inbounds nuw %FutureHdr, ptr %17, i32 0, i32 0
  %19 = load i32, ptr @PENDING, align 4
  %20 = load i32, ptr @WAITING, align 4
  %21 = cmpxchg ptr %18, i32 %19, i32 %20 acq_rel acquire, align 4
  %atm.cas.ok = extractvalue { i32, i1 } %21, 1
  %22 = icmp eq i1 %atm.cas.ok, false
  br i1 %22, label %then, label %merge

then:                                             ; preds = %entry
  %23 = load ptr, ptr %cnt, align 8
  %24 = load ptr, ptr %cnt, align 8
  %25 = load i32, ptr %24, align 4
  %26 = add i32 %25, 1
  store i32 %26, ptr %23, align 4
  %27 = load { ptr, ptr }, ptr %onA, align 8
  %clos.env = extractvalue { ptr, ptr } %27, 1
  call void @free(ptr %clos.env)
  br label %merge

merge:                                            ; preds = %then, %entry
  %28 = load ptr, ptr %b2, align 8
  %29 = getelementptr inbounds nuw %FutureHdr, ptr %28, i32 0, i32 1
  %30 = load { ptr, ptr }, ptr %onB, align 8
  store { ptr, ptr } %30, ptr %29, align 8
  %31 = load ptr, ptr %b2, align 8
  %state10 = getelementptr inbounds nuw %FutureHdr, ptr %31, i32 0, i32 0
  %state11 = load i32, ptr %state10, align 4
  %32 = load ptr, ptr %b2, align 8
  %33 = getelementptr inbounds nuw %FutureHdr, ptr %32, i32 0, i32 0
  %34 = load i32, ptr @PENDING, align 4
  %35 = load i32, ptr @WAITING, align 4
  %36 = cmpxchg ptr %33, i32 %34, i32 %35 acq_rel acquire, align 4
  %atm.cas.ok12 = extractvalue { i32, i1 } %36, 1
  %37 = icmp eq i1 %atm.cas.ok12, false
  br i1 %37, label %then13, label %merge14

then13:                                           ; preds = %merge
  %38 = load ptr, ptr %cnt, align 8
  %39 = load ptr, ptr %cnt, align 8
  %40 = load i32, ptr %39, align 4
  %41 = add i32 %40, 1
  store i32 %41, ptr %38, align 4
  %42 = load { ptr, ptr }, ptr %onB, align 8
  %clos.env15 = extractvalue { ptr, ptr } %42, 1
  call void @free(ptr %clos.env15)
  br label %merge14

merge14:                                          ; preds = %then13, %merge
  %43 = load ptr, ptr %cnt, align 8
  %44 = load i32, ptr %43, align 4
  %45 = icmp eq i32 %44, 2
  br i1 %45, label %then16, label %merge17

then16:                                           ; preds = %merge14
  %46 = load ptr, ptr %cnt, align 8
  call void @free(ptr %46)
  %47 = load ptr, ptr %j, align 8
  call void @future_complete_int(ptr %47, i32 2)
  br label %merge17

merge17:                                          ; preds = %then16, %merge14
  %48 = load ptr, ptr %j, align 8
  ret ptr %48
}

define void @future_drop(ptr %f) {
entry:
  %old = alloca i32, align 4
  %f1 = alloca ptr, align 8
  store ptr %f, ptr %f1, align 8
  %0 = load ptr, ptr %f1, align 8
  %state = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 0
  %state2 = load i32, ptr %state, align 4
  %1 = load ptr, ptr %f1, align 8
  %2 = getelementptr inbounds nuw %FutureHdr, ptr %1, i32 0, i32 0
  %3 = load i32, ptr @CANCELLED, align 4
  %4 = atomicrmw xchg ptr %2, i32 %3 acq_rel, align 4
  store i32 %4, ptr %old, align 4
  %5 = load i32, ptr %old, align 4
  %6 = load i32, ptr @READY, align 4
  %7 = icmp ne i32 %5, %6
  br i1 %7, label %then, label %merge

then:                                             ; preds = %entry
  %8 = load ptr, ptr %f1, align 8
  %on_drop = getelementptr inbounds nuw %FutureHdr, ptr %8, i32 0, i32 2
  %on_drop3 = load { ptr, ptr }, ptr %on_drop, align 8
  %fn.ptr = extractvalue { ptr, ptr } %on_drop3, 0
  %env.ptr = extractvalue { ptr, ptr } %on_drop3, 1
  call void %fn.ptr(ptr %env.ptr)
  br label %merge

merge:                                            ; preds = %then, %entry
  %9 = load ptr, ptr %f1, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %9, i32 0, i32 1
  %waker4 = load { ptr, ptr }, ptr %waker, align 8
  %clos.env = extractvalue { ptr, ptr } %waker4, 1
  call void @free(ptr %clos.env)
  %10 = load ptr, ptr %f1, align 8
  %on_drop5 = getelementptr inbounds nuw %FutureHdr, ptr %10, i32 0, i32 2
  %on_drop6 = load { ptr, ptr }, ptr %on_drop5, align 8
  %clos.env7 = extractvalue { ptr, ptr } %on_drop6, 1
  call void @free(ptr %clos.env7)
  %11 = load ptr, ptr %f1, align 8
  call void @free(ptr %11)
  ret void
}

define void @__consume3_resume(ptr %fr) {
entry:
  %__lambda9.fat = alloca { ptr, ptr }, align 8
  %__lambda8.fat = alloca { ptr, ptr }, align 8
  %__lambda7.fat = alloca { ptr, ptr }, align 8
  %fr1 = alloca ptr, align 8
  store ptr %fr, ptr %fr1, align 8
  br label %while

while:                                            ; preds = %merge, %entry
  br i1 true, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %0 = load ptr, ptr %fr1, align 8
  %st = getelementptr inbounds nuw %__consume3_frame, ptr %0, i32 0, i32 1
  %st2 = load i32, ptr %st, align 4
  %1 = icmp eq i32 %st2, 0
  br i1 %1, label %then, label %else

while_exit:                                       ; preds = %while
  ret void

then:                                             ; preds = %while_body
  %2 = load ptr, ptr %fr1, align 8
  %3 = getelementptr inbounds nuw %__consume3_frame, ptr %2, i32 0, i32 5
  %4 = load ptr, ptr %fr1, align 8
  %ch = getelementptr inbounds nuw %__consume3_frame, ptr %4, i32 0, i32 3
  %ch3 = load ptr, ptr %ch, align 8
  %5 = call ptr @chan_recv_int(ptr %ch3)
  store ptr %5, ptr %3, align 8
  %6 = load ptr, ptr %fr1, align 8
  %__aw0 = getelementptr inbounds nuw %__consume3_frame, ptr %6, i32 0, i32 5
  %__aw04 = load ptr, ptr %__aw0, align 8
  %__lambda7.env.heap = call ptr @malloc(i64 8)
  %fr5 = load ptr, ptr %fr1, align 8
  %7 = getelementptr inbounds nuw %__lambda7.env, ptr %__lambda7.env.heap, i32 0, i32 0
  store ptr %fr5, ptr %7, align 8
  %8 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda7.fat, i32 0, i32 0
  %9 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda7.fat, i32 0, i32 1
  store ptr @__lambda7, ptr %8, align 8
  store ptr %__lambda7.env.heap, ptr %9, align 8
  %__lambda7.fat.val = load { ptr, ptr }, ptr %__lambda7.fat, align 8
  %10 = call i32 @future_poll_int(ptr %__aw04, { ptr, ptr } %__lambda7.fat.val)
  %11 = icmp eq i32 %10, 0
  br i1 %11, label %then6, label %merge7

merge:                                            ; preds = %merge13, %merge7
  br label %while

else:                                             ; preds = %while_body
  %12 = load ptr, ptr %fr1, align 8
  %st10 = getelementptr inbounds nuw %__consume3_frame, ptr %12, i32 0, i32 1
  %st11 = load i32, ptr %st10, align 4
  %13 = icmp eq i32 %st11, 1
  br i1 %13, label %then12, label %else14

then6:                                            ; preds = %then
  %14 = load ptr, ptr %fr1, align 8
  %15 = getelementptr inbounds nuw %__consume3_frame, ptr %14, i32 0, i32 2
  %16 = load ptr, ptr %fr1, align 8
  %__aw08 = getelementptr inbounds nuw %__consume3_frame, ptr %16, i32 0, i32 5
  %__aw09 = load ptr, ptr %__aw08, align 8
  store ptr %__aw09, ptr %15, align 8
  ret void

merge7:                                           ; preds = %then
  %17 = load ptr, ptr %fr1, align 8
  %18 = getelementptr inbounds nuw %__consume3_frame, ptr %17, i32 0, i32 1
  store i32 1, ptr %18, align 4
  br label %merge

then12:                                           ; preds = %else
  %19 = load ptr, ptr %fr1, align 8
  %20 = getelementptr inbounds nuw %__consume3_frame, ptr %19, i32 0, i32 2
  store ptr null, ptr %20, align 8
  %21 = load ptr, ptr %fr1, align 8
  %22 = getelementptr inbounds nuw %__consume3_frame, ptr %21, i32 0, i32 4
  %23 = load ptr, ptr %fr1, align 8
  %__aw015 = getelementptr inbounds nuw %__consume3_frame, ptr %23, i32 0, i32 5
  %__aw016 = load ptr, ptr %__aw015, align 8
  %value = getelementptr inbounds nuw %Future_int, ptr %__aw016, i32 0, i32 3
  %value17 = load i32, ptr %value, align 4
  store i32 %value17, ptr %22, align 4
  %24 = load ptr, ptr %fr1, align 8
  %__aw018 = getelementptr inbounds nuw %__consume3_frame, ptr %24, i32 0, i32 5
  %__aw019 = load ptr, ptr %__aw018, align 8
  %waker = getelementptr inbounds nuw %Future_int, ptr %__aw019, i32 0, i32 1
  %waker20 = load { ptr, ptr }, ptr %waker, align 8
  %clos.env = extractvalue { ptr, ptr } %waker20, 1
  call void @free(ptr %clos.env)
  %25 = load ptr, ptr %fr1, align 8
  %__aw021 = getelementptr inbounds nuw %__consume3_frame, ptr %25, i32 0, i32 5
  %__aw022 = load ptr, ptr %__aw021, align 8
  call void @free_future_int(ptr %__aw022)
  %26 = load ptr, ptr %fr1, align 8
  %27 = getelementptr inbounds nuw %__consume3_frame, ptr %26, i32 0, i32 7
  %28 = load ptr, ptr %fr1, align 8
  %ch23 = getelementptr inbounds nuw %__consume3_frame, ptr %28, i32 0, i32 3
  %ch24 = load ptr, ptr %ch23, align 8
  %29 = call ptr @chan_recv_int(ptr %ch24)
  store ptr %29, ptr %27, align 8
  %30 = load ptr, ptr %fr1, align 8
  %__aw1 = getelementptr inbounds nuw %__consume3_frame, ptr %30, i32 0, i32 7
  %__aw125 = load ptr, ptr %__aw1, align 8
  %__lambda8.env.heap = call ptr @malloc(i64 8)
  %fr26 = load ptr, ptr %fr1, align 8
  %31 = getelementptr inbounds nuw %__lambda8.env, ptr %__lambda8.env.heap, i32 0, i32 0
  store ptr %fr26, ptr %31, align 8
  %32 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda8.fat, i32 0, i32 0
  %33 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda8.fat, i32 0, i32 1
  store ptr @__lambda8, ptr %32, align 8
  store ptr %__lambda8.env.heap, ptr %33, align 8
  %__lambda8.fat.val = load { ptr, ptr }, ptr %__lambda8.fat, align 8
  %34 = call i32 @future_poll_int(ptr %__aw125, { ptr, ptr } %__lambda8.fat.val)
  %35 = icmp eq i32 %34, 0
  br i1 %35, label %then27, label %merge28

merge13:                                          ; preds = %merge34, %merge28
  br label %merge

else14:                                           ; preds = %else
  %36 = load ptr, ptr %fr1, align 8
  %st31 = getelementptr inbounds nuw %__consume3_frame, ptr %36, i32 0, i32 1
  %st32 = load i32, ptr %st31, align 4
  %37 = icmp eq i32 %st32, 2
  br i1 %37, label %then33, label %else35

then27:                                           ; preds = %then12
  %38 = load ptr, ptr %fr1, align 8
  %39 = getelementptr inbounds nuw %__consume3_frame, ptr %38, i32 0, i32 2
  %40 = load ptr, ptr %fr1, align 8
  %__aw129 = getelementptr inbounds nuw %__consume3_frame, ptr %40, i32 0, i32 7
  %__aw130 = load ptr, ptr %__aw129, align 8
  store ptr %__aw130, ptr %39, align 8
  ret void

merge28:                                          ; preds = %then12
  %41 = load ptr, ptr %fr1, align 8
  %42 = getelementptr inbounds nuw %__consume3_frame, ptr %41, i32 0, i32 1
  store i32 2, ptr %42, align 4
  br label %merge13

then33:                                           ; preds = %else14
  %43 = load ptr, ptr %fr1, align 8
  %44 = getelementptr inbounds nuw %__consume3_frame, ptr %43, i32 0, i32 2
  store ptr null, ptr %44, align 8
  %45 = load ptr, ptr %fr1, align 8
  %46 = getelementptr inbounds nuw %__consume3_frame, ptr %45, i32 0, i32 6
  %47 = load ptr, ptr %fr1, align 8
  %__aw136 = getelementptr inbounds nuw %__consume3_frame, ptr %47, i32 0, i32 7
  %__aw137 = load ptr, ptr %__aw136, align 8
  %value38 = getelementptr inbounds nuw %Future_int, ptr %__aw137, i32 0, i32 3
  %value39 = load i32, ptr %value38, align 4
  store i32 %value39, ptr %46, align 4
  %48 = load ptr, ptr %fr1, align 8
  %__aw140 = getelementptr inbounds nuw %__consume3_frame, ptr %48, i32 0, i32 7
  %__aw141 = load ptr, ptr %__aw140, align 8
  %waker42 = getelementptr inbounds nuw %Future_int, ptr %__aw141, i32 0, i32 1
  %waker43 = load { ptr, ptr }, ptr %waker42, align 8
  %clos.env44 = extractvalue { ptr, ptr } %waker43, 1
  call void @free(ptr %clos.env44)
  %49 = load ptr, ptr %fr1, align 8
  %__aw145 = getelementptr inbounds nuw %__consume3_frame, ptr %49, i32 0, i32 7
  %__aw146 = load ptr, ptr %__aw145, align 8
  call void @free_future_int(ptr %__aw146)
  %50 = load ptr, ptr %fr1, align 8
  %51 = getelementptr inbounds nuw %__consume3_frame, ptr %50, i32 0, i32 9
  %52 = load ptr, ptr %fr1, align 8
  %ch47 = getelementptr inbounds nuw %__consume3_frame, ptr %52, i32 0, i32 3
  %ch48 = load ptr, ptr %ch47, align 8
  %53 = call ptr @chan_recv_int(ptr %ch48)
  store ptr %53, ptr %51, align 8
  %54 = load ptr, ptr %fr1, align 8
  %__aw2 = getelementptr inbounds nuw %__consume3_frame, ptr %54, i32 0, i32 9
  %__aw249 = load ptr, ptr %__aw2, align 8
  %__lambda9.env.heap = call ptr @malloc(i64 8)
  %fr50 = load ptr, ptr %fr1, align 8
  %55 = getelementptr inbounds nuw %__lambda9.env, ptr %__lambda9.env.heap, i32 0, i32 0
  store ptr %fr50, ptr %55, align 8
  %56 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda9.fat, i32 0, i32 0
  %57 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda9.fat, i32 0, i32 1
  store ptr @__lambda9, ptr %56, align 8
  store ptr %__lambda9.env.heap, ptr %57, align 8
  %__lambda9.fat.val = load { ptr, ptr }, ptr %__lambda9.fat, align 8
  %58 = call i32 @future_poll_int(ptr %__aw249, { ptr, ptr } %__lambda9.fat.val)
  %59 = icmp eq i32 %58, 0
  br i1 %59, label %then51, label %merge52

merge34:                                          ; preds = %merge58, %merge52
  br label %merge13

else35:                                           ; preds = %else14
  %60 = load ptr, ptr %fr1, align 8
  %st55 = getelementptr inbounds nuw %__consume3_frame, ptr %60, i32 0, i32 1
  %st56 = load i32, ptr %st55, align 4
  %61 = icmp eq i32 %st56, 3
  br i1 %61, label %then57, label %else59

then51:                                           ; preds = %then33
  %62 = load ptr, ptr %fr1, align 8
  %63 = getelementptr inbounds nuw %__consume3_frame, ptr %62, i32 0, i32 2
  %64 = load ptr, ptr %fr1, align 8
  %__aw253 = getelementptr inbounds nuw %__consume3_frame, ptr %64, i32 0, i32 9
  %__aw254 = load ptr, ptr %__aw253, align 8
  store ptr %__aw254, ptr %63, align 8
  ret void

merge52:                                          ; preds = %then33
  %65 = load ptr, ptr %fr1, align 8
  %66 = getelementptr inbounds nuw %__consume3_frame, ptr %65, i32 0, i32 1
  store i32 3, ptr %66, align 4
  br label %merge34

then57:                                           ; preds = %else35
  %67 = load ptr, ptr %fr1, align 8
  %68 = getelementptr inbounds nuw %__consume3_frame, ptr %67, i32 0, i32 2
  store ptr null, ptr %68, align 8
  %69 = load ptr, ptr %fr1, align 8
  %70 = getelementptr inbounds nuw %__consume3_frame, ptr %69, i32 0, i32 8
  %71 = load ptr, ptr %fr1, align 8
  %__aw260 = getelementptr inbounds nuw %__consume3_frame, ptr %71, i32 0, i32 9
  %__aw261 = load ptr, ptr %__aw260, align 8
  %value62 = getelementptr inbounds nuw %Future_int, ptr %__aw261, i32 0, i32 3
  %value63 = load i32, ptr %value62, align 4
  store i32 %value63, ptr %70, align 4
  %72 = load ptr, ptr %fr1, align 8
  %__aw264 = getelementptr inbounds nuw %__consume3_frame, ptr %72, i32 0, i32 9
  %__aw265 = load ptr, ptr %__aw264, align 8
  %waker66 = getelementptr inbounds nuw %Future_int, ptr %__aw265, i32 0, i32 1
  %waker67 = load { ptr, ptr }, ptr %waker66, align 8
  %clos.env68 = extractvalue { ptr, ptr } %waker67, 1
  call void @free(ptr %clos.env68)
  %73 = load ptr, ptr %fr1, align 8
  %__aw269 = getelementptr inbounds nuw %__consume3_frame, ptr %73, i32 0, i32 9
  %__aw270 = load ptr, ptr %__aw269, align 8
  call void @free_future_int(ptr %__aw270)
  %74 = load ptr, ptr %fr1, align 8
  %75 = getelementptr inbounds nuw %__consume3_frame, ptr %74, i32 0, i32 0
  %76 = getelementptr inbounds nuw %Future_int, ptr %75, i32 0, i32 3
  %77 = load ptr, ptr %fr1, align 8
  %a = getelementptr inbounds nuw %__consume3_frame, ptr %77, i32 0, i32 4
  %a71 = load i32, ptr %a, align 4
  %78 = load ptr, ptr %fr1, align 8
  %b = getelementptr inbounds nuw %__consume3_frame, ptr %78, i32 0, i32 6
  %b72 = load i32, ptr %b, align 4
  %79 = add i32 %a71, %b72
  %80 = load ptr, ptr %fr1, align 8
  %c = getelementptr inbounds nuw %__consume3_frame, ptr %80, i32 0, i32 8
  %c73 = load i32, ptr %c, align 4
  %81 = add i32 %79, %c73
  store i32 %81, ptr %76, align 4
  %82 = load ptr, ptr %fr1, align 8
  %83 = getelementptr inbounds nuw %__consume3_frame, ptr %82, i32 0, i32 0
  %state = getelementptr inbounds nuw %Future_int, ptr %83, i32 0, i32 0
  %state74 = load i32, ptr %state, align 4
  %84 = load ptr, ptr %fr1, align 8
  %85 = getelementptr inbounds nuw %__consume3_frame, ptr %84, i32 0, i32 0
  %86 = getelementptr inbounds nuw %Future_int, ptr %85, i32 0, i32 0
  %87 = atomicrmw xchg ptr %86, i32 2 acq_rel, align 4
  %88 = icmp eq i32 %87, 1
  br i1 %88, label %then75, label %merge76

merge58:                                          ; No predecessors!
  br label %merge34

else59:                                           ; preds = %else35
  ret void

then75:                                           ; preds = %then57
  %89 = load ptr, ptr %fr1, align 8
  %90 = getelementptr inbounds nuw %__consume3_frame, ptr %89, i32 0, i32 0
  %waker77 = getelementptr inbounds nuw %Future_int, ptr %90, i32 0, i32 1
  %waker78 = load { ptr, ptr }, ptr %waker77, align 8
  %fn.ptr = extractvalue { ptr, ptr } %waker78, 0
  %env.ptr = extractvalue { ptr, ptr } %waker78, 1
  call void %fn.ptr(ptr %env.ptr)
  br label %merge76

merge76:                                          ; preds = %then75, %then57
  ret void
}

define ptr @consume3(ptr %ch) {
entry:
  %__lambda11.fat = alloca { ptr, ptr }, align 8
  %__lambda10.fat = alloca { ptr, ptr }, align 8
  %fr = alloca ptr, align 8
  %ch1 = alloca ptr, align 8
  store ptr %ch, ptr %ch1, align 8
  %0 = call ptr @alloc___consume3_frame(i64 1)
  store ptr %0, ptr %fr, align 8
  %1 = load ptr, ptr %fr, align 8
  %2 = getelementptr inbounds nuw %__consume3_frame, ptr %1, i32 0, i32 1
  store i32 0, ptr %2, align 4
  %3 = load ptr, ptr %fr, align 8
  %4 = getelementptr inbounds nuw %__consume3_frame, ptr %3, i32 0, i32 2
  store ptr null, ptr %4, align 8
  %5 = load ptr, ptr %fr, align 8
  %6 = getelementptr inbounds nuw %__consume3_frame, ptr %5, i32 0, i32 0
  %7 = getelementptr inbounds nuw %Future_int, ptr %6, i32 0, i32 0
  store i32 0, ptr %7, align 4
  %8 = load ptr, ptr %fr, align 8
  %9 = getelementptr inbounds nuw %__consume3_frame, ptr %8, i32 0, i32 0
  %10 = getelementptr inbounds nuw %Future_int, ptr %9, i32 0, i32 1
  %11 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda10.fat, i32 0, i32 0
  %12 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda10.fat, i32 0, i32 1
  store ptr @__lambda10, ptr %11, align 8
  store ptr null, ptr %12, align 8
  %__lambda10.fat.val = load { ptr, ptr }, ptr %__lambda10.fat, align 8
  store { ptr, ptr } %__lambda10.fat.val, ptr %10, align 8
  %13 = load ptr, ptr %fr, align 8
  %14 = getelementptr inbounds nuw %__consume3_frame, ptr %13, i32 0, i32 0
  %15 = getelementptr inbounds nuw %Future_int, ptr %14, i32 0, i32 2
  %__lambda11.env.heap = call ptr @malloc(i64 8)
  %fr2 = load ptr, ptr %fr, align 8
  %16 = getelementptr inbounds nuw %__lambda11.env, ptr %__lambda11.env.heap, i32 0, i32 0
  store ptr %fr2, ptr %16, align 8
  %17 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda11.fat, i32 0, i32 0
  %18 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda11.fat, i32 0, i32 1
  store ptr @__lambda11, ptr %17, align 8
  store ptr %__lambda11.env.heap, ptr %18, align 8
  %__lambda11.fat.val = load { ptr, ptr }, ptr %__lambda11.fat, align 8
  store { ptr, ptr } %__lambda11.fat.val, ptr %15, align 8
  %19 = load ptr, ptr %fr, align 8
  %20 = getelementptr inbounds nuw %__consume3_frame, ptr %19, i32 0, i32 3
  %21 = load ptr, ptr %ch1, align 8
  store ptr %21, ptr %20, align 8
  %22 = load ptr, ptr %fr, align 8
  call void @__consume3_resume(ptr %22)
  %23 = load ptr, ptr %fr, align 8
  %ret = getelementptr inbounds nuw %__consume3_frame, ptr %23, i32 0, i32 0
  %ret3 = load %Future_int, ptr %ret, align 8
  %24 = load ptr, ptr %fr, align 8
  %25 = getelementptr inbounds nuw %__consume3_frame, ptr %24, i32 0, i32 0
  ret ptr %25
}

define i32 @main() {
entry:
  %__lambda13.fat = alloca { ptr, ptr }, align 8
  %fp = alloca ptr, align 8
  %p = alloca ptr, align 8
  %__lambda12.fat = alloca { ptr, ptr }, align 8
  %fb = alloca ptr, align 8
  %b = alloca ptr, align 8
  %0 = call ptr @chan_new_int(i32 8)
  store ptr %0, ptr %b, align 8
  %1 = load ptr, ptr %b, align 8
  %2 = call i1 @chan_send_int(ptr %1, i32 10)
  %3 = load ptr, ptr %b, align 8
  %4 = call i1 @chan_send_int(ptr %3, i32 20)
  %5 = load ptr, ptr %b, align 8
  %6 = call i1 @chan_send_int(ptr %5, i32 30)
  %7 = load ptr, ptr %b, align 8
  %8 = call ptr @consume3(ptr %7)
  store ptr %8, ptr %fb, align 8
  %9 = load ptr, ptr %fb, align 8
  %10 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda12.fat, i32 0, i32 0
  %11 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda12.fat, i32 0, i32 1
  store ptr @__lambda12, ptr %10, align 8
  store ptr null, ptr %11, align 8
  %__lambda12.fat.val = load { ptr, ptr }, ptr %__lambda12.fat, align 8
  %12 = call i32 @future_poll_int(ptr %9, { ptr, ptr } %__lambda12.fat.val)
  %13 = load ptr, ptr %fb, align 8
  %value = getelementptr inbounds nuw %Future_int, ptr %13, i32 0, i32 3
  %value1 = load i32, ptr %value, align 4
  %14 = call i32 (ptr, ...) @printf(ptr @0, i32 %value1)
  %15 = load ptr, ptr %fb, align 8
  call void @free_future_int(ptr %15)
  %16 = load ptr, ptr %b, align 8
  call void @chan_free_int(ptr %16)
  %17 = call ptr @chan_new_int(i32 8)
  store ptr %17, ptr %p, align 8
  %18 = load ptr, ptr %p, align 8
  %19 = call ptr @consume3(ptr %18)
  store ptr %19, ptr %fp, align 8
  %20 = load ptr, ptr %fp, align 8
  %21 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda13.fat, i32 0, i32 0
  %22 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda13.fat, i32 0, i32 1
  store ptr @__lambda13, ptr %21, align 8
  store ptr null, ptr %22, align 8
  %__lambda13.fat.val = load { ptr, ptr }, ptr %__lambda13.fat, align 8
  %23 = call i32 @future_poll_int(ptr %20, { ptr, ptr } %__lambda13.fat.val)
  %24 = load ptr, ptr %p, align 8
  %25 = call i1 @chan_send_int(ptr %24, i32 1)
  %26 = load ptr, ptr %p, align 8
  %27 = call i1 @chan_send_int(ptr %26, i32 2)
  %28 = load ptr, ptr %p, align 8
  %29 = call i1 @chan_send_int(ptr %28, i32 4)
  %30 = load ptr, ptr %fp, align 8
  %value2 = getelementptr inbounds nuw %Future_int, ptr %30, i32 0, i32 3
  %value3 = load i32, ptr %value2, align 4
  %31 = call i32 (ptr, ...) @printf(ptr @1, i32 %value3)
  %32 = load ptr, ptr %fp, align 8
  call void @free_future_int(ptr %32)
  %33 = load ptr, ptr %p, align 8
  call void @chan_free_int(ptr %33)
  ret i32 0
}

define internal void @__lambda0(ptr %env) {
entry:
  %self = alloca { ptr, ptr }, align 8
  %f = alloca ptr, align 8
  %f.gep = getelementptr inbounds nuw %__lambda0.env, ptr %env, i32 0, i32 0
  %f.val = load ptr, ptr %f.gep, align 8
  store ptr %f.val, ptr %f, align 8
  %0 = load ptr, ptr %f, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 1
  %waker1 = load { ptr, ptr }, ptr %waker, align 8
  store { ptr, ptr } %waker1, ptr %self, align 8
  %1 = load ptr, ptr %f, align 8
  %on_drop = getelementptr inbounds nuw %FutureHdr, ptr %1, i32 0, i32 2
  %on_drop2 = load { ptr, ptr }, ptr %on_drop, align 8
  %clos.env = extractvalue { ptr, ptr } %on_drop2, 1
  call void @free(ptr %clos.env)
  %2 = load ptr, ptr %f, align 8
  call void @free(ptr %2)
  %3 = load { ptr, ptr }, ptr %self, align 8
  %clos.env3 = extractvalue { ptr, ptr } %3, 1
  call void @free(ptr %clos.env3)
  ret void
}

define ptr @future_new_int() {
entry:
  %__lambda2.fat = alloca { ptr, ptr }, align 8
  %__lambda1.fat = alloca { ptr, ptr }, align 8
  %f = alloca ptr, align 8
  %0 = call ptr @alloc_Future_int(i64 1)
  store ptr %0, ptr %f, align 8
  %1 = load ptr, ptr %f, align 8
  %2 = getelementptr inbounds nuw %Future_int, ptr %1, i32 0, i32 0
  %3 = load i32, ptr @PENDING, align 4
  store i32 %3, ptr %2, align 4
  %4 = load ptr, ptr %f, align 8
  %5 = getelementptr inbounds nuw %Future_int, ptr %4, i32 0, i32 1
  %6 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda1.fat, i32 0, i32 0
  %7 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda1.fat, i32 0, i32 1
  store ptr @__lambda1, ptr %6, align 8
  store ptr null, ptr %7, align 8
  %__lambda1.fat.val = load { ptr, ptr }, ptr %__lambda1.fat, align 8
  store { ptr, ptr } %__lambda1.fat.val, ptr %5, align 8
  %8 = load ptr, ptr %f, align 8
  %9 = getelementptr inbounds nuw %Future_int, ptr %8, i32 0, i32 2
  %10 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda2.fat, i32 0, i32 0
  %11 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda2.fat, i32 0, i32 1
  store ptr @__lambda2, ptr %10, align 8
  store ptr null, ptr %11, align 8
  %__lambda2.fat.val = load { ptr, ptr }, ptr %__lambda2.fat, align 8
  store { ptr, ptr } %__lambda2.fat.val, ptr %9, align 8
  %12 = load ptr, ptr %f, align 8
  ret ptr %12
}

define ptr @alloc_Future_int(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 48
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define internal void @__lambda1(ptr %env) {
entry:
  ret void
}

define internal void @__lambda2(ptr %env) {
entry:
  ret void
}

define internal void @__lambda3(ptr %env) {
entry:
  %self = alloca { ptr, ptr }, align 8
  %sel = alloca ptr, align 8
  %b = alloca ptr, align 8
  %a = alloca ptr, align 8
  %a.gep = getelementptr inbounds nuw %__lambda3.env, ptr %env, i32 0, i32 0
  %a.val = load ptr, ptr %a.gep, align 8
  store ptr %a.val, ptr %a, align 8
  %b.gep = getelementptr inbounds nuw %__lambda3.env, ptr %env, i32 0, i32 1
  %b.val = load ptr, ptr %b.gep, align 8
  store ptr %b.val, ptr %b, align 8
  %sel.gep = getelementptr inbounds nuw %__lambda3.env, ptr %env, i32 0, i32 2
  %sel.val = load ptr, ptr %sel.gep, align 8
  store ptr %sel.val, ptr %sel, align 8
  %0 = load ptr, ptr %a, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 1
  %waker1 = load { ptr, ptr }, ptr %waker, align 8
  store { ptr, ptr } %waker1, ptr %self, align 8
  %1 = load ptr, ptr %b, align 8
  call void @future_drop(ptr %1)
  %2 = load ptr, ptr %sel, align 8
  call void @future_complete_int(ptr %2, i32 0)
  %3 = load { ptr, ptr }, ptr %self, align 8
  %clos.env = extractvalue { ptr, ptr } %3, 1
  call void @free(ptr %clos.env)
  ret void
}

define void @future_complete_int(ptr %f, i32 %v) {
entry:
  %v2 = alloca i32, align 4
  %f1 = alloca ptr, align 8
  store ptr %f, ptr %f1, align 8
  store i32 %v, ptr %v2, align 4
  %0 = load ptr, ptr %f1, align 8
  %1 = getelementptr inbounds nuw %Future_int, ptr %0, i32 0, i32 3
  %2 = load i32, ptr %v2, align 4
  store i32 %2, ptr %1, align 4
  %3 = load ptr, ptr %f1, align 8
  %state = getelementptr inbounds nuw %Future_int, ptr %3, i32 0, i32 0
  %state3 = load i32, ptr %state, align 4
  %4 = load ptr, ptr %f1, align 8
  %5 = getelementptr inbounds nuw %Future_int, ptr %4, i32 0, i32 0
  %6 = load i32, ptr @PENDING, align 4
  %7 = load i32, ptr @READY, align 4
  %8 = cmpxchg ptr %5, i32 %6, i32 %7 acq_rel acquire, align 4
  %atm.cas.ok = extractvalue { i32, i1 } %8, 1
  br i1 %atm.cas.ok, label %then, label %merge

then:                                             ; preds = %entry
  ret void

merge:                                            ; preds = %entry
  %9 = load ptr, ptr %f1, align 8
  %state4 = getelementptr inbounds nuw %Future_int, ptr %9, i32 0, i32 0
  %state5 = load i32, ptr %state4, align 4
  %10 = load ptr, ptr %f1, align 8
  %11 = getelementptr inbounds nuw %Future_int, ptr %10, i32 0, i32 0
  %12 = load i32, ptr @WAITING, align 4
  %13 = load i32, ptr @READY, align 4
  %14 = cmpxchg ptr %11, i32 %12, i32 %13 acq_rel acquire, align 4
  %atm.cas.ok6 = extractvalue { i32, i1 } %14, 1
  br i1 %atm.cas.ok6, label %then7, label %merge8

then7:                                            ; preds = %merge
  %15 = load ptr, ptr %f1, align 8
  %waker = getelementptr inbounds nuw %Future_int, ptr %15, i32 0, i32 1
  %waker9 = load { ptr, ptr }, ptr %waker, align 8
  %fn.ptr = extractvalue { ptr, ptr } %waker9, 0
  %env.ptr = extractvalue { ptr, ptr } %waker9, 1
  call void %fn.ptr(ptr %env.ptr)
  br label %merge8

merge8:                                           ; preds = %then7, %merge
  ret void
}

define internal void @__lambda4(ptr %env) {
entry:
  %self = alloca { ptr, ptr }, align 8
  %sel = alloca ptr, align 8
  %b = alloca ptr, align 8
  %a = alloca ptr, align 8
  %a.gep = getelementptr inbounds nuw %__lambda4.env, ptr %env, i32 0, i32 0
  %a.val = load ptr, ptr %a.gep, align 8
  store ptr %a.val, ptr %a, align 8
  %b.gep = getelementptr inbounds nuw %__lambda4.env, ptr %env, i32 0, i32 1
  %b.val = load ptr, ptr %b.gep, align 8
  store ptr %b.val, ptr %b, align 8
  %sel.gep = getelementptr inbounds nuw %__lambda4.env, ptr %env, i32 0, i32 2
  %sel.val = load ptr, ptr %sel.gep, align 8
  store ptr %sel.val, ptr %sel, align 8
  %0 = load ptr, ptr %b, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 1
  %waker1 = load { ptr, ptr }, ptr %waker, align 8
  store { ptr, ptr } %waker1, ptr %self, align 8
  %1 = load ptr, ptr %a, align 8
  call void @future_drop(ptr %1)
  %2 = load ptr, ptr %sel, align 8
  call void @future_complete_int(ptr %2, i32 1)
  %3 = load { ptr, ptr }, ptr %self, align 8
  %clos.env = extractvalue { ptr, ptr } %3, 1
  call void @free(ptr %clos.env)
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

define internal void @__lambda5(ptr %env) {
entry:
  %self = alloca { ptr, ptr }, align 8
  %j = alloca ptr, align 8
  %cnt = alloca ptr, align 8
  %a = alloca ptr, align 8
  %a.gep = getelementptr inbounds nuw %__lambda5.env, ptr %env, i32 0, i32 0
  %a.val = load ptr, ptr %a.gep, align 8
  store ptr %a.val, ptr %a, align 8
  %cnt.gep = getelementptr inbounds nuw %__lambda5.env, ptr %env, i32 0, i32 1
  %cnt.val = load ptr, ptr %cnt.gep, align 8
  store ptr %cnt.val, ptr %cnt, align 8
  %j.gep = getelementptr inbounds nuw %__lambda5.env, ptr %env, i32 0, i32 2
  %j.val = load ptr, ptr %j.gep, align 8
  store ptr %j.val, ptr %j, align 8
  %0 = load ptr, ptr %a, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 1
  %waker1 = load { ptr, ptr }, ptr %waker, align 8
  store { ptr, ptr } %waker1, ptr %self, align 8
  %1 = load ptr, ptr %cnt, align 8
  %2 = load ptr, ptr %cnt, align 8
  %3 = load i32, ptr %2, align 4
  %4 = add i32 %3, 1
  store i32 %4, ptr %1, align 4
  %5 = load ptr, ptr %cnt, align 8
  %6 = load i32, ptr %5, align 4
  %7 = icmp eq i32 %6, 2
  br i1 %7, label %then, label %merge

then:                                             ; preds = %entry
  %8 = load ptr, ptr %cnt, align 8
  call void @free(ptr %8)
  %9 = load ptr, ptr %j, align 8
  call void @future_complete_int(ptr %9, i32 2)
  br label %merge

merge:                                            ; preds = %then, %entry
  %10 = load { ptr, ptr }, ptr %self, align 8
  %clos.env = extractvalue { ptr, ptr } %10, 1
  call void @free(ptr %clos.env)
  ret void
}

define internal void @__lambda6(ptr %env) {
entry:
  %self = alloca { ptr, ptr }, align 8
  %j = alloca ptr, align 8
  %cnt = alloca ptr, align 8
  %b = alloca ptr, align 8
  %b.gep = getelementptr inbounds nuw %__lambda6.env, ptr %env, i32 0, i32 0
  %b.val = load ptr, ptr %b.gep, align 8
  store ptr %b.val, ptr %b, align 8
  %cnt.gep = getelementptr inbounds nuw %__lambda6.env, ptr %env, i32 0, i32 1
  %cnt.val = load ptr, ptr %cnt.gep, align 8
  store ptr %cnt.val, ptr %cnt, align 8
  %j.gep = getelementptr inbounds nuw %__lambda6.env, ptr %env, i32 0, i32 2
  %j.val = load ptr, ptr %j.gep, align 8
  store ptr %j.val, ptr %j, align 8
  %0 = load ptr, ptr %b, align 8
  %waker = getelementptr inbounds nuw %FutureHdr, ptr %0, i32 0, i32 1
  %waker1 = load { ptr, ptr }, ptr %waker, align 8
  store { ptr, ptr } %waker1, ptr %self, align 8
  %1 = load ptr, ptr %cnt, align 8
  %2 = load ptr, ptr %cnt, align 8
  %3 = load i32, ptr %2, align 4
  %4 = add i32 %3, 1
  store i32 %4, ptr %1, align 4
  %5 = load ptr, ptr %cnt, align 8
  %6 = load i32, ptr %5, align 4
  %7 = icmp eq i32 %6, 2
  br i1 %7, label %then, label %merge

then:                                             ; preds = %entry
  %8 = load ptr, ptr %cnt, align 8
  call void @free(ptr %8)
  %9 = load ptr, ptr %j, align 8
  call void @future_complete_int(ptr %9, i32 2)
  br label %merge

merge:                                            ; preds = %then, %entry
  %10 = load { ptr, ptr }, ptr %self, align 8
  %clos.env = extractvalue { ptr, ptr } %10, 1
  call void @free(ptr %clos.env)
  ret void
}

define ptr @chan_recv_int(ptr %ch) {
entry:
  %v = alloca i32, align 4
  %f = alloca ptr, align 8
  %ch1 = alloca ptr, align 8
  store ptr %ch, ptr %ch1, align 8
  %0 = call ptr @future_new_int()
  store ptr %0, ptr %f, align 8
  %1 = load ptr, ptr %ch1, align 8
  %count = getelementptr inbounds nuw %Chan_int, ptr %1, i32 0, i32 3
  %count2 = load i32, ptr %count, align 4
  %2 = icmp sgt i32 %count2, 0
  br i1 %2, label %then, label %else

then:                                             ; preds = %entry
  %3 = load ptr, ptr %ch1, align 8
  %head = getelementptr inbounds nuw %Chan_int, ptr %3, i32 0, i32 2
  %head3 = load i32, ptr %head, align 4
  %4 = load ptr, ptr %ch1, align 8
  %buf = getelementptr inbounds nuw %Chan_int, ptr %4, i32 0, i32 0
  %buf4 = load ptr, ptr %buf, align 8
  %5 = getelementptr i32, ptr %buf4, i32 %head3
  %6 = load i32, ptr %5, align 4
  store i32 %6, ptr %v, align 4
  %7 = load ptr, ptr %ch1, align 8
  %8 = getelementptr inbounds nuw %Chan_int, ptr %7, i32 0, i32 2
  %9 = load ptr, ptr %ch1, align 8
  %head5 = getelementptr inbounds nuw %Chan_int, ptr %9, i32 0, i32 2
  %head6 = load i32, ptr %head5, align 4
  %10 = add i32 %head6, 1
  store i32 %10, ptr %8, align 4
  %11 = load ptr, ptr %ch1, align 8
  %head7 = getelementptr inbounds nuw %Chan_int, ptr %11, i32 0, i32 2
  %head8 = load i32, ptr %head7, align 4
  %12 = load ptr, ptr %ch1, align 8
  %cap = getelementptr inbounds nuw %Chan_int, ptr %12, i32 0, i32 1
  %cap9 = load i32, ptr %cap, align 4
  %13 = icmp sge i32 %head8, %cap9
  br i1 %13, label %then10, label %merge11

merge:                                            ; preds = %else, %merge11
  %14 = load ptr, ptr %f, align 8
  ret ptr %14

else:                                             ; preds = %entry
  %15 = load ptr, ptr %ch1, align 8
  %16 = getelementptr inbounds nuw %Chan_int, ptr %15, i32 0, i32 4
  %17 = load ptr, ptr %f, align 8
  store ptr %17, ptr %16, align 8
  br label %merge

then10:                                           ; preds = %then
  %18 = load ptr, ptr %ch1, align 8
  %19 = getelementptr inbounds nuw %Chan_int, ptr %18, i32 0, i32 2
  store i32 0, ptr %19, align 4
  br label %merge11

merge11:                                          ; preds = %then10, %then
  %20 = load ptr, ptr %ch1, align 8
  %21 = getelementptr inbounds nuw %Chan_int, ptr %20, i32 0, i32 3
  %22 = load ptr, ptr %ch1, align 8
  %count12 = getelementptr inbounds nuw %Chan_int, ptr %22, i32 0, i32 3
  %count13 = load i32, ptr %count12, align 4
  %23 = sub i32 %count13, 1
  store i32 %23, ptr %21, align 4
  %24 = load ptr, ptr %f, align 8
  %25 = load i32, ptr %v, align 4
  call void @future_complete_int(ptr %24, i32 %25)
  br label %merge
}

define i32 @future_poll_int(ptr %f, { ptr, ptr } %resume) {
entry:
  %resume2 = alloca { ptr, ptr }, align 8
  %f1 = alloca ptr, align 8
  store ptr %f, ptr %f1, align 8
  store { ptr, ptr } %resume, ptr %resume2, align 8
  %0 = load ptr, ptr %f1, align 8
  %1 = getelementptr inbounds nuw %Future_int, ptr %0, i32 0, i32 1
  %2 = load { ptr, ptr }, ptr %resume2, align 8
  store { ptr, ptr } %2, ptr %1, align 8
  %3 = load ptr, ptr %f1, align 8
  %state = getelementptr inbounds nuw %Future_int, ptr %3, i32 0, i32 0
  %state3 = load i32, ptr %state, align 4
  %4 = load ptr, ptr %f1, align 8
  %5 = getelementptr inbounds nuw %Future_int, ptr %4, i32 0, i32 0
  %6 = load i32, ptr @PENDING, align 4
  %7 = load i32, ptr @WAITING, align 4
  %8 = cmpxchg ptr %5, i32 %6, i32 %7 acq_rel acquire, align 4
  %atm.cas.ok = extractvalue { i32, i1 } %8, 1
  br i1 %atm.cas.ok, label %then, label %merge

then:                                             ; preds = %entry
  ret i32 0

merge:                                            ; preds = %entry
  ret i32 1
}

define internal void @__lambda7(ptr %env) {
entry:
  %fr = alloca ptr, align 8
  %fr.gep = getelementptr inbounds nuw %__lambda7.env, ptr %env, i32 0, i32 0
  %fr.val = load ptr, ptr %fr.gep, align 8
  store ptr %fr.val, ptr %fr, align 8
  %0 = load ptr, ptr %fr, align 8
  %1 = getelementptr inbounds nuw %__consume3_frame, ptr %0, i32 0, i32 1
  store i32 1, ptr %1, align 4
  %2 = load ptr, ptr %fr, align 8
  call void @__consume3_resume(ptr %2)
  ret void
}

define void @free_future_int(ptr %f) {
entry:
  %f1 = alloca ptr, align 8
  store ptr %f, ptr %f1, align 8
  %0 = load ptr, ptr %f1, align 8
  %on_drop = getelementptr inbounds nuw %Future_int, ptr %0, i32 0, i32 2
  %on_drop2 = load { ptr, ptr }, ptr %on_drop, align 8
  %clos.env = extractvalue { ptr, ptr } %on_drop2, 1
  call void @free(ptr %clos.env)
  %1 = load ptr, ptr %f1, align 8
  call void @free(ptr %1)
  ret void
}

define internal void @__lambda8(ptr %env) {
entry:
  %fr = alloca ptr, align 8
  %fr.gep = getelementptr inbounds nuw %__lambda8.env, ptr %env, i32 0, i32 0
  %fr.val = load ptr, ptr %fr.gep, align 8
  store ptr %fr.val, ptr %fr, align 8
  %0 = load ptr, ptr %fr, align 8
  %1 = getelementptr inbounds nuw %__consume3_frame, ptr %0, i32 0, i32 1
  store i32 2, ptr %1, align 4
  %2 = load ptr, ptr %fr, align 8
  call void @__consume3_resume(ptr %2)
  ret void
}

define internal void @__lambda9(ptr %env) {
entry:
  %fr = alloca ptr, align 8
  %fr.gep = getelementptr inbounds nuw %__lambda9.env, ptr %env, i32 0, i32 0
  %fr.val = load ptr, ptr %fr.gep, align 8
  store ptr %fr.val, ptr %fr, align 8
  %0 = load ptr, ptr %fr, align 8
  %1 = getelementptr inbounds nuw %__consume3_frame, ptr %0, i32 0, i32 1
  store i32 3, ptr %1, align 4
  %2 = load ptr, ptr %fr, align 8
  call void @__consume3_resume(ptr %2)
  ret void
}

define ptr @alloc___consume3_frame(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 120
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define internal void @__lambda10(ptr %env) {
entry:
  ret void
}

define internal void @__lambda11(ptr %env) {
entry:
  %fr = alloca ptr, align 8
  %fr.gep = getelementptr inbounds nuw %__lambda11.env, ptr %env, i32 0, i32 0
  %fr.val = load ptr, ptr %fr.gep, align 8
  store ptr %fr.val, ptr %fr, align 8
  %0 = load ptr, ptr %fr, align 8
  %awaiting = getelementptr inbounds nuw %__consume3_frame, ptr %0, i32 0, i32 2
  %awaiting1 = load ptr, ptr %awaiting, align 8
  %1 = icmp ne ptr %awaiting1, null
  br i1 %1, label %then, label %merge

then:                                             ; preds = %entry
  %2 = load ptr, ptr %fr, align 8
  %awaiting2 = getelementptr inbounds nuw %__consume3_frame, ptr %2, i32 0, i32 2
  %awaiting3 = load ptr, ptr %awaiting2, align 8
  call void @future_drop(ptr %awaiting3)
  br label %merge

merge:                                            ; preds = %then, %entry
  ret void
}

define ptr @chan_new_int(i32 %cap) {
entry:
  %ch = alloca ptr, align 8
  %cap1 = alloca i32, align 4
  store i32 %cap, ptr %cap1, align 4
  %0 = call ptr @alloc_Chan_int(i64 1)
  store ptr %0, ptr %ch, align 8
  %1 = load ptr, ptr %ch, align 8
  %2 = getelementptr inbounds nuw %Chan_int, ptr %1, i32 0, i32 0
  %3 = load i32, ptr %cap1, align 4
  %4 = sext i32 %3 to i64
  %5 = call ptr @alloc_int(i64 %4)
  store ptr %5, ptr %2, align 8
  %6 = load ptr, ptr %ch, align 8
  %7 = getelementptr inbounds nuw %Chan_int, ptr %6, i32 0, i32 1
  %8 = load i32, ptr %cap1, align 4
  store i32 %8, ptr %7, align 4
  %9 = load ptr, ptr %ch, align 8
  %10 = getelementptr inbounds nuw %Chan_int, ptr %9, i32 0, i32 2
  store i32 0, ptr %10, align 4
  %11 = load ptr, ptr %ch, align 8
  %12 = getelementptr inbounds nuw %Chan_int, ptr %11, i32 0, i32 3
  store i32 0, ptr %12, align 4
  %13 = load ptr, ptr %ch, align 8
  %14 = getelementptr inbounds nuw %Chan_int, ptr %13, i32 0, i32 4
  store ptr null, ptr %14, align 8
  %15 = load ptr, ptr %ch, align 8
  ret ptr %15
}

define ptr @alloc_Chan_int(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 32
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define i1 @chan_send_int(ptr %ch, i32 %v) {
entry:
  %tail = alloca i32, align 4
  %w = alloca ptr, align 8
  %v2 = alloca i32, align 4
  %ch1 = alloca ptr, align 8
  store ptr %ch, ptr %ch1, align 8
  store i32 %v, ptr %v2, align 4
  %0 = load ptr, ptr %ch1, align 8
  %waiter = getelementptr inbounds nuw %Chan_int, ptr %0, i32 0, i32 4
  %waiter3 = load ptr, ptr %waiter, align 8
  %1 = icmp ne ptr %waiter3, null
  br i1 %1, label %then, label %merge

then:                                             ; preds = %entry
  %2 = load ptr, ptr %ch1, align 8
  %waiter4 = getelementptr inbounds nuw %Chan_int, ptr %2, i32 0, i32 4
  %waiter5 = load ptr, ptr %waiter4, align 8
  store ptr %waiter5, ptr %w, align 8
  %3 = load ptr, ptr %ch1, align 8
  %4 = getelementptr inbounds nuw %Chan_int, ptr %3, i32 0, i32 4
  store ptr null, ptr %4, align 8
  %5 = load ptr, ptr %w, align 8
  %6 = load i32, ptr %v2, align 4
  call void @future_complete_int(ptr %5, i32 %6)
  ret i1 true

merge:                                            ; preds = %entry
  %7 = load ptr, ptr %ch1, align 8
  %count = getelementptr inbounds nuw %Chan_int, ptr %7, i32 0, i32 3
  %count6 = load i32, ptr %count, align 4
  %8 = load ptr, ptr %ch1, align 8
  %cap = getelementptr inbounds nuw %Chan_int, ptr %8, i32 0, i32 1
  %cap7 = load i32, ptr %cap, align 4
  %9 = icmp sge i32 %count6, %cap7
  br i1 %9, label %then8, label %merge9

then8:                                            ; preds = %merge
  ret i1 false

merge9:                                           ; preds = %merge
  %10 = load ptr, ptr %ch1, align 8
  %head = getelementptr inbounds nuw %Chan_int, ptr %10, i32 0, i32 2
  %head10 = load i32, ptr %head, align 4
  %11 = load ptr, ptr %ch1, align 8
  %count11 = getelementptr inbounds nuw %Chan_int, ptr %11, i32 0, i32 3
  %count12 = load i32, ptr %count11, align 4
  %12 = add i32 %head10, %count12
  store i32 %12, ptr %tail, align 4
  %13 = load i32, ptr %tail, align 4
  %14 = load ptr, ptr %ch1, align 8
  %cap13 = getelementptr inbounds nuw %Chan_int, ptr %14, i32 0, i32 1
  %cap14 = load i32, ptr %cap13, align 4
  %15 = icmp sge i32 %13, %cap14
  br i1 %15, label %then15, label %merge16

then15:                                           ; preds = %merge9
  %16 = load i32, ptr %tail, align 4
  %17 = load ptr, ptr %ch1, align 8
  %cap17 = getelementptr inbounds nuw %Chan_int, ptr %17, i32 0, i32 1
  %cap18 = load i32, ptr %cap17, align 4
  %18 = sub i32 %16, %cap18
  store i32 %18, ptr %tail, align 4
  br label %merge16

merge16:                                          ; preds = %then15, %merge9
  %19 = load i32, ptr %tail, align 4
  %20 = load ptr, ptr %ch1, align 8
  %buf = getelementptr inbounds nuw %Chan_int, ptr %20, i32 0, i32 0
  %buf19 = load ptr, ptr %buf, align 8
  %21 = getelementptr i32, ptr %buf19, i32 %19
  %22 = load i32, ptr %v2, align 4
  store i32 %22, ptr %21, align 4
  %23 = load ptr, ptr %ch1, align 8
  %24 = getelementptr inbounds nuw %Chan_int, ptr %23, i32 0, i32 3
  %25 = load ptr, ptr %ch1, align 8
  %count20 = getelementptr inbounds nuw %Chan_int, ptr %25, i32 0, i32 3
  %count21 = load i32, ptr %count20, align 4
  %26 = add i32 %count21, 1
  store i32 %26, ptr %24, align 4
  ret i1 true
}

define internal void @__lambda12(ptr %env) {
entry:
  ret void
}

define void @chan_free_int(ptr %ch) {
entry:
  %ch1 = alloca ptr, align 8
  store ptr %ch, ptr %ch1, align 8
  %0 = load ptr, ptr %ch1, align 8
  %buf = getelementptr inbounds nuw %Chan_int, ptr %0, i32 0, i32 0
  %buf2 = load ptr, ptr %buf, align 8
  call void @free(ptr %buf2)
  %1 = load ptr, ptr %ch1, align 8
  call void @free(ptr %1)
  ret void
}

define internal void @__lambda13(ptr %env) {
entry:
  ret void
}

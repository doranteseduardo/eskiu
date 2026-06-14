
%__lambda0.env = type { ptr }
%FutureHdr = type { i32, { ptr, ptr }, { ptr, ptr } }
%__lambda3.env = type { ptr, ptr, ptr }
%__lambda4.env = type { ptr, ptr, ptr }
%__lambda5.env = type { ptr, ptr, ptr }
%__lambda6.env = type { ptr, ptr, ptr }
%__one_frame = type { %Future_int, i32, ptr, i32, ptr }
%Future_int = type { i32, { ptr, ptr }, { ptr, ptr }, i32 }
%__lambda7.env = type { ptr }
%__lambda9.env = type { ptr }

@PENDING = private global i32 0
@WAITING = private global i32 1
@READY = private global i32 2
@CANCELLED = private global i32 3
@0 = private unnamed_addr constant [7 x i8] c"%d %d\0A\00", align 1

declare ptr @malloc(i64)

declare void @free(ptr)

declare ptr @memcpy(ptr, ptr, i32)

declare ptr @memset(ptr, i32, i32)

declare ptr @memmove(ptr, ptr, i32)

declare i32 @memcmp(ptr, ptr, i32)

declare i32 @strlen(ptr)

declare ptr @memchr(ptr, i32, i32)

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

define ptr @produce() {
entry:
  %f = alloca ptr, align 8
  %0 = call ptr @future_new_int()
  store ptr %0, ptr %f, align 8
  %1 = load ptr, ptr %f, align 8
  call void @future_complete_int(ptr %1, i32 41)
  %2 = load ptr, ptr %f, align 8
  ret ptr %2
}

define void @__one_resume(ptr %fr) {
entry:
  %__lambda7.fat = alloca { ptr, ptr }, align 8
  %fr1 = alloca ptr, align 8
  store ptr %fr, ptr %fr1, align 8
  br label %while

while:                                            ; preds = %merge, %entry
  br i1 true, label %while_body, label %while_exit

while_body:                                       ; preds = %while
  %0 = load ptr, ptr %fr1, align 8
  %st = getelementptr inbounds nuw %__one_frame, ptr %0, i32 0, i32 1
  %st2 = load i32, ptr %st, align 4
  %1 = icmp eq i32 %st2, 0
  br i1 %1, label %then, label %else

while_exit:                                       ; preds = %while
  ret void

then:                                             ; preds = %while_body
  %2 = load ptr, ptr %fr1, align 8
  %3 = getelementptr inbounds nuw %__one_frame, ptr %2, i32 0, i32 4
  %4 = call ptr @produce()
  store ptr %4, ptr %3, align 8
  %5 = load ptr, ptr %fr1, align 8
  %__aw0 = getelementptr inbounds nuw %__one_frame, ptr %5, i32 0, i32 4
  %__aw03 = load ptr, ptr %__aw0, align 8
  %__lambda7.env.heap = call ptr @malloc(i64 8)
  %fr4 = load ptr, ptr %fr1, align 8
  %6 = getelementptr inbounds nuw %__lambda7.env, ptr %__lambda7.env.heap, i32 0, i32 0
  store ptr %fr4, ptr %6, align 8
  %7 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda7.fat, i32 0, i32 0
  %8 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda7.fat, i32 0, i32 1
  store ptr @__lambda7, ptr %7, align 8
  store ptr %__lambda7.env.heap, ptr %8, align 8
  %__lambda7.fat.val = load { ptr, ptr }, ptr %__lambda7.fat, align 8
  %9 = call i32 @future_poll_int(ptr %__aw03, { ptr, ptr } %__lambda7.fat.val)
  %10 = icmp eq i32 %9, 0
  br i1 %10, label %then5, label %merge6

merge:                                            ; preds = %merge12, %merge6
  br label %while

else:                                             ; preds = %while_body
  %11 = load ptr, ptr %fr1, align 8
  %st9 = getelementptr inbounds nuw %__one_frame, ptr %11, i32 0, i32 1
  %st10 = load i32, ptr %st9, align 4
  %12 = icmp eq i32 %st10, 1
  br i1 %12, label %then11, label %else13

then5:                                            ; preds = %then
  %13 = load ptr, ptr %fr1, align 8
  %14 = getelementptr inbounds nuw %__one_frame, ptr %13, i32 0, i32 2
  %15 = load ptr, ptr %fr1, align 8
  %__aw07 = getelementptr inbounds nuw %__one_frame, ptr %15, i32 0, i32 4
  %__aw08 = load ptr, ptr %__aw07, align 8
  store ptr %__aw08, ptr %14, align 8
  ret void

merge6:                                           ; preds = %then
  %16 = load ptr, ptr %fr1, align 8
  %17 = getelementptr inbounds nuw %__one_frame, ptr %16, i32 0, i32 1
  store i32 1, ptr %17, align 4
  br label %merge

then11:                                           ; preds = %else
  %18 = load ptr, ptr %fr1, align 8
  %19 = getelementptr inbounds nuw %__one_frame, ptr %18, i32 0, i32 2
  store ptr null, ptr %19, align 8
  %20 = load ptr, ptr %fr1, align 8
  %21 = getelementptr inbounds nuw %__one_frame, ptr %20, i32 0, i32 3
  %22 = load ptr, ptr %fr1, align 8
  %__aw014 = getelementptr inbounds nuw %__one_frame, ptr %22, i32 0, i32 4
  %__aw015 = load ptr, ptr %__aw014, align 8
  %value = getelementptr inbounds nuw %Future_int, ptr %__aw015, i32 0, i32 3
  %value16 = load i32, ptr %value, align 4
  store i32 %value16, ptr %21, align 4
  %23 = load ptr, ptr %fr1, align 8
  %__aw017 = getelementptr inbounds nuw %__one_frame, ptr %23, i32 0, i32 4
  %__aw018 = load ptr, ptr %__aw017, align 8
  %waker = getelementptr inbounds nuw %Future_int, ptr %__aw018, i32 0, i32 1
  %waker19 = load { ptr, ptr }, ptr %waker, align 8
  %clos.env = extractvalue { ptr, ptr } %waker19, 1
  call void @free(ptr %clos.env)
  %24 = load ptr, ptr %fr1, align 8
  %__aw020 = getelementptr inbounds nuw %__one_frame, ptr %24, i32 0, i32 4
  %__aw021 = load ptr, ptr %__aw020, align 8
  call void @free_future_int(ptr %__aw021)
  %25 = load ptr, ptr %fr1, align 8
  %26 = getelementptr inbounds nuw %__one_frame, ptr %25, i32 0, i32 0
  %27 = getelementptr inbounds nuw %Future_int, ptr %26, i32 0, i32 3
  %28 = load ptr, ptr %fr1, align 8
  %n = getelementptr inbounds nuw %__one_frame, ptr %28, i32 0, i32 3
  %n22 = load i32, ptr %n, align 4
  %29 = add i32 %n22, 1
  store i32 %29, ptr %27, align 4
  %30 = load ptr, ptr %fr1, align 8
  %31 = getelementptr inbounds nuw %__one_frame, ptr %30, i32 0, i32 0
  %state = getelementptr inbounds nuw %Future_int, ptr %31, i32 0, i32 0
  %state23 = load i32, ptr %state, align 4
  %32 = load ptr, ptr %fr1, align 8
  %33 = getelementptr inbounds nuw %__one_frame, ptr %32, i32 0, i32 0
  %34 = getelementptr inbounds nuw %Future_int, ptr %33, i32 0, i32 0
  %35 = atomicrmw xchg ptr %34, i32 2 acq_rel, align 4
  %36 = icmp eq i32 %35, 1
  br i1 %36, label %then24, label %merge25

merge12:                                          ; No predecessors!
  br label %merge

else13:                                           ; preds = %else
  ret void

then24:                                           ; preds = %then11
  %37 = load ptr, ptr %fr1, align 8
  %38 = getelementptr inbounds nuw %__one_frame, ptr %37, i32 0, i32 0
  %waker26 = getelementptr inbounds nuw %Future_int, ptr %38, i32 0, i32 1
  %waker27 = load { ptr, ptr }, ptr %waker26, align 8
  %fn.ptr = extractvalue { ptr, ptr } %waker27, 0
  %env.ptr = extractvalue { ptr, ptr } %waker27, 1
  call void %fn.ptr(ptr %env.ptr)
  br label %merge25

merge25:                                          ; preds = %then24, %then11
  ret void
}

define ptr @one() {
entry:
  %__lambda9.fat = alloca { ptr, ptr }, align 8
  %__lambda8.fat = alloca { ptr, ptr }, align 8
  %fr = alloca ptr, align 8
  %0 = call ptr @alloc___one_frame(i64 1)
  store ptr %0, ptr %fr, align 8
  %1 = load ptr, ptr %fr, align 8
  %2 = getelementptr inbounds nuw %__one_frame, ptr %1, i32 0, i32 1
  store i32 0, ptr %2, align 4
  %3 = load ptr, ptr %fr, align 8
  %4 = getelementptr inbounds nuw %__one_frame, ptr %3, i32 0, i32 2
  store ptr null, ptr %4, align 8
  %5 = load ptr, ptr %fr, align 8
  %6 = getelementptr inbounds nuw %__one_frame, ptr %5, i32 0, i32 0
  %7 = getelementptr inbounds nuw %Future_int, ptr %6, i32 0, i32 0
  store i32 0, ptr %7, align 4
  %8 = load ptr, ptr %fr, align 8
  %9 = getelementptr inbounds nuw %__one_frame, ptr %8, i32 0, i32 0
  %10 = getelementptr inbounds nuw %Future_int, ptr %9, i32 0, i32 1
  %11 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda8.fat, i32 0, i32 0
  %12 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda8.fat, i32 0, i32 1
  store ptr @__lambda8, ptr %11, align 8
  store ptr null, ptr %12, align 8
  %__lambda8.fat.val = load { ptr, ptr }, ptr %__lambda8.fat, align 8
  store { ptr, ptr } %__lambda8.fat.val, ptr %10, align 8
  %13 = load ptr, ptr %fr, align 8
  %14 = getelementptr inbounds nuw %__one_frame, ptr %13, i32 0, i32 0
  %15 = getelementptr inbounds nuw %Future_int, ptr %14, i32 0, i32 2
  %__lambda9.env.heap = call ptr @malloc(i64 8)
  %fr1 = load ptr, ptr %fr, align 8
  %16 = getelementptr inbounds nuw %__lambda9.env, ptr %__lambda9.env.heap, i32 0, i32 0
  store ptr %fr1, ptr %16, align 8
  %17 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda9.fat, i32 0, i32 0
  %18 = getelementptr inbounds nuw { ptr, ptr }, ptr %__lambda9.fat, i32 0, i32 1
  store ptr @__lambda9, ptr %17, align 8
  store ptr %__lambda9.env.heap, ptr %18, align 8
  %__lambda9.fat.val = load { ptr, ptr }, ptr %__lambda9.fat, align 8
  store { ptr, ptr } %__lambda9.fat.val, ptr %15, align 8
  %19 = load ptr, ptr %fr, align 8
  call void @__one_resume(ptr %19)
  %20 = load ptr, ptr %fr, align 8
  %ret = getelementptr inbounds nuw %__one_frame, ptr %20, i32 0, i32 0
  %ret2 = load %Future_int, ptr %ret, align 8
  %21 = load ptr, ptr %fr, align 8
  %22 = getelementptr inbounds nuw %__one_frame, ptr %21, i32 0, i32 0
  ret ptr %22
}

define i32 @main() {
entry:
  %r = alloca ptr, align 8
  %0 = call ptr @one()
  store ptr %0, ptr %r, align 8
  %1 = load ptr, ptr %r, align 8
  %value = getelementptr inbounds nuw %Future_int, ptr %1, i32 0, i32 3
  %value1 = load i32, ptr %value, align 4
  %2 = load ptr, ptr %r, align 8
  %state = getelementptr inbounds nuw %Future_int, ptr %2, i32 0, i32 0
  %state2 = load i32, ptr %state, align 4
  %3 = call i32 (ptr, ...) @printf(ptr @0, i32 %value1, i32 %state2)
  %4 = load ptr, ptr %r, align 8
  call void @free_future_int(ptr %4)
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
  %1 = getelementptr inbounds nuw %__one_frame, ptr %0, i32 0, i32 1
  store i32 1, ptr %1, align 4
  %2 = load ptr, ptr %fr, align 8
  call void @__one_resume(ptr %2)
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

define ptr @alloc___one_frame(i64 %n) {
entry:
  %n1 = alloca i64, align 8
  store i64 %n, ptr %n1, align 8
  %0 = load i64, ptr %n1, align 8
  %1 = mul i64 %0, 80
  %2 = call ptr @malloc(i64 %1)
  ret ptr %2
}

define internal void @__lambda8(ptr %env) {
entry:
  ret void
}

define internal void @__lambda9(ptr %env) {
entry:
  %fr = alloca ptr, align 8
  %fr.gep = getelementptr inbounds nuw %__lambda9.env, ptr %env, i32 0, i32 0
  %fr.val = load ptr, ptr %fr.gep, align 8
  store ptr %fr.val, ptr %fr, align 8
  %0 = load ptr, ptr %fr, align 8
  %awaiting = getelementptr inbounds nuw %__one_frame, ptr %0, i32 0, i32 2
  %awaiting1 = load ptr, ptr %awaiting, align 8
  %1 = icmp ne ptr %awaiting1, null
  br i1 %1, label %then, label %merge

then:                                             ; preds = %entry
  %2 = load ptr, ptr %fr, align 8
  %awaiting2 = getelementptr inbounds nuw %__one_frame, ptr %2, i32 0, i32 2
  %awaiting3 = load ptr, ptr %awaiting2, align 8
  call void @future_drop(ptr %awaiting3)
  br label %merge

merge:                                            ; preds = %then, %entry
  ret void
}

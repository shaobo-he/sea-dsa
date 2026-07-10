; memcpy of a pointer-free object must NOT unify source and destination
; nodes. The transferred type is recovered by pointee inference (the source
; strips to an alloca of [2 x i64]), restoring dev14's transfersNoPointers
; behavior for opaque-pointer modules. A regression unifies the two allocas
; into one node whose allocation-site list contains both.
;
; RUN: %seadsa %s %ci_dsa --sea-dsa-dot --sea-dsa-type-aware=true --sea-dsa-dot-print-as --sea-dsa-dot-outdir=%T/opaque_ptr_memcpy_noptr.ll
; RUN: cat %T/opaque_ptr_memcpy_noptr.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-SRC
; RUN: cat %T/opaque_ptr_memcpy_noptr.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-DST
; RUN: cat %T/opaque_ptr_memcpy_noptr.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-SEP
; CHECK-SRC: AS: src
; CHECK-DST: AS: dst
; CHECK-SEP-NOT: AS: src, dst
; CHECK-SEP-NOT: AS: dst, src

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @use(ptr, ptr)

define void @main() {
entry:
  %src = alloca [2 x i64], align 8
  %dst = alloca [2 x i64], align 8
  %f = getelementptr [2 x i64], ptr %src, i64 0, i64 1
  store i64 7, ptr %f, align 8
  call void @llvm.memcpy.p0.p0.i64(ptr %dst, ptr %src, i64 16, i1 false)
  call void @use(ptr %src, ptr %dst)
  ret void
}

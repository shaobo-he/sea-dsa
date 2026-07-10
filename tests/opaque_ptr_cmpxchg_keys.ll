; A pointer cmpxchg must key its content field consistently with plain
; stores/loads of the same slot (fieldTypeForCmpXchg): the store keys the
; slot ptr<i32> (gep i32 evidence on %v) and the cmpxchg picks up the same
; evidence through its compare operand. A regression forks a second link on
; the slot node, visible as an <s1> record port.
;
; RUN: %seadsa %s %ci_dsa --sea-dsa-dot --sea-dsa-type-aware=true --sea-dsa-dot-outdir=%T/opaque_ptr_cmpxchg_keys.ll
; RUN: cat %T/opaque_ptr_cmpxchg_keys.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-KEY
; RUN: cat %T/opaque_ptr_cmpxchg_keys.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-JOIN
; CHECK-KEY: ptr<i32>
; CHECK-JOIN-NOT: <s1>

target datalayout = "e-m:e-p270:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

declare void @sink(ptr)

define void @main(ptr %v, ptr %n) {
entry:
  %g = getelementptr i32, ptr %v, i64 1
  store i32 7, ptr %g, align 4
  %slot = alloca ptr, align 8
  store ptr %v, ptr %slot, align 8
  %res = cmpxchg ptr %slot, ptr %v, ptr %n monotonic monotonic
  %old = extractvalue { ptr, i1 } %res, 0
  call void @sink(ptr %old)
  ret void
}

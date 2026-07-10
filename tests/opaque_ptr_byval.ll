; The byval(T) parameter attribute kept its pointee type through the opaque
; pointer transition and is used as inference evidence: storing a
; byval(%struct.Pair) argument keys the field as a pointer to Pair's first
; primitive type (i32), not as a bare ptr.
;
; RUN: %seadsa %s %ci_dsa --sea-dsa-dot --sea-dsa-type-aware=true --sea-dsa-dot-outdir=%T/opaque_ptr_byval.ll
; RUN: cat %T/opaque_ptr_byval.ll/main.mem.dot | OutputCheck %s -d --comment=";"
; CHECK: ptr<i32>

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.Pair = type { i32, i32 }

define void @main(ptr %slot, ptr byval(%struct.Pair) %p) {
entry:
  store ptr %p, ptr %slot, align 8
  ret void
}

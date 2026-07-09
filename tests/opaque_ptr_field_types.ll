; RUN: %seadsa %s %ci_dsa --sea-dsa-dot --sea-dsa-type-aware=true --sea-dsa-dot-outdir=%T/opaque_ptr_field_types.ll
; RUN: cat %T/opaque_ptr_field_types.ll/main.mem.dot | OutputCheck %s -d --comment=";"
; CHECK: ptr<i32>
; CHECK: ptr<double>

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define void @main(ptr %a, ptr %b) {
entry:
  %slot_a = alloca ptr, align 8
  %slot_b = alloca ptr, align 8
  store ptr %a, ptr %slot_a, align 8
  store ptr %b, ptr %slot_b, align 8
  %pa = load ptr, ptr %slot_a, align 8
  %pb = load ptr, ptr %slot_b, align 8
  %ia = getelementptr i32, ptr %pa, i64 1
  %db = getelementptr double, ptr %pb, i64 1
  store i32 0, ptr %ia, align 4
  store double 0.0, ptr %db, align 8
  ret void
}

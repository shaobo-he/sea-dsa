; Regression test for severed points-to links under pointee inference: the
; store site of a slot has pointee evidence (gep i32 on the stored value in
; @store_it) while the load site (@load_it) has none. The two sites must
; resolve to the SAME link -- a pointee-less ptr field joins pointee-typed
; pointer links at the same offset (Node::addLink) -- so the loaded value
; aliases the stored one. A severed graph shows up as a node with two
; parallel links at offset 0, i.e. a record with an <s1> port.
;
; RUN: %seadsa %s %butd_dsa --sea-dsa-dot --sea-dsa-type-aware=true --sea-dsa-dot-outdir=%T/opaque_ptr_consistent_keys.ll
; RUN: cat %T/opaque_ptr_consistent_keys.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-LINK
; RUN: cat %T/opaque_ptr_consistent_keys.ll/main.mem.dot | OutputCheck %s -d --comment=";" --check-prefix=CHECK-JOIN
; CHECK-LINK: 0, ptr
; CHECK-JOIN-NOT: <s1>

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define void @store_it(ptr %slot, ptr %v) {
entry:
  %g = getelementptr i32, ptr %v, i64 1
  store i32 7, ptr %g, align 4
  store ptr %v, ptr %slot, align 8
  ret void
}

declare void @sink(ptr)

define ptr @load_it(ptr %slot) {
entry:
  %w = load ptr, ptr %slot, align 8
  ret ptr %w
}

define void @main() {
entry:
  %slot = alloca ptr, align 8
  %obj = alloca i32, align 4
  call void @store_it(ptr %slot, ptr %obj)
  %r = call ptr @load_it(ptr %slot)
  call void @sink(ptr %r)
  ret void
}

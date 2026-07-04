; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-allow-explicit-collapse --sea-dsa-wf-max-collapsed-live-nodes=1 --sea-dsa-wf-max-collapsed-access-ratio=0 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: accesses through collapsed nodes 1
; CHECK: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry(i32* %p) {
bb:
  %q = bitcast i32* %p to i8*
  call void @sea_dsa_collapse(i8* %q)
  store i32 0, i32* %p, align 4
  ret void
}

declare void @sea_dsa_collapse(i8*)

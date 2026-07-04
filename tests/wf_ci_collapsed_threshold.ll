; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-allow-explicit-collapse --sea-dsa-wf-max-collapsed-access-ratio=100 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=REJECT
; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-report --sea-dsa-wf-allow-explicit-collapse --sea-dsa-wf-max-collapsed-live-nodes=1 --sea-dsa-wf-max-collapsed-access-ratio=100 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ALLOW
; REJECT: collapsed live nodes 1
; REJECT: SeaDsa well-formedness check failed
; ALLOW: collapsed live nodes 1
; ALLOW-NOT: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = alloca i32, align 4
  store i32 0, i32* %p, align 4
  %q = bitcast i32* %p to i8*
  call void @sea_dsa_collapse(i8* %q)
  ret void
}

declare void @sea_dsa_collapse(i8*)

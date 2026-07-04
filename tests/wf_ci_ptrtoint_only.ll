; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-report 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: live inttoptr nodes 0
; CHECK-NOT: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry(i64* %out) {
bb:
  %p = alloca i32, align 4
  store i32 0, i32* %p, align 4
  %i = ptrtoint i32* %p to i64
  store i64 %i, i64* %out, align 8
  ret void
}

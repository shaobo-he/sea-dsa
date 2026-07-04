; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-report 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: live inttoptr nodes 0
; CHECK-NOT: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = alloca i32, align 4
  %base = ptrtoint i32* %p to i64
  %addr = add i64 %base, 0
  %q = inttoptr i64 %addr to i32*
  store i32 7, i32* %q, align 4
  ret void
}

; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: === SeaDsa well-formedness report ===
; CHECK: live inttoptr nodes 1
; CHECK: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry(i64 %addr) {
bb:
  %p = inttoptr i64 %addr to i32*
  store i32 0, i32* %p, align 4
  ret void
}

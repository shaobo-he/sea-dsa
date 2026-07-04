; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-report 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: === SeaDsa well-formedness report ===
; CHECK: live nodes
; CHECK: live inttoptr nodes 0

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = alloca i32, align 4
  store i32 0, i32* %p, align 4
  ret void
}

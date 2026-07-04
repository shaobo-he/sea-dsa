; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: live external nodes 1
; CHECK: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = call i32* @unknown_ptr()
  store i32 0, i32* %p, align 4
  ret void
}

declare i32* @unknown_ptr()

; RUN: not %seadsa %s %cs_dsa --sea-dsa-wf-strict 2>&1 | OutputCheck %s -d --comment=";"
; CHECK: SeaDsa well-formedness checking currently supports only context-insensitive analysis

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = alloca i32, align 4
  store i32 0, i32* %p, align 4
  ret void
}

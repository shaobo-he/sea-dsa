; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=REJECT
; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-report --sea-dsa-wf-allow-constant-addresses 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ALLOW
; REJECT: live inttoptr nodes 1
; REJECT: SeaDsa well-formedness check failed
; ALLOW: live inttoptr nodes 1
; ALLOW: rejected non-constant-address inttoptr nodes 0
; ALLOW-NOT: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = inttoptr i64 4096 to i32*
  store i32 0, i32* %p, align 4
  ret void
}

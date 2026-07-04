; RUN: not %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-max-alloc-sites-per-node=1 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=REJECT
; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-strict --sea-dsa-wf-report --sea-dsa-wf-max-alloc-sites-per-node=2 2>&1 | OutputCheck %s -d --comment=";" --check-prefix=ALLOW
; REJECT: max allocation sites in a live non-inttoptr node 2
; REJECT: SeaDsa well-formedness check failed
; ALLOW: max allocation sites in a live non-inttoptr node 2
; ALLOW-NOT: SeaDsa well-formedness check failed

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry(i1 %c) {
bb:
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  br i1 %c, label %left, label %right

left:
  br label %join

right:
  br label %join

join:
  %p = phi i32* [ %a, %left ], [ %b, %right ]
  store i32 0, i32* %p, align 4
  ret void
}

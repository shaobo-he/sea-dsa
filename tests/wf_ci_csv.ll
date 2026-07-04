; RUN: %seadsa %s %ci_dsa --sea-dsa-wf-to-csv=%T/wf_ci_csv.csv
; RUN: OutputCheck %s --file-to-check=%T/wf_ci_csv.csv --comment=";" --check-prefix=CSV
; CSV: metric,value
; CSV: live_nodes,
; CSV: collapsed_live_nodes,0
; CSV: inttoptr_live_nodes,0
; CSV: max_alloc_sites_per_live_non_inttoptr_node,1

target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @entry() {
bb:
  %p = alloca i32, align 4
  store i32 0, i32* %p, align 4
  ret void
}

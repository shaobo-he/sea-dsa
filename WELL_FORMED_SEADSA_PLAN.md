# Well-Formed SeaDsa Plan

## Goal

Build a stricter, simpler operating mode for SeaDsa that accepts a broad
subset of real C/C++ programs while rejecting inputs whose SeaDsa graphs lose
too much precision.

The mode should not reject programs merely because Clang/LLVM emits
`ptrtoint`, `inttoptr`, integer stores of pointers, or ABI-related pointer
constants. Instead, it should reject only cases where a dereferenceable pointer
has unknown or ambiguous provenance, or where the final DSA graph is too
collapsed to be useful.

The intended result is:

- most ordinary C/C++ code still compiles and analyzes,
- benign pointer-as-integer idioms are accepted or rewritten,
- unsupported low-level pointer tricks fail with clear diagnostics,
- clients can rely on accepted DSA graphs having bounded imprecision.

## Core Idea

Define the accepted subset semantically, by pointer provenance and graph
quality, rather than syntactically banning every suspicious LLVM instruction.

A program is accepted if every dereferenceable pointer value comes from one of
these sources:

- allocation sites, globals, or function arguments,
- normal pointer operations such as bitcast, GEP, PHI, select, and return,
- recognized allocation or library wrappers,
- a pointer-integer round trip whose original pointer provenance is preserved,
- a recognized external specification.

A program is rejected if a dereferenceable pointer is created from arbitrary
integer data, mixed unrelated pointer provenance, unknown external behavior, or
another source that SeaDsa cannot model without unsoundly inventing memory or
collapsing large graph regions.

## Accepted Cases

Design principle: `ptrtoint` is always accepted, including when the integer
escapes into calls or memory. Strictness is enforced at the `inttoptr`
re-entry point instead. This avoids classifying callees (hashing, logging,
diagnostics) entirely: an escaped pointer-integer is harmless unless something
turns it back into a dereferenceable pointer, and every such re-entry is
policed by the provenance and graph checks below. The existing
`isEscapingPtrToInt` marking stays as informational metadata.

The strict mode should accept these patterns by default:

- any `ptrtoint`, whether used for comparison, subtraction, branching, or
  escaping into calls and memory.
- `ptrtoint(p) + constant` followed by `inttoptr` when it is equivalent to a
  fixed-offset GEP. (Already modeled precisely by `isFixedOffset` in
  `DsaLocal.cc` and rewritten by `RemovePtrToInt`; the work here is tests and
  extension, not new modeling.)
- PHI/select over pointer-derived integers when all incoming values preserve
  compatible pointer provenance.
- integer stores/loads that are really pointer stores/loads through bitcasted
  memory, when they can be rewritten back to pointer operations.
- simple pointer tagging when tags are removed before dereference.
- C++ ABI patterns such as vtables, RTTI, and constant expressions, provided
  they do not create ordinary dereferenceable pointers from unknown integers.

## Rejected Cases

The strict mode should reject these patterns:

- `inttoptr` from arbitrary integers, including constant literal addresses
  (memory-mapped I/O such as `(int *)0x40001000`) unless
  `--sea-dsa-wf-allow-constant-addresses` is given. Constant addresses are
  common and legitimate in embedded code, so the diagnostic must name the
  constant and suggest the flag.
- `inttoptr` from arithmetic that combines unrelated pointers.
- pointer tags that are not removed before dereference.
- pointer values recovered from unknown integer memory.
- unknown external calls that return integers later cast to pointers.
- explicit `sea_dsa_collapse` in specs unless the user opts into allowing it.
- unresolved indirect calls, if the selected policy requires precise call graph
  resolution. Detection should reuse the existing `CompleteCallGraph`
  analysis, which already tracks whether each callsite was fully resolved.
- final DSA graphs whose live memory nodes exceed configured collapse or
  sharing thresholds.

## Analysis Pipeline

1. Canonicalize the module.
   - Run the existing `RemovePtrToInt` pass.
   - Add targeted rewrites for additional pointer-integer round trips.
   - Prefer rewriting compiler artifacts to pointer operations when safe.

2. Validate pointer provenance (best-effort, pre-DSA).
   - Track pointer-derived integer values intraprocedurally.
   - Permit integers that are only observed as integers.
   - Reject `inttoptr` whose source is locally visible and clearly unknown,
     for fast failure with precise diagnostics.
   - This phase cannot be authoritative: provenance that flows through memory
     or across functions (a `ptrtoint` stored in one place, loaded and cast
     back elsewhere) needs points-to information, which is what SeaDsa
     computes. Those cases are caught in step 4, where a live
     `inttoptr`-flagged node is exactly a pointer recovered from integer data
     the analysis could not trace.

3. Run SeaDsa normally.
   - Reuse the existing local, bottom-up, top-down, and context-sensitive graph
     construction.
   - Avoid forking the core graph algorithm unless later evidence shows it is
     necessary.

4. Check graph quality (authoritative).
   - Check the graphs the client actually consumes: the top-down per-function
     graphs under the context-sensitive analysis, or the global graph under
     the context-insensitive one.
   - Count live offset-collapsed nodes; when `sea-dsa-type-aware` is on,
     count type-collapsed nodes as well.
   - Count memory accesses through collapsed nodes.
   - Count allocation sites per node, excluding the synthetic allocation
     sites that `visitCastIntToPtr` attaches to fresh `inttoptr` nodes.
   - Reject live nodes marked `inttoptr`, `external`, or `unknown` per
     policy; report `incomplete` nodes. `ptrtoint` marks are informational
     only, per the design principle above.
   - Reject graphs that violate the configured policy.

5. Report actionable diagnostics.
   - Explain the rejected IR pattern or graph property.
   - Include function and instruction locations when available.
   - Summarize the worst collapsed nodes and allocation-site sharing.

## Proposed Options

- `--sea-dsa-wf-report`
  - Run the well-formedness checks and print diagnostics, but do not fail.

- `--sea-dsa-wf-strict`
  - Reject unsupported provenance patterns and graph-quality violations.

- `--sea-dsa-wf-allow-explicit-collapse`
  - Permit `sea_dsa_collapse` in external specs.

- `--sea-dsa-wf-max-collapsed-live-nodes=N`
  - Reject if more than `N` live nodes are offset-collapsed.

- `--sea-dsa-wf-max-collapsed-access-ratio=P`
  - Reject if more than `P` percent of memory accesses go through collapsed
    nodes.

- `--sea-dsa-wf-max-alloc-sites-per-node=N`
  - Reject if any live node merges more than `N` allocation sites.

- `--sea-dsa-wf-allow-constant-addresses`
  - Accept `inttoptr` of integer constant literals (memory-mapped I/O).

- `--sea-dsa-wf-to-csv=FILE`
  - Emit the well-formedness metrics in machine-readable form, following the
    existing `sea-dsa-to-csv` precedent, so benchmark runs can be classified
    automatically.

## Graph Quality Policy

Start with a conservative default policy:

- reject any live offset-collapsed node,
- reject any live unknown-provenance `inttoptr` node,
- reject any live external/unknown node created by unsupported fallback logic,
- report, but do not initially reject, high allocation-site sharing.

After collecting benchmark data, relax the default to thresholds if needed.

### Defining "live"

`DsaInfo` has two non-equivalent notions of liveness, and they diverge:

- node flags: `isRead() || isModified()`, which can become set purely through
  unification even when no instruction accesses the node,
- counted accesses: per-node counts of syntactic loads, stores, and memory
  intrinsics (`memcpy`, `memmove`, `memset`).

The strict default policy uses the node flags, which are more conservative
and catch unification-induced liveness. The threshold metrics
(`--sea-dsa-wf-max-collapsed-access-ratio`) use counted accesses, since a
ratio needs a concrete denominator.

Important distinction:

- A dead collapsed node may be harmless.
- A live collapsed node used by loads, stores, memcpy, memset, or memmove is a
  direct precision problem.
- A collapsed node reachable from a public argument, return value, or global is
  higher risk than an isolated local temporary.

## Implementation Sketch

The current SeaDsa code already has useful hooks:

- `Node::collapseOffsets(int tag)` (`Graph.hh:774`, `Graph.cc:263`) is the
  central offset-collapse operation; all call sites currently pass `__LINE__`.
- `Node::unifyAt` triggers many implicit collapses (`Graph.cc:482-545`).
- `BlockBuilderBase::visitCastIntToPtr` (`DsaLocal.cc:1703`) creates fresh
  abstract memory for unknown `inttoptr`, marks it `setAlloca()`, and attaches
  a synthetic allocation site (which the quality checker must exclude from
  alloc-site metrics). It already models `ptrtoint(p) + constant` round trips
  precisely via `isFixedOffset`. It is also invoked for `inttoptr` constant
  expressions, which is where constant-address handling plugs in.
- `IntraBlockBuilder::visitPtrToIntInst` (`DsaLocal.cc:1820`) marks escaping
  `ptrtoint` via `isEscapingPtrToInt`, which already treats comparison,
  branching, and pointer-subtraction uses as non-escaping.
- `DsaInfo` already identifies live/read/modified nodes and counts memory
  accesses (loads, stores, and memory intrinsics).
- `DsaStats` already reports offset-collapsed node counts.
- `CompleteCallGraph` already tracks whether each indirect callsite was fully
  resolved; the indirect-call policy should consume it rather than add new
  detection.

The build targets LLVM 14 with typed pointers, so the bitcast-based rewrites
in `RemovePtrToInt` remain viable as-is; an opaque-pointer migration would
require revisiting them.

Suggested implementation steps:

1. Add a report-only module pass for well-formedness diagnostics.
2. Extend or wrap `RemovePtrToInt` with provenance-aware validation.
3. Change unknown `inttoptr` handling in strict mode from "fresh region plus
   warning" to "diagnostic plus rejection."
4. Add a post-DSA quality checker using `DsaInfo`.
5. Replace integer collapse tags with structured collapse reasons.
6. Add lit tests for accepted rewrites, rejected provenance, and rejected graph
   collapse.

## Collapse Reason Tracking

`collapseOffsets(int tag)` currently receives line-number style debug tags.
For strict diagnostics, replace or supplement this with a structured reason:

- explicit spec collapse,
- self-unification at non-zero offset,
- merge with already collapsed node,
- array/non-array conflict,
- incompatible array sizes,
- array merge at non-zero offset,
- growth conflict,
- unknown external memory model,
- unsupported pointer-integer provenance.

Attribution has limits: many collapses fire deep inside unification cascades
in `Graph.cc` during bottom-up/top-down cloning, far from any instruction,
and the proximate trigger is often in a different function than the root
cause. The committed scope is a reason enum, plus the owning graph/function,
plus the triggering `Value`/`DebugLoc` when the local builder has one —
threaded through a "current context" object set by the builders. The
`__LINE__` tag stays as a secondary debug field. Full causal chains are out
of scope.

Each rejected graph should be able to answer:

- why did this node collapse,
- which graph and function it collapsed in,
- which value or instruction triggered it, when known,
- how many memory accesses are affected.

## Milestones

### Milestone 1: Measurement

- Add `--sea-dsa-wf-report`.
- Print unsupported pointer-integer patterns.
- Print live collapsed node counts and collapsed memory-access counts.
- Emit the same metrics as CSV (`--sea-dsa-wf-to-csv`) so Milestone 4 can
  classify benchmark results automatically.
- Do not reject anything yet.

### Milestone 2: Strict Provenance

- Add `--sea-dsa-wf-strict`.
- Reject unknown-provenance `inttoptr`.
- Keep accepting all `ptrtoint` uses, per the design principle.
- Add tests for common accepted C/C++ idioms, including the fixed-offset
  round trips already handled by `RemovePtrToInt` and `isFixedOffset`.

### Milestone 3: Graph Rejection

- Reject graphs with live collapsed nodes under the initial strict policy.
- Add configurable thresholds.
- Print concise diagnostics for the worst offending nodes.

### Milestone 4: Benchmark Tuning

- Run representative C/C++ benchmarks.
- Classify false rejections.
- Add safe rewrites for common compiler idioms.
- Relax thresholds only when the resulting graphs remain useful.

## Open Questions

- Should explicit `sea_dsa_collapse` be forbidden by default, or allowed for
  trusted specs only?
- Should unresolved indirect calls (as reported by `CompleteCallGraph`) be
  rejected, or treated as a separate call graph precision policy?
- What is the first target client: alias analysis, memory instrumentation,
  verification condition generation, or graph visualization?
- Should the default strict policy reject any live collapse, or start with a
  small threshold?
- How much C++ ABI-specific handling should live in this mode versus in a
  preprocessing/canonicalization pass?

# Shift-by-1 Evaluation

This note captures the concrete evaluation setup for the first pass of
`ROWSHL1` / `ROWSHR1` benchmarking before extending to arbitrary shift amounts.

## Correctness Cases

The shift-by-1 benchmarks should validate the following input patterns for both
left and right shifts:

- `0` (`SHIFT_PATTERN_RANDOM`): random data for a realistic steady-state case
- `1` (`SHIFT_PATTERN_ZERO`): all-zero input, expected output remains zero
- `2` (`SHIFT_PATTERN_ONES`): all-one input, checks zero-fill semantics at the
  shifted edge
- `3` (`SHIFT_PATTERN_ALTERNATING`): alternating bits, exposes directionality
  mistakes
- `4` (`SHIFT_PATTERN_BOUNDARY`): set bits around a 64-bit boundary to confirm
  carry propagation across adjacent words in the row

The software reference should follow the current functional simulator semantics
in [`gem5/src/mem/abstract_mem.cc`] where a row is shifted logically as a
single 8192-byte stream with carry propagation across ascending-address bytes.

## Targeted Microbenchmarks

The following workloads are used for the first shift evaluation pass:

- [`microworkloads/93_shift_left_1-baseline.c`]
- [`microworkloads/94_shift_right_1-baseline.c`]
- [`microworkloads/93_shift_left_1-plus.c`]
- [`microworkloads/94_shift_right_1-plus.c`]
- [`microworkloads/95_shift_chain_1-baseline.c`]
- [`microworkloads/95_shift_chain_1-plus.c`]

Workload structure:

- baseline variants use CPU software shifts over the same per-column byte
  buffers
- plus variants invoke `rowop_shl1` / `rowop_shr1` directly
- all workloads keep the existing CLI shape:
  - `argv[1]`: logical column width in bits (`8, 16, 32, 64`)
  - `argv[2]`: size exponent where `num_vals = 1024 << k`
  - `argv[3]` optional: pattern selector
  - `argv[4]` optional in `95_shift_chain_1-plus.c`: repeated shift count

All benchmarks:

- call `m5_reset_stats(0,0)` before the measured region
- call `m5_dump_stats(0,0)` after the measured region
- print a checksum and mismatch count after execution to combine performance
  measurement with functional validation

## Measurement Matrix

The first evaluation pass should sweep:

- width: `8, 16, 32, 64`
- size exponent `k`: `0, 1, 2, 3`
- pattern: `0, 1, 2, 3, 4`
- direction: left and right

For repeated-shift scaling:

- use `95_shift_chain_1-plus.c`
- sweep repeats: `1, 2, 4, 8, 16`
- start with pattern `0` and width `32`, then expand to other widths if needed

This gives a compact matrix that covers both correctness and timing without
requiring a full workload suite.

## Measured Results

The DRAM timing path in [`gem5/src/mem/dram_interface.cc`] currently models
`ROWSHL1` and `ROWSHR1` as four serialized internal steps:

- two `issue_ap(...)` operations on the source row
- two `issue_ap(...)` operations on the destination row

The first measurement pass produced the following trends:

- single-shift PIM is slower than the CPU baseline for every measured left and
  right shift point in the `width in {8,16,32,64}` and `size_exp in {0,1,2,3}`
  sweep
- the slowdown is largest at the smallest problem sizes and narrows as the
  working set grows
- left-shift speedup ranges from about `0.86` to `0.99`
- right-shift speedup ranges from about `0.86` to `0.99`
- the largest single-shift cases are close to break-even, but still slightly
  favor the baseline
- repeated chain shifts show a crossover where PIM becomes beneficial once the
  repeated in-DRAM operation cost is amortized across enough shifts and enough
  data
- for the 32-bit chain benchmark:
  - `size_exp=0` remains slower than baseline across all tested repeat counts
  - `size_exp=1` reaches slight PIM speedup only at `repeats=16`
  - `size_exp=2` exceeds `1.0x` speedup starting at `repeats=4` and reaches
    about `1.24x` at `repeats=16`
  - `size_exp=3` exceeds `1.0x` speedup starting at `repeats=2` and reaches
    about `1.40x` at `repeats=16`

These results are consistent with the current timing model:

- a single `ROWSHL1` / `ROWSHR1` pays four serialized internal DRAM steps, so
  one isolated shift does not beat a short CPU software loop
- chained shifts benefit from keeping the work inside DRAM and reusing the same
  row-oriented execution path repeatedly
- the shift path does not behave like a normal read/write burst because the
  model returns the shift completion tick directly instead of adding external
  burst transfer timing

## Reportable Outputs

The first report should include:

1. Correctness table
   - benchmark
   - width
   - pattern
   - mismatches
   - checksum

2. Baseline vs PIM single-shift figure
   - x-axis: problem size (`1024 << k`)
  - y-axis: speedup (`baseline / PIM`)
  - series: left-shift widths and right-shift widths
  - key takeaway: PIM is below `1.0x` for all single-shift points, but trends
    upward with size

3. Repeated-shift scaling figure
   - x-axis: repeat count
  - y-axis: speedup (`baseline / PIM`)
  - series: chain-shift size exponents at fixed width
  - key takeaway: larger chain workloads cross above `1.0x`, showing positive
    PIM speedup for repeated shifts

4. DRAM timing discussion
  - short narrative connecting single-shift slowdown and chain-shift crossover
    to the four-step shift timing model

These outputs justify two conclusions for the current prototype:

- isolated shift-by-1 is not yet a performance win over baseline software
- repeated shift-by-1 can become a performance win, which supports compiling
  larger logical shifts into chains of DRAM-resident 1-bit shifts

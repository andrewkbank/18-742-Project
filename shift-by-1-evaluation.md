# Shift Evaluation

This note captures the evaluation setup for row-op shifts after extending the
prototype beyond `ROWSHL1` / `ROWSHR1` to include:

- direct shift-by-1
- direct shift-by-8
- arbitrary constant shifts lowered into `(N / 8)` shift-by-8 operations plus
  `(N % 8)` shift-by-1 operations

## Correctness Cases

The shift benchmarks should validate the following input patterns for both left
and right shifts:

- `0` (`SHIFT_PATTERN_RANDOM`): random data for a realistic steady-state case
- `1` (`SHIFT_PATTERN_ZERO`): all-zero input, expected output remains zero
- `2` (`SHIFT_PATTERN_ONES`): all-one input, checks zero-fill semantics at the
  shifted edge
- `3` (`SHIFT_PATTERN_ALTERNATING`): alternating bits, exposes directionality
  mistakes
- `4` (`SHIFT_PATTERN_BOUNDARY`): set bits around a 64-bit boundary to confirm
  carry propagation across adjacent words in the row

The software reference should follow the functional simulator semantics in
[`gem5/src/mem/abstract_mem.cc`] where a row is shifted logically as a single
8192-byte stream with carry propagation across ascending-address bytes.

For arbitrary constant shifts, correctness should be checked against a software
reference that applies the same decomposition rule used by the compiler and PIM
benchmarks:

- `N / 8` applications of the byte-shift primitive
- `N % 8` applications of the 1-bit shift primitive

## Targeted Microbenchmarks

The following workloads cover the current shift feature set:

- [`microworkloads/93_shift_left_1-baseline.c`]
- [`microworkloads/94_shift_right_1-baseline.c`]
- [`microworkloads/93_shift_left_1-plus.c`]
- [`microworkloads/94_shift_right_1-plus.c`]
- [`microworkloads/95_shift_chain_1-baseline.c`]
- [`microworkloads/95_shift_chain_1-plus.c`]
- [`microworkloads/96_shift_left_8-baseline.c`]
- [`microworkloads/96_shift_left_8-plus.c`]
- [`microworkloads/97_shift_right_8-baseline.c`]
- [`microworkloads/97_shift_right_8-plus.c`]
- [`microworkloads/98_shift_left_arbitrary-baseline.c`]
- [`microworkloads/98_shift_left_arbitrary-plus.c`]
- [`microworkloads/99_shift_right_arbitrary-baseline.c`]
- [`microworkloads/99_shift_right_arbitrary-plus.c`]

Workload structure:

- baseline variants use CPU software shifts over the same per-column byte
  buffers
- plus variants invoke `rowop_shl1` / `rowop_shr1` / `rowop_shl8` /
  `rowop_shr8` directly
- all workloads keep the existing CLI shape:
  - `argv[1]`: logical column width in bits (`8, 16, 32, 64`)
  - `argv[2]`: size exponent where `num_vals = 1024 << k`
  - `argv[3]` optional: pattern selector
  - `argv[4]` optional in chain and arbitrary-shift workloads:
    - repeated shift count for `95_shift_chain_1-*`
    - logical shift amount for `98_shift_left_arbitrary-*` and
      `99_shift_right_arbitrary-*`

All benchmarks:

- call `m5_reset_stats(0,0)` before the measured region
- call `m5_dump_stats(0,0)` after the measured region
- print a checksum and mismatch count after execution to combine performance
  measurement with functional validation

## Measurement Matrix

The full shift evaluation should sweep:

- width: `8, 16, 32, 64`
- size exponent `k`: `0, 1, 2, 3`
- pattern: `0, 1, 2, 3, 4`
- direction: left and right

For repeated shift-by-1 scaling:

- use `95_shift_chain_1-plus.c`
- sweep repeats: `1, 2, 4, 8, 16`
- start with pattern `0` and width `32`, then expand to other widths if needed

For direct shift-by-8:

- use `96_shift_left_8-*` and `97_shift_right_8-*`
- reuse the same width / size / pattern grid as shift-by-1

For arbitrary constant shifts:

- use `98_shift_left_arbitrary-*` and `99_shift_right_arbitrary-*`
- sweep representative shift amounts such as `1, 8, 13, 24`
- start with pattern `0` and width `32`, then expand once the basic crossover
  behavior is understood

This keeps the matrix compact while still isolating primitive correctness,
decomposition correctness, and scaling behavior.

## Measured Results

The DRAM timing path in [`gem5/src/mem/dram_interface.cc`] currently models
the shift primitives as serialized internal row operations. In the current
prototype:

- `ROWSHL1` / `ROWSHR1` use four internal steps
- `ROWSHL8` / `ROWSHR8` currently reuse the same internal timing shape while
  exercising different functional wiring semantics

The measured shift-by-1 results from the first pass remain:

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

The new shift-by-8 and arbitrary-shift benchmarks are intended to answer the
next question: how much of that single-shift overhead can be amortized by
replacing eight separate bit shifts with one byte-shift primitive? The expected
trend is:

- direct shift-by-8 should improve over an equivalent chain of eight shift-by-1
  operations
- arbitrary shifts with larger `N / 8` components should move the crossover
  point closer to smaller problem sizes and smaller repeat counts
- shifts with small remainders should look more like the shift-by-8 primitive
  than the shift-by-1 primitive

## Reportable Outputs

The shift report should include:

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
   - key takeaway: PIM is below `1.0x` for all shift-by-1 single-shift points,
     but trends upward with size

3. Direct shift-by-8 figure
   - x-axis: problem size (`1024 << k`)
   - y-axis: speedup (`baseline / PIM`)
   - series: left-shift widths and right-shift widths
   - key takeaway: compare the byte-shift primitive against the shift-by-1
     primitive at the same workload sizes

4. Repeated-shift scaling figure
   - x-axis: repeat count
   - y-axis: speedup (`baseline / PIM`)
   - series: chain-shift size exponents at fixed width
   - key takeaway: larger chain workloads cross above `1.0x`, showing positive
     PIM speedup for repeated shifts

5. Arbitrary-shift decomposition figure
   - x-axis: logical shift amount
   - y-axis: speedup (`baseline / PIM`)
   - series: size exponents at fixed width
   - key takeaway: identify where replacing long bit-shift chains with
     shift-by-8 primitives materially changes the crossover

6. DRAM timing discussion
   - short narrative connecting single-shift slowdown, direct shift-by-8, and
     arbitrary-shift crossover behavior to the current internal timing model

These outputs justify the current prototype direction:

- isolated shift-by-1 is not yet a performance win over baseline software
- repeated shift-by-1 can become a performance win
- adding a direct shift-by-8 primitive is the next lever for reducing the cost
  of larger logical shifts
- arbitrary constant shifts should therefore be evaluated as mixed chains of
  `ROWSHL8` / `ROWSHR8` and `ROWSHL1` / `ROWSHR1`

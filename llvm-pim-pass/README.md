# PIM Vectorization Pass

This is an LLVM pass that converts vectorizable instructions into their PIM
equivalent which can be used by the gem5 simulator.

The modernized simulator path currently supports the row-op opcodes used by
`rowop.S`. The current shift-related opcode assignments are:

- `0x66`: `ROWSHL1`
- `0x67`: `ROWSHR1`
- `0x68`: `ROWSHL8`
- `0x69`: `ROWSHR8`

If you extend the pass to emit additional custom opcodes, make sure the gem5
x86 decoder and memory model are updated in lockstep.

## Shift Lowering

The pass lowers only constant logical shifts today.

- `shl <const 1>` lowers to `__llvm_PIM_shl1`
- `lshr <const 1>` lowers to `__llvm_PIM_shr1`
- `shl <const 8>` lowers to `__llvm_PIM_shl8`
- `lshr <const 8>` lowers to `__llvm_PIM_shr8`
- `shl <const N>` lowers to `floor(N / 8)` shift-by-8 calls plus `N % 8`
  shift-by-1 calls
- `lshr <const N>` lowers to `floor(N / 8)` shift-by-8 calls plus `N % 8`
  shift-by-1 calls

Non-constant shifts remain unsupported by the PIM lowering and fall back to the
baseline CPU path.

Build:

    $ mkdir cmake-build-debug
    $ cd cmake-build-debug
    $ cmake ..
    $ make
    $ cd ..

Run:

    $ cd workloads
    $ make {WORKLOAD}

To compile a custom workload, look at `workloads/Makefile` to create a custom
makefile. Applications have to be linked before the pass is run.

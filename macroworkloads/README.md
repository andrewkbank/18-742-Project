# Macroworkloads

This directory contains larger, more complex workloads designed to evaluate the real-world utility and performance of the shift-by-1 DRAM architecture. While the `microworkloads` evaluate primitive operations (e.g., addition, simple shifting, boolean operations), these macroworkloads compose those primitives to execute higher-level algorithms.

## Current Workloads

### 00_bitweave_range_queries
This workload implements a range query (e.g., $A < B$) using a **parallel prefix-sum algorithm** applied to a horizontal data layout.

#### Motivation
Traditional bit-serial layouts (like Ambit) perform equality predicates efficiently, but require $O(N)$ cycles to evaluate range predicates ($<$ or $>$), where $N$ is the bit-width of the data (e.g., 32 cycles for 32-bit integers).

#### Implementation Details
By storing words horizontally (e.g., standard little-endian arrays of 32-bit integers) and taking advantage of the `ROWSHL1` (shift row left by 1 bit) hardware capability, we can drastically accelerate this operation:
1. We compute initial logical conditions simultaneously for all bits:
   - `L = ~A & B`
   - `E = ~(A ^ B)`
2. We treat the row as a Carry-Lookahead Adder (CLA) applied to logical comparisons.
3. We perform parallel prefix propagation using vector shifts. Since bits of the same word are stored adjacently, a vector shift of 1, 2, 4, 8, and 16 bits combined with logical AND/OR can propagate the match mask across the entire 32-bit word.
4. This reduces the time complexity from $O(N)$ cycles down to $O(\log N)$ cycles (e.g., 5 stages for 32-bit integers).

#### Files Included
- `00_bitweave_range_queries-baseline.c`: A purely scalar/C-vector implementation. This serves as the target for the `llvm-pim-pass` to automatically vectorize and lower into our custom PIM ISA. It is also used to validate correctness against standard scalar operations.
- `00_bitweave_range_queries-plus.c`: A manual implementation using `mimdram.h` intrinsic macros. This models the exact `m5op` commands (`rowop_and`, `rowop_or`, `rowop_not`, `rowop_shl1`, `rowop_shl8`) sent to the `gem5` memory controller to execute the operations directly in DRAM.

## Building and Running
The build process exactly mirrors the microworkloads. Assuming your environment is set up:
```bash
make
```
This will compile the `*.exe` binaries, which can then be executed inside the `gem5` simulation environment.

### 01_convolution
This workload implements a simple 1D convolution (e.g., a 3-element sliding window blur) using bit-serial arithmetic and horizontal shifts to eliminate software Transpose-and-Align operations.

#### Motivation
In a Convolutional Neural Network (CNN) or basic image processing, a core operation is the 2D or 1D stencil (sliding window). If pixels are stored in a bit-serial vertical layout (like Ambit), adjacent pixels reside in different bitlines. Baseline Ambit requires a costly "Transpose-and-Align" step via software or complex routing to align neighboring pixels into the same vertical column before they can be added.

#### Implementation Details
We leverage the hardware's `ROWSHL1` and `ROWSHR1` (shift row left/right by 1 bit) instructions to physically move the entire bit-planes.
1. By shifting the entire memory array of bit-planes to the left by 1 bit, we perfectly align the data of pixel $x+1$ into the sense amplifiers of pixel $x$.
2. By shifting the bit-planes to the right by 1 bit, we align pixel $x-1$ into pixel $x$.
3. With the neighboring pixels now occupying the exact same vertical bitlines as the target pixel, we can issue a single sequence of native Ambit TRAs (Triple Row Activations) to compute the addition ($x-1 + x + x+1$) entirely within DRAM.
4. This achieves a parallel-sliding-window effect, where an entire DRAM row (e.g., 8,192 pixels) is convolved simultaneously in memory without ANY data movement to the CPU.

#### Files Included
- `01_convolution-baseline.c`: A purely scalar C implementation looping over an array to compute the sliding window sum. This acts as the standard target for the `llvm-pim-pass` to vectorize and compile into shift+add sequences.
- `01_convolution-plus.c`: A manual intrinsic-based implementation simulating the PIM execution. It uses `rowop_shl1`/`rowop_shr1` to align the bit-planes, followed by Ambit `AAP_VECTORS` logic to perform the bit-serial addition.

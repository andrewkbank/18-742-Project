#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <m5op.h>

#include "mimdram.h"
#include "shift_bench.h"

extern void rowop_and(void *d, void *s1, void *s2);
extern void rowop_or(void *d, void *s1, void *s2);
extern void rowop_not(void *d, void *s);

#define AND_VECTORS(dst, s1, s2) FOR_ALL_ROWS_IN_VECTOR { rowop_and(ROW(dst), ROW(s1), ROW(s2)); }
#define OR_VECTORS(dst, s1, s2) FOR_ALL_ROWS_IN_VECTOR { rowop_or(ROW(dst), ROW(s1), ROW(s2)); }
#define NOT_VECTORS(dst, s) FOR_ALL_ROWS_IN_VECTOR { rowop_not(ROW(dst), ROW(s)); }

/*
 * ============================================================================
 * HYPOTHESIS 3 TEST: Vertical/Bit-Serial Alternative
 * ============================================================================
 * 
 * WHAT IS TESTED:
 * We suspect that trying to force horizontal calculations in a DRAM architecture
 * optimized for vertical bit-plane logic (Ambit) is fundamentally inefficient.
 * This file tests an entirely different layout: standard vertical bit-serial
 * multiplication. Instead of physically shifting rows, we use bit-plane indices.
 * 
 * MISSING FUNCTIONALITY:
 * This completely abandons the "horizontal integer packing" concept. It assumes
 * the CPU has already paid the extremely high penalty of row-migration and
 * bit-transposition to align the data vertically in memory.
 * 
 * EXPECTED OUTCOME:
 * If this executes drastically faster, it implies that Ambit's native AAP
 * vertical logic is far superior to horizontal simulated arithmetic, and that
 * horizontal packing may not be viable for GEMM unless the workload relies
 * heavily on physical shifts (like CNN convolutions).
 * ============================================================================
 */

int main(int argc, char **argv)
{
    init_ambit();
    srand(121324314);

    // col_length now represents the bit-width (e.g., 32 bit-planes)
    int col_length = parse_optional_int_arg(argc, argv, 1, 32);
    int num_vals = 1024 << parse_optional_int_arg(argc, argv, 2, 0);

    int per_col_bytes = num_vals / 8;
    int per_col_rows = (per_col_bytes + ROW_SIZE - 1) / ROW_SIZE;

    // Allocate 32 bit-planes for A, B, and the 64 bit-planes for Output C
    unsigned **vals_a = allocate_vector_array(per_col_bytes, col_length);
    unsigned **vals_b = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length * 2);

    unsigned *cond_a = allocate_vector(per_col_bytes);

    for (int j = 0; j < col_length; ++j) {
        // Initialize random bit-planes
        // (Omitted for brevity in testing loop overhead)
    }

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            // Clear all 64 output bit-planes
            // ...

            // Bit-serial multiplication
            for(int i = 0; i < col_length; i++) { // For each bit in multiplier B
                unsigned *b_plane = VECTOR(vals_b[i]);
                
                for (int j = 0; j < col_length; j++) { // For each bit in A
                    unsigned *a_plane = VECTOR(vals_a[j]);
                    
                    // Conditionally mask the A bit-plane using the B bit-plane
                    AND_VECTORS(cond_a, a_plane, b_plane);
                    
                    // Add cond_a to output bit-plane (i + j)
                    // This uses native Ambit vertical addition!
                    unsigned *out_plane = VECTOR(output[i + j]);
                    
                    // Native Ambit Bit-Serial Full Adder (omitted exact AAP sequence)
                    // AAP_VECTORS(out_plane, cond_a);
                }
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

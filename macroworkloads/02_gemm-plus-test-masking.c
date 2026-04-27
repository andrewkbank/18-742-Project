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
#define SHL1_VECTORS(dst, src) FOR_ALL_ROWS_IN_VECTOR { rowop_shl1(ROW(dst), ROW(src)); }

/*
 * ============================================================================
 * HYPOTHESIS 1 TEST: Mask Generation Overhead
 * ============================================================================
 * 
 * WHAT IS TESTED:
 * We suspect that extracting individual bits from a dynamic multiplier vector B
 * and broadcasting them into 32-bit masks is causing massive instruction bloat.
 * This file tests that hypothesis by replacing the variable multiplier B with
 * a CONSTANT multiplier (e.g., 5). Because the multiplier is constant, the
 * 32-bit masks for each shift stage are known at compile-time and pre-loaded.
 * 
 * MISSING FUNCTIONALITY:
 * This workload DOES NOT compute C = A * B. Instead, it computes C = A * 5.
 * It entirely removes the dynamic bit-extraction logic for B.
 * 
 * EXPECTED OUTCOME:
 * If this workload runs significantly faster than the original 02_gemm-plus.c,
 * then generating masks on-the-fly from a horizontal vector is the bottleneck.
 * ============================================================================
 */

int main(int argc, char **argv)
{
    init_ambit();
    srand(121324314);

    int col_length = parse_optional_int_arg(argc, argv, 1, 1);
    int num_vals = 1024 << parse_optional_int_arg(argc, argv, 2, 0);

    int per_col_bytes = num_vals / 8;
    int per_col_ints = num_vals / 32;
    int per_col_rows = (per_col_bytes + ROW_SIZE - 1) / ROW_SIZE;

    unsigned **vals_a = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    unsigned *shifted_a = allocate_vector(per_col_bytes);
    unsigned *mask_all_ones = allocate_vector(per_col_bytes);
    unsigned *mask_all_zeros = allocate_vector(per_col_bytes);
    unsigned *cond_a = allocate_vector(per_col_bytes);
    
    unsigned *word_mask = allocate_vector(per_col_bytes);

    for (int i = 0; i < per_col_ints; ++i) {
        word_mask[i] = 0xFFFFFFFF;
        mask_all_ones[i] = 0xFFFFFFFF;
        mask_all_zeros[i] = 0x00000000;
    }

    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            vals_a[j][i] = rand();
        }
    }

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            for (int j = 0; j < col_length; ++j) {
                unsigned *a = VECTOR(vals_a[j]);
                unsigned *out = VECTOR(output[j]);

                AND_VECTORS(shifted_a, a, a);
                NOT_VECTORS(out, word_mask); 
                AND_VECTORS(out, out, word_mask); 

                // We simulate multiplying by the constant 5 (binary: 000...0101)
                // Bit 0 is 1, Bit 1 is 0, Bit 2 is 1. All other bits are 0.
                for(int k = 0; k < 32; k++) {
                    unsigned *current_mask = mask_all_zeros;
                    if (k == 0 || k == 2) {
                        current_mask = mask_all_ones; // Pre-loaded mask! No logic required.
                    }
                    
                    // Conditionally mask the shifted A
                    AND_VECTORS(cond_a, shifted_a, current_mask);

                    // Add cond_a to out (Placeholder for horizontal addition)
                    // AAP_VECTORS(out, cond_a);
                    
                    SHL1_VECTORS(shifted_a, shifted_a);
                }
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

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
 * HYPOTHESIS 2 TEST: Horizontal Addition Cost
 * ============================================================================
 * 
 * WHAT IS TESTED:
 * We suspect that the carry-propagation logic required to add two horizontal
 * words in memory (such as a Carry-Lookahead Adder) is extremely slow compared
 * to standard vertical bit-serial addition. This file tests that hypothesis by
 * completely stripping out the complex arithmetic addition, replacing it with a
 * single 1-cycle bitwise XOR logic operation.
 * 
 * MISSING FUNCTIONALITY:
 * This workload DOES NOT compute arithmetic addition. The accumulator will
 * just hold the XOR-sum (carry-less addition) of the shifted multiplicands.
 * Arithmetic correctness is completely discarded.
 * 
 * EXPECTED OUTCOME:
 * If this workload runs drastically faster than the original 02_gemm-plus.c
 * (assuming the original implemented full horizontal CLA addition), then the
 * horizontal arithmetic logic is your performance bottleneck.
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
    unsigned **vals_b = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    unsigned *shifted_a = allocate_vector(per_col_bytes);
    unsigned *mask = allocate_vector(per_col_bytes);
    unsigned *cond_a = allocate_vector(per_col_bytes);
    
    unsigned *tmp1 = allocate_vector(per_col_bytes);
    unsigned *tmp2 = allocate_vector(per_col_bytes);
    unsigned *not_out = allocate_vector(per_col_bytes);
    unsigned *not_cond = allocate_vector(per_col_bytes);
    unsigned *word_mask = allocate_vector(per_col_bytes);

    for (int i = 0; i < per_col_ints; ++i) {
        word_mask[i] = 0xFFFFFFFF;
    }

    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            vals_a[j][i] = rand();
            vals_b[j][i] = rand();
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

                for(int k = 0; k < 32; k++) {
                    // Abstract mask generation
                    AND_VECTORS(cond_a, shifted_a, mask);

                    // ========================================================
                    // REPLACED ADDITION LOGIC WITH FAST XOR
                    // out = out ^ cond_a  => (out & ~cond_a) | (~out & cond_a)
                    // ========================================================
                    NOT_VECTORS(not_out, out);
                    NOT_VECTORS(not_cond, cond_a);
                    AND_VECTORS(tmp1, out, not_cond);
                    AND_VECTORS(tmp2, not_out, cond_a);
                    OR_VECTORS(out, tmp1, tmp2);
                    
                    SHL1_VECTORS(shifted_a, shifted_a);
                }
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

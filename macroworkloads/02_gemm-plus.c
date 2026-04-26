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

int
main(int argc, char **argv)
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
    
    // For horizontal integers, we need to mask out bleeding bits from shifts
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
                unsigned *b = VECTOR(vals_b[j]);
                unsigned *out = VECTOR(output[j]);

                AND_VECTORS(shifted_a, a, a);
                // Initialize out to 0
                NOT_VECTORS(out, word_mask); // out = 0 (assuming word_mask is all 1s)
                AND_VECTORS(out, out, word_mask); // Ensure 0

                // Horizontal shift-and-add
                // For each bit in the multiplier B
                for(int k = 0; k < 32; k++) {
                    // Extract k-th bit of B and create a mask.
                    // This involves shifting B, masking, and replicating the bit across the 32-bit word.
                    // Here we abstract it as generating a boolean mask from B.
                    
                    // Conditionally add: cond_a = shifted_a & mask
                    AND_VECTORS(cond_a, shifted_a, mask);

                    // Add cond_a to out.
                    // Since it's horizontal, addition involves CLA logic (omitted here for brevity, 
                    // similar to bitweave prefix sum logic).
                    // We use Ambit's AAP_VECTORS for a bit-serial addition just as a placeholder
                    // AAP_VECTORS(out, cond_a);
                    
                    // Shift A left by 1 for the next bit
                    SHL1_VECTORS(shifted_a, shifted_a);
                }
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

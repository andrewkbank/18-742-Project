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
#define SHL8_VECTORS(dst, src) FOR_ALL_ROWS_IN_VECTOR { rowop_shl8(ROW(dst), ROW(src)); }

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

    unsigned **vals = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    unsigned *tmp1 = allocate_vector(per_col_bytes);
    unsigned *tmp2 = allocate_vector(per_col_bytes);
    unsigned *not_a = allocate_vector(per_col_bytes);
    unsigned *not_b = allocate_vector(per_col_bytes);
    unsigned *shifted = allocate_vector(per_col_bytes);
    unsigned *accum = allocate_vector(per_col_bytes);

    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            vals[j][i] = rand();
        }
    }

    flush_vector_array(vals, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);
    flush_cache_range(tmp1, per_col_bytes);
    flush_cache_range(tmp2, per_col_bytes);
    flush_cache_range(not_a, per_col_bytes);
    flush_cache_range(not_b, per_col_bytes);
    flush_cache_range(shifted, per_col_bytes);
    flush_cache_range(accum, per_col_bytes);

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            for (int j = 0; j < col_length; ++j) {
                unsigned *v = VECTOR(vals[j]);
                unsigned *out = VECTOR(output[j]);

                // accum = v
                AND_VECTORS(accum, v, v);

                // XOR-Tree up to 32 bits for individual integer parity
                // The algorithm is: accum = accum ^ (accum >> k)
                // However, we can shift left as well to gather bits.
                // In a true DRAM row, parity of the entire row involves up to 32768 bits.
                // We'll demonstrate parity within 32-bit words (5 steps) or larger.
                
                for (int step = 1; step < 32; step *= 2) {
                    // Shift accum by `step` bits
                    AND_VECTORS(shifted, accum, accum);
                    int s = step;
                    while (s >= 8) {
                        SHL8_VECTORS(tmp1, shifted);
                        AND_VECTORS(shifted, tmp1, tmp1);
                        s -= 8;
                    }
                    while (s > 0) {
                        SHL1_VECTORS(tmp1, shifted);
                        AND_VECTORS(shifted, tmp1, tmp1);
                        s -= 1;
                    }

                    // XOR: accum = accum ^ shifted
                    // a^b = (a & ~b) | (~a & b)
                    NOT_VECTORS(not_a, accum);
                    NOT_VECTORS(not_b, shifted);
                    AND_VECTORS(tmp1, accum, not_b);
                    AND_VECTORS(tmp2, not_a, shifted);
                    OR_VECTORS(accum, tmp1, tmp2);
                }

                AND_VECTORS(out, accum, accum);
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

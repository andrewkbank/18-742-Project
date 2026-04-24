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

    unsigned **vals1 = allocate_vector_array(per_col_bytes, col_length);
    unsigned **vals2 = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    unsigned *mask1 = allocate_vector(per_col_bytes);
    unsigned *mask2 = allocate_vector(per_col_bytes);
    unsigned *mask4 = allocate_vector(per_col_bytes);
    unsigned *mask8 = allocate_vector(per_col_bytes);
    unsigned *mask16 = allocate_vector(per_col_bytes);

    for (int i = 0; i < per_col_ints; ++i) {
        mask1[i] = 0xFFFFFFFE;
        mask2[i] = 0xFFFFFFFC;
        mask4[i] = 0xFFFFFFF0;
        mask8[i] = 0xFFFFFF00;
        mask16[i] = 0xFFFF0000;
    }

    unsigned *L_vec = allocate_vector(per_col_bytes);
    unsigned *E_vec = allocate_vector(per_col_bytes);
    unsigned *tmp = allocate_vector(per_col_bytes);
    unsigned *shifted_L = allocate_vector(per_col_bytes);
    unsigned *shifted_E = allocate_vector(per_col_bytes);
    unsigned *not_a = allocate_vector(per_col_bytes);
    unsigned *not_b = allocate_vector(per_col_bytes);
    unsigned *xor_ab = allocate_vector(per_col_bytes);

    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            vals1[j][i] = rand();
            vals2[j][i] = rand();
        }
    }

    flush_vector_array(vals1, col_length, per_col_bytes);
    flush_vector_array(vals2, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);
    flush_cache_range(mask1, per_col_bytes);
    flush_cache_range(mask2, per_col_bytes);
    flush_cache_range(mask4, per_col_bytes);
    flush_cache_range(mask8, per_col_bytes);
    flush_cache_range(mask16, per_col_bytes);
    flush_cache_range(L_vec, per_col_bytes);
    flush_cache_range(E_vec, per_col_bytes);
    flush_cache_range(tmp, per_col_bytes);
    flush_cache_range(shifted_L, per_col_bytes);
    flush_cache_range(shifted_E, per_col_bytes);
    flush_cache_range(not_a, per_col_bytes);
    flush_cache_range(not_b, per_col_bytes);
    flush_cache_range(xor_ab, per_col_bytes);

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            for (int j = 0; j < col_length; ++j) {
                unsigned *v1 = VECTOR(vals1[j]);
                unsigned *v2 = VECTOR(vals2[j]);
                unsigned *out = VECTOR(output[j]);

                // L = ~a & b
                NOT_VECTORS(not_a, v1);
                AND_VECTORS(L_vec, not_a, v2);

                // E = ~(a ^ b)
                // a^b = (a & ~b) | (~a & b)
                NOT_VECTORS(not_b, v2);
                AND_VECTORS(tmp, v1, not_b);
                OR_VECTORS(xor_ab, tmp, L_vec);
                NOT_VECTORS(E_vec, xor_ab);

                // Step 1: Shift by 1
                SHL1_VECTORS(shifted_L, L_vec);
                AND_VECTORS(shifted_L, shifted_L, mask1);
                AND_VECTORS(tmp, E_vec, shifted_L);
                OR_VECTORS(L_vec, L_vec, tmp);

                SHL1_VECTORS(shifted_E, E_vec);
                AND_VECTORS(shifted_E, shifted_E, mask1);
                AND_VECTORS(E_vec, E_vec, shifted_E);

                // Step 2: Shift by 2
                SHL1_VECTORS(tmp, L_vec);
                SHL1_VECTORS(shifted_L, tmp);
                AND_VECTORS(shifted_L, shifted_L, mask2);
                AND_VECTORS(tmp, E_vec, shifted_L);
                OR_VECTORS(L_vec, L_vec, tmp);

                SHL1_VECTORS(tmp, E_vec);
                SHL1_VECTORS(shifted_E, tmp);
                AND_VECTORS(shifted_E, shifted_E, mask2);
                AND_VECTORS(E_vec, E_vec, shifted_E);

                // Step 3: Shift by 4
                SHL1_VECTORS(shifted_L, L_vec);
                SHL1_VECTORS(tmp, shifted_L);
                SHL1_VECTORS(shifted_L, tmp);
                SHL1_VECTORS(tmp, shifted_L);
                AND_VECTORS(shifted_L, tmp, mask4);
                AND_VECTORS(tmp, E_vec, shifted_L);
                OR_VECTORS(L_vec, L_vec, tmp);

                SHL1_VECTORS(shifted_E, E_vec);
                SHL1_VECTORS(tmp, shifted_E);
                SHL1_VECTORS(shifted_E, tmp);
                SHL1_VECTORS(tmp, shifted_E);
                AND_VECTORS(shifted_E, tmp, mask4);
                AND_VECTORS(E_vec, E_vec, shifted_E);

                // Step 4: Shift by 8
                SHL8_VECTORS(shifted_L, L_vec);
                AND_VECTORS(shifted_L, shifted_L, mask8);
                AND_VECTORS(tmp, E_vec, shifted_L);
                OR_VECTORS(L_vec, L_vec, tmp);

                SHL8_VECTORS(shifted_E, E_vec);
                AND_VECTORS(shifted_E, shifted_E, mask8);
                AND_VECTORS(E_vec, E_vec, shifted_E);

                // Step 5: Shift by 16
                SHL8_VECTORS(tmp, L_vec);
                SHL8_VECTORS(shifted_L, tmp);
                AND_VECTORS(shifted_L, shifted_L, mask16);
                AND_VECTORS(tmp, E_vec, shifted_L);
                OR_VECTORS(L_vec, L_vec, tmp);

                // MSB of L_vec has the result
                // Copy to out
                AND_VECTORS(out, L_vec, L_vec);
            }
        }
    }

    m5_dump_stats(0, 0);

    // Validation
    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            uint32_t a = vals1[j][i];
            uint32_t b = vals2[j][i];
            uint32_t actual = (output[j][i] >> 31) & 1;
            uint32_t expected = (a < b) ? 1 : 0;
            if (actual != expected) {
                mismatches++;
            }
        }
    }

    printf("bitweave_range_queries-plus mismatches=%d\n", mismatches);

    return mismatches != 0;
}

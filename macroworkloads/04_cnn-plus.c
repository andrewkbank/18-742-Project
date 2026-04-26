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
#define SHR1_VECTORS(dst, src) FOR_ALL_ROWS_IN_VECTOR { rowop_shr1(ROW(dst), ROW(src)); }

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
    unsigned **vals_left = allocate_vector_array(per_col_bytes, col_length);
    unsigned **vals_right = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    unsigned *w0 = allocate_vector(per_col_bytes);
    unsigned *w1 = allocate_vector(per_col_bytes);
    unsigned *w2 = allocate_vector(per_col_bytes);
    
    unsigned *tmp1 = allocate_vector(per_col_bytes);
    unsigned *tmp2 = allocate_vector(per_col_bytes);
    unsigned *not_a = allocate_vector(per_col_bytes);
    unsigned *not_b = allocate_vector(per_col_bytes);
    
    unsigned *xnor_l = allocate_vector(per_col_bytes);
    unsigned *xnor_c = allocate_vector(per_col_bytes);
    unsigned *xnor_r = allocate_vector(per_col_bytes);
    unsigned *temp_sum = allocate_vector(per_col_bytes);

    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            vals[j][i] = rand();
        }
    }

    flush_vector_array(vals, col_length, per_col_bytes);
    flush_vector_array(vals_left, col_length, per_col_bytes);
    flush_vector_array(vals_right, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);
    flush_cache_range(w0, per_col_bytes);
    flush_cache_range(w1, per_col_bytes);
    flush_cache_range(w2, per_col_bytes);
    flush_cache_range(tmp1, per_col_bytes);
    flush_cache_range(tmp2, per_col_bytes);
    flush_cache_range(not_a, per_col_bytes);
    flush_cache_range(not_b, per_col_bytes);
    flush_cache_range(xnor_l, per_col_bytes);
    flush_cache_range(xnor_c, per_col_bytes);
    flush_cache_range(xnor_r, per_col_bytes);
    flush_cache_range(temp_sum, per_col_bytes);

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            for (int j = 0; j < col_length; ++j) {
                unsigned *v = VECTOR(vals[j]);
                unsigned *vl = VECTOR(vals_left[j]);
                unsigned *vr = VECTOR(vals_right[j]);
                unsigned *out = VECTOR(output[j]);

                SHL1_VECTORS(vl, v);
                SHR1_VECTORS(vr, v);

                // XNOR with weights (w0, w1, w2)
                // ~(a ^ b) = (a & b) | (~a & ~b)
                
                // For w0 (left)
                NOT_VECTORS(not_a, vl);
                NOT_VECTORS(not_b, w0);
                AND_VECTORS(tmp1, vl, w0);
                AND_VECTORS(tmp2, not_a, not_b);
                OR_VECTORS(xnor_l, tmp1, tmp2);

                // For w1 (center)
                NOT_VECTORS(not_a, v);
                NOT_VECTORS(not_b, w1);
                AND_VECTORS(tmp1, v, w1);
                AND_VECTORS(tmp2, not_a, not_b);
                OR_VECTORS(xnor_c, tmp1, tmp2);

                // For w2 (right)
                NOT_VECTORS(not_a, vr);
                NOT_VECTORS(not_b, w2);
                AND_VECTORS(tmp1, vr, w2);
                AND_VECTORS(tmp2, not_a, not_b);
                OR_VECTORS(xnor_r, tmp1, tmp2);

                // Ambit addition for Popcount (XNOR-Net sum)
                // Using AAP vectors to add xnor_l + xnor_c + xnor_r
                AAP_VECTORS( B_DCC1,     C_0 )
                AAP_VECTORS( B_T0_T1_T2  , B_DCC1     )
                AAP_VECTORS( B_T2_T3     , xnor_l     )
                AAP_VECTORS( B_DCC1      , xnor_c     )
                AP_VECTOR(   B_DCC1_T0_T3             )
                AAP_VECTORS( B_T0_T3     , B_DCC1N    )
                AP_VECTOR(   B_T0_T1_T2               )
                AAP_VECTORS( B_T1        , xnor_c     )
                AAP_VECTORS( temp_sum    , B_T1_T2_T3 )

                AAP_VECTORS( B_DCC1,     C_0 )
                AAP_VECTORS( B_T0_T1_T2  , B_DCC1     )
                AAP_VECTORS( B_T2_T3     , temp_sum   )
                AAP_VECTORS( B_DCC1      , xnor_r     )
                AP_VECTOR(   B_DCC1_T0_T3             )
                AAP_VECTORS( B_T0_T3     , B_DCC1N    )
                AP_VECTOR(   B_T0_T1_T2               )
                AAP_VECTORS( B_T1        , xnor_r     )
                AAP_VECTORS( out         , B_T1_T2_T3 )
            }
        }
    }

    m5_dump_stats(0, 0);
    return 0;
}

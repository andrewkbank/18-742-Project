#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <immintrin.h>
#include <m5op.h>
#include "mimdram.h"
#include "shift_bench.h"

extern void rowop_shl1(void *dst, void *src);
extern void rowop_shr1(void *dst, void *src);

#define SHL1_VECTORS(dst, src) FOR_ALL_ROWS_IN_VECTOR { rowop_shl1(ROW(dst), ROW(src)); }
#define SHR1_VECTORS(dst, src) FOR_ALL_ROWS_IN_VECTOR { rowop_shr1(ROW(dst), ROW(src)); }

int main(int argc, char **argv) {
    init_ambit();
    srand(121324314);

    int col_length = atoi(argv[1]);
    int num_vals = 1024 << (atoi(argv[2]));

    int per_col_bytes = num_vals / 8;
    int per_col_rows = (per_col_bytes + ROW_SIZE - 1) / ROW_SIZE;

    // allocate operands data (bit planes)
    unsigned **vals = random_vector_array(per_col_bytes, col_length);
    unsigned **vals_left = allocate_vector_array(per_col_bytes, col_length);
    unsigned **vals_right = allocate_vector_array(per_col_bytes, col_length);

    // allocate output
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);
    unsigned **temp_sum = allocate_vector_array(per_col_bytes, col_length);

    flush_vector_array(vals, col_length, per_col_bytes);
    flush_vector_array(vals_left, col_length, per_col_bytes);
    flush_vector_array(vals_right, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);
    flush_vector_array(temp_sum, col_length, per_col_bytes);

	// run some iterations of the algorithm
    for (int iter = 0; iter < 2; iter ++) {
        m5_reset_stats(0,0);

		FOR_ALL_VECTORS {
            // Shift bit-planes to align neighboring pixels
            for (int j = 0; j < col_length; j++) {
                SHL1_VECTORS(VECTOR(vals_left[j]), VECTOR(vals[j]));
                SHR1_VECTORS(VECTOR(vals_right[j]), VECTOR(vals[j]));
            }

            // ADD vals + vals_left -> temp_sum
			AAP_VECTORS( B_DCC1,     C_0 )
			for (int j = 0; j < col_length; j ++) {
				unsigned *v1 = VECTOR(vals[j]);
				unsigned *v2 = VECTOR(vals_left[j]);
				unsigned *out = VECTOR(temp_sum[j]);

				AAP_VECTORS( B_T0_T1_T2  , B_DCC1     )
				AAP_VECTORS( B_T2_T3     , v1         )
				AAP_VECTORS( B_DCC1      , v2         )
				AP_VECTOR(   B_DCC1_T0_T3             )
				AAP_VECTORS( B_T0_T3     , B_DCC1N    )
				AP_VECTOR(   B_T0_T1_T2               )
				AAP_VECTORS( B_T1        , v2         )
				AAP_VECTORS( out         , B_T1_T2_T3 )
			}

            // ADD temp_sum + vals_right -> output
			AAP_VECTORS( B_DCC1,     C_0 )
			for (int j = 0; j < col_length; j ++) {
				unsigned *v1 = VECTOR(temp_sum[j]);
				unsigned *v2 = VECTOR(vals_right[j]);
				unsigned *out = VECTOR(output[j]);

				AAP_VECTORS( B_T0_T1_T2  , B_DCC1     )
				AAP_VECTORS( B_T2_T3     , v1         )
				AAP_VECTORS( B_DCC1      , v2         )
				AP_VECTOR(   B_DCC1_T0_T3             )
				AAP_VECTORS( B_T0_T3     , B_DCC1N    )
				AP_VECTOR(   B_T0_T1_T2               )
				AAP_VECTORS( B_T1        , v2         )
				AAP_VECTORS( out         , B_T1_T2_T3 )
			}
		}

    }
    m5_dump_stats(0,0);

    return 0;
}

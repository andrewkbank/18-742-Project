#define _POSIX_C_SOURCE 200112L

#include <m5op.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "mimdram.h"
#include "shift_bench.h"

int
main(int argc, char **argv)
{
    init_ambit();
    srand(121324314);

    int col_length = atoi(argv[1]);
    int num_vals = 1024 << atoi(argv[2]);
    int pattern = parse_optional_int_arg(argc, argv, 3, SHIFT_PATTERN_RANDOM);
    int shift_amount = parse_optional_int_arg(argc, argv, 4, 13);

    int per_col_bytes = num_vals / 8;
    int per_col_rows = (per_col_bytes + ROW_SIZE - 1) / ROW_SIZE;

    unsigned **vals1 = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);
    unsigned **scratch = allocate_vector_array(per_col_bytes, col_length);

    for (int j = 0; j < col_length; ++j) {
        fill_shift_pattern((uint8_t *)vals1[j], per_col_bytes, pattern);
    }

    flush_vector_array(vals1, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);
    flush_vector_array(scratch, col_length, per_col_bytes);

    unsigned **final_output = vals1;
    for (int iter = 0; iter < 2; ++iter) {
        unsigned **src_arrays = vals1;
        unsigned **dst_arrays = output;

        m5_reset_stats(0, 0);

        for (int step = 0; step < shift_amount / 8; ++step) {
            FOR_ALL_VECTORS {
                for (int j = 0; j < col_length; ++j) {
                    uint8_t *src = (uint8_t *)VECTOR(src_arrays[j]);
                    uint8_t *dst = (uint8_t *)VECTOR(dst_arrays[j]);
                    FOR_ALL_ROWS_IN_VECTOR {
                        rowop_shl8(ROW(dst), ROW(src));
                    }
                }
            }

            src_arrays = dst_arrays;
            dst_arrays = (dst_arrays == output) ? scratch : output;
        }

        for (int step = 0; step < shift_amount % 8; ++step) {
            FOR_ALL_VECTORS {
                for (int j = 0; j < col_length; ++j) {
                    uint8_t *src = (uint8_t *)VECTOR(src_arrays[j]);
                    uint8_t *dst = (uint8_t *)VECTOR(dst_arrays[j]);
                    FOR_ALL_ROWS_IN_VECTOR {
                        rowop_shl1(ROW(dst), ROW(src));
                    }
                }
            }

            src_arrays = dst_arrays;
            dst_arrays = (dst_arrays == output) ? scratch : output;
        }

        final_output = src_arrays;
    }

    m5_dump_stats(0, 0);

    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        uint8_t *expected = malloc(per_col_bytes);
        shift_row_left_bytes(expected, (uint8_t *)vals1[j], per_col_bytes,
                             shift_amount);
        mismatches += count_mismatched_bytes(expected, (uint8_t *)final_output[j],
                                             per_col_bytes);
        free(expected);
    }

    uint64_t checksum = checksum_vector_array((uint8_t **)final_output,
                                              col_length, per_col_bytes);
    print_shift_summary("shift_left_arbitrary-plus", pattern, shift_amount,
                        checksum, mismatches);

    return mismatches != 0;
}

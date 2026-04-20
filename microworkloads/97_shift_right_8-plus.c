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

    int per_col_bytes = num_vals / 8;
    int per_col_rows = (per_col_bytes + ROW_SIZE - 1) / ROW_SIZE;

    unsigned **vals1 = allocate_vector_array(per_col_bytes, col_length);
    unsigned **output = allocate_vector_array(per_col_bytes, col_length);

    for (int j = 0; j < col_length; ++j) {
        fill_shift_pattern((uint8_t *)vals1[j], per_col_bytes, pattern);
    }

    flush_vector_array(vals1, col_length, per_col_bytes);
    flush_vector_array(output, col_length, per_col_bytes);

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        FOR_ALL_VECTORS {
            for (int j = 0; j < col_length; ++j) {
                uint8_t *src = (uint8_t *)VECTOR(vals1[j]);
                uint8_t *dst = (uint8_t *)VECTOR(output[j]);
                FOR_ALL_ROWS_IN_VECTOR {
                    rowop_shr8(ROW(dst), ROW(src));
                }
            }
        }
    }

    m5_dump_stats(0, 0);

    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        uint8_t *expected = malloc(per_col_bytes);
        shift_row_right_8_bytes(expected, (uint8_t *)vals1[j], per_col_bytes);
        mismatches += count_mismatched_bytes(expected, (uint8_t *)output[j],
                                             per_col_bytes);
        free(expected);
    }

    uint64_t checksum = checksum_vector_array((uint8_t **)output, col_length,
                                              per_col_bytes);
    print_shift_summary("shift_right_8-plus", pattern, 8, checksum,
                        mismatches);

    return mismatches != 0;
}

#include <m5op.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "baseline.h"
#include "shift_bench.h"

int
main(int argc, char **argv)
{
    srand(121324314);

    int col_length = atoi(argv[1]);
    int num_vals = 1024 << atoi(argv[2]);
    int pattern = parse_optional_int_arg(argc, argv, 3, SHIFT_PATTERN_RANDOM);

    int per_col_bytes = num_vals / 8;

    uint8_t **vals1 = malloc(col_length * sizeof(uint8_t *));
    uint8_t **output = malloc(col_length * sizeof(uint8_t *));
    uint8_t **expected = malloc(col_length * sizeof(uint8_t *));

    for (int j = 0; j < col_length; ++j) {
        vals1[j] = allocate_array(per_col_bytes);
        output[j] = allocate_array(per_col_bytes);
        expected[j] = allocate_array(per_col_bytes);
        fill_shift_pattern(vals1[j], per_col_bytes, pattern);
    }

    for (int iter = 0; iter < 2; ++iter) {
        m5_reset_stats(0, 0);

        for (int j = 0; j < col_length; ++j) {
            shift_row_right_1_bytes(output[j], vals1[j], per_col_bytes);
        }
    }

    m5_dump_stats(0, 0);

    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        shift_row_right_1_bytes(expected[j], vals1[j], per_col_bytes);
        mismatches += count_mismatched_bytes(expected[j], output[j],
                                             per_col_bytes);
    }

    uint64_t checksum = checksum_vector_array(output, col_length,
                                              per_col_bytes);
    print_shift_summary("shift_right_1-baseline", pattern, 1, checksum,
                        mismatches);

    return mismatches != 0;
}

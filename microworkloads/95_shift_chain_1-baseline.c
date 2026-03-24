#include <m5op.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline.h"
#include "shift_bench.h"

int
main(int argc, char **argv)
{
    srand(121324314);

    int col_length = atoi(argv[1]);
    int num_vals = 1024 << atoi(argv[2]);
    int pattern = parse_optional_int_arg(argc, argv, 3, SHIFT_PATTERN_RANDOM);
    int repeats = parse_optional_int_arg(argc, argv, 4, 8);

    int per_col_bytes = num_vals / 8;

    uint8_t **vals1 = malloc(col_length * sizeof(uint8_t *));
    uint8_t **output = malloc(col_length * sizeof(uint8_t *));
    uint8_t **scratch = malloc(col_length * sizeof(uint8_t *));

    for (int j = 0; j < col_length; ++j) {
        vals1[j] = allocate_array(per_col_bytes);
        output[j] = allocate_array(per_col_bytes);
        scratch[j] = allocate_array(per_col_bytes);
        fill_shift_pattern(vals1[j], per_col_bytes, pattern);
    }

    uint8_t **final_output = vals1;
    for (int iter = 0; iter < 2; ++iter) {
        uint8_t **src_arrays = vals1;
        uint8_t **dst_arrays = output;

        m5_reset_stats(0, 0);

        for (int step = 0; step < repeats; ++step) {
            for (int j = 0; j < col_length; ++j) {
                shift_row_left_1_bytes(dst_arrays[j], src_arrays[j],
                                       per_col_bytes);
            }

            src_arrays = dst_arrays;
            dst_arrays = (dst_arrays == output) ? scratch : output;
        }

        final_output = src_arrays;
    }

    m5_dump_stats(0, 0);

    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        uint8_t *cur = allocate_array(per_col_bytes);
        uint8_t *next = allocate_array(per_col_bytes);
        memcpy(cur, vals1[j], per_col_bytes);

        for (int step = 0; step < repeats; ++step) {
            shift_row_left_1_bytes(next, cur, per_col_bytes);
            uint8_t *tmp = cur;
            cur = next;
            next = tmp;
        }

        mismatches += count_mismatched_bytes(cur, final_output[j],
                                             per_col_bytes);
        free(cur);
        free(next);
    }

    uint64_t checksum = checksum_vector_array(final_output, col_length,
                                              per_col_bytes);
    print_shift_summary("shift_chain_1-baseline", pattern, repeats, checksum,
                        mismatches);

    return mismatches != 0;
}

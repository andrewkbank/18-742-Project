#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shift_bench.h"

int
main(int argc, char **argv)
{
    srand(121324314);

    int col_length = parse_optional_int_arg(argc, argv, 1, 1);
    int num_vals = 1024 << parse_optional_int_arg(argc, argv, 2, 0);

    int per_col_bytes = num_vals / 8;
    int per_col_ints = num_vals / 32;

    uint32_t **vals1 = malloc(col_length * sizeof(uint32_t *));
    uint32_t **vals2 = malloc(col_length * sizeof(uint32_t *));
    uint32_t **output = malloc(col_length * sizeof(uint32_t *));
    uint32_t **expected = malloc(col_length * sizeof(uint32_t *));

    for (int j = 0; j < col_length; ++j) {
        vals1[j] = malloc(per_col_bytes);
        vals2[j] = malloc(per_col_bytes);
        output[j] = malloc(per_col_bytes);
        expected[j] = malloc(per_col_bytes);
        
        for (int i = 0; i < per_col_ints; ++i) {
            vals1[j][i] = rand();
            vals2[j][i] = rand();
        }
    }

    for (int iter = 0; iter < 2; ++iter) {
        for (int j = 0; j < col_length; ++j) {
            for (int i = 0; i < per_col_ints; ++i) {
                uint32_t a = vals1[j][i];
                uint32_t b = vals2[j][i];
                
                uint32_t L = ~a & b;
                uint32_t E = ~(a ^ b);
                
                L = L | (E & (L << 1));
                E = E & (E << 1);
                
                L = L | (E & (L << 2));
                E = E & (E << 2);
                
                L = L | (E & (L << 4));
                E = E & (E << 4);
                
                L = L | (E & (L << 8));
                E = E & (E << 8);
                
                L = L | (E & (L << 16));
                
                // Result is in MSB (bit 31)
                output[j][i] = (L >> 31) & 1;
                
                // standard comparison for validation
                expected[j][i] = (a < b) ? 1 : 0;
            }
        }
    }

    int mismatches = 0;
    for (int j = 0; j < col_length; ++j) {
        for (int i = 0; i < per_col_ints; ++i) {
            if (output[j][i] != expected[j][i]) {
                mismatches++;
            }
        }
    }

    printf("bitweave_range_queries-baseline mismatches=%d\n", mismatches);

    return mismatches != 0;
}

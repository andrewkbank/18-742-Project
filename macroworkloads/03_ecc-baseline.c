#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <m5op.h>
#include "baseline.h"

#define ITERATE(type_t) ({ \
		type_t *v = (type_t *)vals; \
		type_t *out = (type_t *)output; \
		for (int i = 0; i < num_vals; ++i) { \
            type_t p = v[i]; \
            p ^= p >> 1; \
            p ^= p >> 2; \
            p ^= p >> 4; \
            if (sizeof(type_t) > 1) p ^= p >> 8; \
            if (sizeof(type_t) > 2) p ^= p >> 16; \
            if (sizeof(type_t) > 4) p ^= p >> 32; \
			out[i] = p & 1; \
		} \
	})

int main(int argc, char **argv) {
    srand(121324314);

    int col_length = atoi(argv[1]);
    int num_vals = 1024 << (atoi(argv[2]));

	int col_length_bytes = col_length / 8;
	int total_bytes = col_length_bytes * num_vals;

    // allocate operands data
	void *vals = random_array(total_bytes);

    // allocate output
    void *output = allocate_array(total_bytes);

	// run some iterations of the algorithm
    for (int iter = 0; iter < 2; iter ++) {
        m5_reset_stats(0,0);
			
		switch(col_length) {
			case 8:
				ITERATE(uint8_t);
				break;
			case 16:
				ITERATE(uint16_t);
				break;
			case 32:
				ITERATE(uint32_t);
				break;
			case 64:
				ITERATE(uint64_t);
				break;
		}
   	}
    m5_dump_stats(0,0);

	// dummy output
	uint64_t s = 0;
	for(int i = 0; i < total_bytes / 8; ++i) {
		s += ((uint64_t *)output)[i];
	}
	printf("%lu\n", s);

    return 0;
}

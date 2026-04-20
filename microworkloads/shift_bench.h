#ifndef SHIFT_BENCH_H
#define SHIFT_BENCH_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ShiftPattern {
    SHIFT_PATTERN_RANDOM = 0,
    SHIFT_PATTERN_ZERO = 1,
    SHIFT_PATTERN_ONES = 2,
    SHIFT_PATTERN_ALTERNATING = 3,
    SHIFT_PATTERN_BOUNDARY = 4,
};

static inline int
parse_optional_int_arg(int argc, char **argv, int index, int default_value)
{
    if (argc > index) {
        return atoi(argv[index]);
    }
    return default_value;
}

static inline void
fill_shift_pattern(uint8_t *buf, size_t bytes, int pattern)
{
    switch (pattern) {
      case SHIFT_PATTERN_ZERO:
        memset(buf, 0, bytes);
        break;
      case SHIFT_PATTERN_ONES:
        memset(buf, 0xFF, bytes);
        break;
      case SHIFT_PATTERN_ALTERNATING:
        for (size_t i = 0; i < bytes; ++i) {
            buf[i] = (i & 1) ? 0x55 : 0xAA;
        }
        break;
      case SHIFT_PATTERN_BOUNDARY:
        memset(buf, 0, bytes);
        if (bytes > 8) {
            buf[7] = 0x80;
            buf[8] = 0x01;
        } else if (bytes > 0) {
            buf[bytes - 1] = 0x80;
        }
        break;
      case SHIFT_PATTERN_RANDOM:
      default:
        for (size_t i = 0; i < bytes; ++i) {
            buf[i] = rand() & 0xFF;
        }
        break;
    }
}

static inline void
flush_cache_range(void *buf, size_t bytes)
{
    uint8_t *ptr = (uint8_t *)buf;
    for (size_t offset = 0; offset < bytes; offset += 64) {
        __asm__ __volatile__("clflush (%0)" : : "r"(ptr + offset) : "memory");
    }
    __asm__ __volatile__("mfence" : : : "memory");
}

static inline void
flush_vector_array(unsigned **bufs, int count, size_t bytes)
{
    for (int i = 0; i < count; ++i) {
        flush_cache_range(bufs[i], bytes);
    }
}

static inline void
shift_row_left_1_bytes(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    uint8_t carry = 0;
    for (size_t i = 0; i < bytes; ++i) {
        const uint8_t byte = src[i];
        dst[i] = (byte << 1) | carry;
        carry = byte >> 7;
    }
}

static inline void
shift_row_right_1_bytes(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    uint8_t carry = 0;
    for (size_t i = bytes; i-- > 0;) {
        const uint8_t byte = src[i];
        dst[i] = (byte >> 1) | (carry << 7);
        carry = byte & 0x1;
    }
}

static inline void
shift_row_left_8_bytes(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    dst[0] = 0;
    for (size_t i = 1; i < bytes; ++i) {
        dst[i] = src[i - 1];
    }
}

static inline void
shift_row_right_8_bytes(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    if (bytes == 0) {
        return;
    }

    for (size_t i = 0; i + 1 < bytes; ++i) {
        dst[i] = src[i + 1];
    }
    dst[bytes - 1] = 0;
}

static inline void
shift_row_left_bytes(uint8_t *dst, const uint8_t *src, size_t bytes,
                     int shift_amount)
{
    uint8_t *cur = malloc(bytes);
    uint8_t *next = malloc(bytes);

    memcpy(cur, src, bytes);
    for (int i = 0; i < shift_amount / 8; ++i) {
        shift_row_left_8_bytes(next, cur, bytes);
        uint8_t *tmp = cur;
        cur = next;
        next = tmp;
    }
    for (int i = 0; i < shift_amount % 8; ++i) {
        shift_row_left_1_bytes(next, cur, bytes);
        uint8_t *tmp = cur;
        cur = next;
        next = tmp;
    }

    memcpy(dst, cur, bytes);
    free(cur);
    free(next);
}

static inline void
shift_row_right_bytes(uint8_t *dst, const uint8_t *src, size_t bytes,
                      int shift_amount)
{
    uint8_t *cur = malloc(bytes);
    uint8_t *next = malloc(bytes);

    memcpy(cur, src, bytes);
    for (int i = 0; i < shift_amount / 8; ++i) {
        shift_row_right_8_bytes(next, cur, bytes);
        uint8_t *tmp = cur;
        cur = next;
        next = tmp;
    }
    for (int i = 0; i < shift_amount % 8; ++i) {
        shift_row_right_1_bytes(next, cur, bytes);
        uint8_t *tmp = cur;
        cur = next;
        next = tmp;
    }

    memcpy(dst, cur, bytes);
    free(cur);
    free(next);
}

static inline uint64_t
checksum_bytes(const uint8_t *buf, size_t bytes)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < bytes; ++i) {
        sum = (sum * 131) + buf[i];
    }
    return sum;
}

static inline uint64_t
checksum_vector_array(uint8_t **bufs, int count, size_t bytes)
{
    uint64_t sum = 0;
    for (int i = 0; i < count; ++i) {
        sum ^= checksum_bytes(bufs[i], bytes) + (uint64_t)i;
    }
    return sum;
}

static inline int
count_mismatched_bytes(const uint8_t *expected, const uint8_t *actual,
                       size_t bytes)
{
    int mismatches = 0;
    for (size_t i = 0; i < bytes; ++i) {
        mismatches += expected[i] != actual[i];
    }
    return mismatches;
}

static inline void
print_shift_summary(const char *name, int pattern, int repeats,
                    uint64_t checksum, int mismatches)
{
    printf("%s pattern=%d repeats=%d checksum=%lu mismatches=%d\n",
           name, pattern, repeats, checksum, mismatches);
}

#endif

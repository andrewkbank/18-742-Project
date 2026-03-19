#!/bin/bash

M5UTIL=../../../gem5/util/m5
GEM5INC=../../../gem5/include

# Compile each .c file in the current directory
for file in *.c; do
    # Extract filename without extension
    filename=$(basename -- "$file")
    filename="${filename%.*}"

    # Compile the .c file into an executable binary with the same name
    gcc -static -std=c99 -O3 -msse2 -I. -I "$M5UTIL" -I "$GEM5INC" \
        m5op_x86.S rowop.S "$file" -o "$filename.exe"
done

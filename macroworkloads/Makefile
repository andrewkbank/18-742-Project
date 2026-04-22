M5UTIL=../gem5/util/m5
GEM5INC=../gem5/include
FLAGS=-g -O3 -msse4 -Wall -Werror -I. -I../microworkloads -I$(M5UTIL) -I$(GEM5INC)
CPPFLAGS=$(FLAGS) -std=c++11
CFLAGS=$(FLAGS) -std=c99
LDFLAGS=-static -lm

SRCS = $(wildcard *.c)

PROGS = $(patsubst %.c,%.exe,$(SRCS))

all: $(PROGS)

%.exe: %.c
	gcc -static -O3 -msse2 -I. -I../microworkloads -I$(M5UTIL) -I$(GEM5INC) $< ../microworkloads/m5op_x86.S ../microworkloads/rowop.S -o $@

clean:
	rm -f *.exe

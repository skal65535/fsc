#
# Simple makefile for gcc compiler
# 

EXES = fsc test bit_test
all: libfsc.a $(EXES)

CC = gcc
CFLAGS = -O3 -DNDEBUG
AR = ar
ARFLAGS = r
LDFLAGS = -lm

%.o: %.c fsc.h
	$(CC) $(CFLAGS) -c $< -o $@

%.a:
	$(AR) $(ARFLAGS) $@ $^

libfscutils.a: fsc_utils.o fsc_utils.h

libfsc.a: fsc_enc.o fsc_dec.o fsc.h bits.o bits.h

test: test.o libfsc.a libfscutils.a
	gcc -o test test.o ./libfsc.a ./libfscutils.a $(LDFLAGS) $(CFLAGS)

fsc: fsc.o libfsc.a libfscutils.a
	gcc -o fsc fsc.o ./libfsc.a ./libfscutils.a $(LDFLAGS) $(CFLAGS)

bit_test: bit_test.o libfsc.a libfscutils.a
	gcc -o bit_test bit_test.o ./libfsc.a ./libfscutils.a $(LDFLAGS) $(CFLAGS)

pak: clean
	tar czf fsc_oss.tgz *.c *.h Makefile AUTHORS CONTRIBUTORS LICENSE README

clean:
	rm -f *~ *.o *.a $(EXES)

bug: test
	./test -s 2 40
	./test -s 13 10000
	./test -s 3 -p

bench: $(EXES)
	./bit_test -fsc 10000000 -p 252
	./bit_test -l 13 -p 255 60
	./bit_test -fsc -p 255 -l8 8 104
	./test 2000000 -t 0 -p 4 -s 61 | grep errors
	./test 2000000 -t 1 -p 6 -s 17 | grep errors
	./test 2000000 -t 2 -p 7 -s 23 | grep errors
	./test 2000000 -t 3 -p 9 -s 31 | grep errors
	./test 2000000 -t 4 -p 3 -s 254 | grep errors
	./test 2000000 -t 5 -p 2 -s 199 | grep errors
	./test -s 2 -l 2 10 | grep errors

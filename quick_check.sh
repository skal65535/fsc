#!/bin/sh

# quick validation test

set -e

make
for s in 2 5 10 30 100 200 256; do
  ./test 200001 -s $s -buck | grep errors
  ./test 200001 -s $s -rev  | grep errors
  ./test 200001 -s $s -mod  | grep errors
  ./test 200001 -s $s -pack | grep errors
  ./test 200001 -s $s -w    | grep errors
  ./test 200001 -s $s -w2   | grep errors
  ./test 200001 -s $s -w4   | grep errors
  ./test 200001 -s $s -a    | grep errors
  ./test 200001 -s $s -a2   | grep errors
done

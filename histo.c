//Copyright 2014 The FSC Authors. All Rights Reserved.
//
//Licensed under the Apache License, Version 2.0 (the "License");
//you may not use this file except in compliance with the License.
//You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
//------------------------------------------------------------------------------
//
// Histograms / cumulative frequencies / spread functions
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./fsc.h"

#include <stdio.h>
#include <math.h>
#include <assert.h>

//------------------------------------------------------------------------------

void FSCCountSymbols(const uint8_t* in, size_t in_size,
                     uint32_t counts[MAX_SYMBOLS]) {
  size_t n;
  memset(counts, 0, MAX_SYMBOLS * sizeof(counts[0]));
  for (n = 0; n < in_size; ++n) ++counts[in[n]];
}

//------------------------------------------------------------------------------

static int CompareKey(const void* p1, const void* p2) {
  const uint32_t a = *(const uint32_t*)p1;
  const uint32_t b = *(const uint32_t*)p2;
  assert(a != b);
  return (a > b) ? -1 : 1;
}

// Analyze counts[] and renormalize with Squeaky Wheel fix, so that
// the total is rescaled to be equal to tab_size exactly.
int FSCNormalizeCounts(uint32_t counts[MAX_SYMBOLS], int max_symbol,
                       int log_tab_size) {
  const int tab_size = 1 << log_tab_size;
  uint64_t total = 0;
  int nb_symbols = 0;
  int n;
  int last_nz = 0;

  for (n = 0; n < max_symbol; ++n) {
    total += counts[n];
    if (counts[n] > 0) {
      ++nb_symbols;
      last_nz = n + 1;
    }
  }
  if (nb_symbols < 1) return 0;   // won't work
  if (log_tab_size < 1) return 0;
  if (nb_symbols > tab_size) return 0;
  max_symbol = last_nz;

  uint32_t keys[MAX_SYMBOLS];
  int miss = tab_size;
  const float norm = 1.f * tab_size / total;
  int non_zero = 0;
  for (n = 0; n < max_symbol; ++n) {
    if (counts[n] > 0) {
      const float target = norm * counts[n];
      counts[n] = (uint32_t)target;  // floor
      if (counts[n] == 0) counts[n] = 1;
      miss -= counts[n];
      const uint32_t error = (uint32_t)((target - counts[n]) * (1 << 24));
      keys[non_zero++] = (error << 8) | n;
    }
  }
  if (miss == 0) return max_symbol;

  if (miss > 0) {
    // TODO(skal): we only need to select #miss top entries, not sort
    qsort(keys, non_zero, sizeof(keys[0]), CompareKey);
    for (n = 0; n < miss; ++n) {
      const uint32_t key = keys[n];
      const int idx = keys[n] & 0xff;
      ++counts[idx];
    }
  } else {
    // Rare case where ended up overflowing because counts[] are capped to 1 below.
    qsort(keys, non_zero, sizeof(keys[0]), CompareKey);
    while (miss != 0) {
      for (n = 0; miss != 0 && n < non_zero; ++n) {
        const uint32_t key = keys[non_zero - 1 - n];
        const int idx = keys[n] & 0xff;
        if (counts[idx] > 1) {
          --counts[idx];
          ++miss;
          if (miss == 0) break;
        }
      }
    }
  }
  return max_symbol;
}

//------------------------------------------------------------------------------
// Spread functions

#define MAX_INSERT_ITERATION 0   // limit bucket-sort complexity (0=off)

// insert with limited bucket sort
#define INSERT(s, key) do {                      \
  const double k = (key);                        \
  const int b = (int)(k);                        \
  if (b < tab_size) {                            \
    const int S = (s);                           \
    int16_t* p = &buckets[b];                    \
    int M = MAX_INSERT_ITERATION;                \
    while (M-- && *p != -1 && keys[*p] < k) {    \
      p = &next[*p];                             \
    }                                            \
    next[S] = *p;                                \
    *p = S;                                      \
    keys[S] = k;                                 \
  }                                              \
} while (0)

int BuildSpreadTableBucket(int max_symbol, const uint32_t counts[],
                           int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  int s, n, pos;
  int16_t* buckets = NULL;        // entry to linked list of bucket's symbol
  int16_t next[MAX_SYMBOLS];        // linked list of symbols in the same bucket
  double keys[MAX_SYMBOLS];           // key associated to each symbol
  buckets = (int16_t*)malloc(tab_size * sizeof(*buckets));
  if (buckets == NULL) return 0;

  for (n = 0; n < tab_size; ++n) {
    buckets[n] = -1;  // NIL
  }
  for (s = 0; s < max_symbol; ++s) {
    if (counts[s] > 0) {
      INSERT(s, 0.5 * tab_size / counts[s]);
    }
  }
  for (n = 0, pos = 0; n < tab_size && pos < tab_size; ++pos) {
    while (1) {
      const int s = buckets[pos];
      if (s < 0) break;
      symbols[n++] = s;
      buckets[pos] = next[s];   // POP s
      INSERT(s, keys[s] + 1. * tab_size / counts[s]);
    }
  }
  // n < tab_size can happen due to rounding errors
  for (; n != tab_size; ++n) symbols[n] = symbols[n - 1];
  free(buckets);
  return 1;
}

//------------------------------------------------------------------------------

static inline int ReverseBits(int i, int max_bits) {
  const int tab_size = 1 << max_bits;
  int v = 0, n = max_bits;
  while (n-- > 0) {
    v |= (i & 1) << n;
    i >>= 1;
  }
  return v;
}

int BuildSpreadTableReverse(int max_symbol, const uint32_t counts[],
                            int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  int s, n, pos;
  for (s = 0, pos = 0; s < max_symbol; ++s) {
    for (n = 0; n < counts[s]; ++n, ++pos) {
      symbols[ReverseBits(pos, log_tab_size)] = s;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------

int BuildSpreadTableModulo(int max_symbol, const uint32_t counts[],
                           int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  const int kStep = ((tab_size >> 1) + (tab_size >> 3) + 1);
  int s, n, pos;
  for (s = 0, pos = 0; s < max_symbol; ++s) {
    for (n = 0; n < counts[s]; ++n, ++pos) {
      const int v = pos * kStep;
      const int slot = (v ^ CRYPTO_KEY) & (tab_size - 1);
      symbols[slot] = s;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------

int BuildSpreadTablePack(int max_symbol, const uint32_t counts[],
                         int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  const int kStep = ((tab_size >> 1) + (tab_size >> 3) + 1);
  int s, n, pos;
  for (s = 0, pos = 0; s < max_symbol; ++s) {
    for (n = 0; n < counts[s]; ++n, ++pos) {
      symbols[pos] = s;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------

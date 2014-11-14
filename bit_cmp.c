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
// Test proggy for comparing Arithmetic / binary ANS compression
//

#include "./fsc_utils.h"

#define PROBA_BITS 31
#define BITS 32
typedef uint64_t ANSProba;
typedef uint32_t ANSBaseW;   // I/O words
typedef uint64_t ANSStateW;  // internal state

#define PROBA_MAX ((ANSProba)1 << PROBA_BITS)
#define PROBA_MASK (PROBA_MAX - 1)
#define BITS_LIMIT ((ANSStateW)1 << BITS)
#define BITS_MASK (BITS_LIMIT - 1)

static size_t bANSEncode(const uint8_t* in, size_t in_size,
                         uint8_t* const buf_start, uint8_t* const buf_end,
                         ANSProba p0) {
  ANSStateW x = BITS_LIMIT;
  const ANSProba q0 = PROBA_MAX - p0;
  const ANSStateW threshold0 = BITS_LIMIT * p0;
  const ANSStateW threshold1 = BITS_LIMIT * q0;
#if (PROBA_BITS <= 16)
#define FIX 63
  const uint64_t inv_p0 = p0 ? ((1ull << FIX) + p0 - 1) / p0 : 0;
  const uint64_t inv_q0 = q0 ? ((1ull << FIX) + q0 - 1) / q0 : 0;
#endif
  ANSBaseW* buf = (ANSBaseW*)buf_end;
  int i;
  assert(sizeof(ANSBaseW) * 8 == BITS);
  for (i = in_size - 1; i >= 0; --i) {   // encode in reverse
    if (x >= (in[i] ? threshold1 : threshold0)) {
      if (buf <= (ANSBaseW*)buf_start) return 0;  // error
      *--buf = x & BITS_MASK;
      x >>= BITS;
    }
#if defined(FIX)
// x is decomposed as: x = k.p0 + r
// x' = k.L + r , where k = x/p0 = (x * inv_p0) >> FIX
// x' = k.L + x - k.p0 = k.(L-p0) + x = k.q0 + x
    if (in[i]) {
      const ANSStateW q = ((__int128)x * inv_q0) >> FIX;
      x += q * p0 + p0;
    } else {
      const ANSStateW q = ((__int128)x * inv_p0) >> FIX;
      x += q * q0 + 0;
    }
#else
    if (in[i]) {
      x = ((x / q0) << PROBA_BITS) + (x % q0) + p0;
// slower:      x += (x / q0) * p0 + p0;
    } else {
      x = ((x / p0) << PROBA_BITS) + (x % p0);
// slower:      x += (x / p0) * q0;
    }
#endif
  }
  if (buf - 2 < (ANSBaseW*)buf_start) {
    printf("BUFFER ERROR!\n");
    return 0;
  }
  *--buf = (x >>    0) & BITS_MASK;
  *--buf = (x >> BITS) & BITS_MASK;
  return buf_end - (uint8_t*)buf;
}

static int bANSDecode(const ANSBaseW* ptr,
                      uint8_t* out, size_t in_size, ANSProba p0) {

  ANSStateW x = ((ANSStateW)ptr[0] << BITS) | (ANSStateW)ptr[1];
  ptr += 2;
  const ANSProba q0 = PROBA_MAX - p0;
  int i;
  for (i = 0; i < in_size; ++i) {
    if (x < PROBA_MAX) {
      x = (x << BITS) | *ptr++;   // decode forward
    }
    const ANSProba xfrac = x & PROBA_MASK;
    out[i] = (xfrac >= p0);
    if (xfrac < p0) {
      x = p0 * (x >> PROBA_BITS) + xfrac;
    } else {
      x = q0 * (x >> PROBA_BITS) + xfrac - p0;
   }
  }
  assert(x == BITS_LIMIT);
  return 1;
}

//------------------------------------------------------------------------------

static size_t bArithEncode(const uint8_t* in, size_t in_size,
                           uint8_t* const buf_start, uint8_t* const buf_end,
                           ANSProba p0) {
  ANSStateW low = 0;
  ANSStateW hi = ~0;
  ANSBaseW* buf = (ANSBaseW*)buf_start;
  int i;
  for (i = 0; i < in_size; ++i) {
    const ANSStateW diff = hi - low;
    ANSStateW split = low + (diff >> PROBA_BITS) * p0;
#if (2 * PROBA_BITS + BITS > 64)
    split += ((diff & PROBA_MASK) * p0) >> PROBA_BITS;
#endif
    if (!in[i]) {
      hi = split;
    } else {
      low = split + 1;
    }
    if ((low ^ hi) < BITS_LIMIT) {
      if (buf >= (ANSBaseW*)buf_end) return 0;  // error
      *buf++ = hi >> BITS;
      low <<= BITS;
      hi <<= BITS;
      hi |= BITS_MASK;
    }
  }
  if (buf + 1 > (ANSBaseW*)buf_end) return 0;  // error
  *buf++ = hi >> BITS;
  hi <<= BITS;
  *buf++ = hi >> BITS;

  const size_t size = (uint8_t*)buf - buf_start;
  return size; 
}

static int bArithDecode(const ANSBaseW* ptr,
                        uint8_t* out, size_t in_size, ANSProba p0) {

  ANSStateW low = 0;
  ANSStateW hi = ~0;
  ANSStateW x = *ptr++;
  x = (x << BITS) | *ptr++;
  
  int i;
  for (i = 0; i < in_size; ++i) {
    const ANSStateW diff = hi - low;
    ANSStateW split = low + (diff >> PROBA_BITS) * p0;
#if (2 * PROBA_BITS + BITS > 64)
    split += ((diff & PROBA_MASK) * p0) >> PROBA_BITS;
#endif
    out[i] = (x > split);
    if (!out[i]) {
      hi = split;
    } else {
      low = split + 1;
    }    
    if ((low ^ hi) < BITS_LIMIT) {
      x = (x << BITS) | *ptr++;
      low <<= BITS;
      hi <<= BITS;
      hi |= BITS_MASK;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------

static int CheckErrors(size_t N, const uint8_t out[], const uint8_t base[],
                       const char* name) {
  int nb_errors = 0;
  int i;
  for (i = 0; i < N; ++i) {
    nb_errors += (out[i] != base[i]);
  }
  if (nb_errors) {
    printf("%s Decoding errors! (%d)\n", name, nb_errors);
    for (i = 0; i < (N > 40 ? 40 : N); ++i) {
      printf("[%d/%d]%c", out[i], base[i], " *"[out[i] != base[i]]);
    }
    printf("\n");
  }
  return nb_errors;
}

static void Generate(uint8_t* in, size_t size, ANSProba p0, FSCRandom* rg) {
  int i;
  for (i = 0; i < size; ++i) {
    ANSProba k = 0;
    int b = 0;
    while (b < PROBA_BITS) {
      int B = PROBA_BITS - b;
      if (B > 16) B = 16;
      k = (k << B) | FSCRandomBits(rg, B);
      b += B;
    }
    in[i] = (k >= p0);
  }
}

//------------------------------------------------------------------------------

void Help() {
  printf("usage: ./bit_cmp [options] [size]\n");
  printf("-h                 : this help\n");
  exit(0);
}

int main(int argc, const char* argv[]) {
  int N = 100000;
  int L = 16;
  int nb_errors = 0;
  int pmin = 1, pmax = 255;
  int c;

  for (c = 1; c < argc; ++c) {
    if (!strcmp(argv[c], "-h")) {
      Help();
    } else if (!strcmp(argv[c], "-l") && c + 1 < argc) {
      L = atoi(argv[++c]);
    } else if (!strcmp(argv[c], "-p") && c + 1 < argc) {
      pmin = pmax = atoi(argv[++c]);
    } else {
      N = atoi(argv[c]);
      if (N <= 2) N = 2;
    }
  }
  const double MS = 1.e-6 * N;

  uint8_t* const base = (uint8_t*)malloc(2 * N * sizeof(*base));
  uint8_t* const out = base + N;
  if (base == NULL) return 0;
  const size_t kExtraBytes = 32;
  const size_t total_size = (N + kExtraBytes + 7) & ~7;
  uint8_t* const bits = (uint8_t*)malloc(total_size);
  if (bits == NULL) goto End;
  uint8_t* const bits_end = bits + total_size;
  size_t bits_size = 0;

  FSCRandom r;
  FSCInitRandom(&r);

  int p;
  for (p = pmin; (p <= pmax) && (nb_errors == 0); ++p) {
    const ANSProba p0 =  (ANSProba)(p / 256. * PROBA_MAX);
    MyClock start, tmp;
    double S_ANS = 0., S_AC = 0.;
    double t_ANS_enc = 0., t_ANS_dec = 0.;
    double t_AC_enc = 0., t_AC_dec = 0.;
    int i;

    Generate(base, N, p0, &r);
    const double S1 = 8. * GetEntropy(base, N);

    {
      GetElapsed(&start, NULL);
      bits_size = bANSEncode(base, N, bits, bits_end, p0);
      if (bits_size == 0) {
        printf("ANS Encoding error!\n");
        goto End;
      }
      S_ANS = 8.0 * bits_size / N;
      t_ANS_enc = MS / GetElapsed(&tmp, &start);

      GetElapsed(&start, NULL);

      nb_errors = !bANSDecode((ANSBaseW*)(bits_end - bits_size), out, N, p0);
      t_ANS_dec = MS / GetElapsed(&tmp, &start);
      nb_errors += CheckErrors(N, out, base, "ANS");
    }

    {
      GetElapsed(&start, NULL);
      bits_size = bArithEncode(base, N, bits, bits_end, p0);
      if (bits_size == 0) {
        printf("Arith Encoding error!\n");
        goto End;
      }
      S_AC = 8.0 * bits_size / N;
      t_AC_enc = MS / GetElapsed(&tmp, &start);

      GetElapsed(&start, NULL);

      nb_errors = !bArithDecode((ANSBaseW*)bits, out, N, p0);
      t_AC_dec = MS / GetElapsed(&tmp, &start);
      nb_errors += CheckErrors(N, out, base, "AC");
    }

    printf("%.7lf %.7lf %.7lf %.7lf "
           "  %3.1lf     %3.1lf  "
           "  %3.1lf     %3.1lf\n",
           1. * p0 / PROBA_MAX, S_ANS, S_AC, S1,
           t_ANS_enc,  t_ANS_dec,
           t_AC_enc, t_AC_dec);
  }
  printf("# 1 Proba|2  S_ANS |3 S_AC  |4 entropy"
         "|5 ANS enc|6 ANS dec"
         "|7 AC enc |8 AC dec\n");

 End:
  free(base);
  free(bits);
  return (nb_errors != 0);
}

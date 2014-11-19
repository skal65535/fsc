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
// Everything needed to implement divide-by-multiply
//
// You need to define RECIPROCAL_BITS before including this header, with value
// being either 32, 16 or 0 (=use float)
// This header will define the following:
// *  inv_t type (to store the data needed for the reciprocal calculation)
// * void FSCInitDivide(ANSProba p, inv_t* div):
//       to be called to initialize the data to compute 1/p
// * ANSStateW FSCDivide(ANSStateW x, inv_t div)
//       to perform x/p

#include "./fsc_utils.h"

#if (RECIPROCAL_BITS == 32)

typedef struct {
  uint64_t mult_;
  int shift_;
} inv_t;
#define DIV_FIX 64

static inline void FSCInitDivide(ANSProba p, inv_t* div) {
  if (p > 0) {
    int s = 0;
    while (p > (1ull << s)) ++s;
    const uint64_t base = 1ull << (s - 1 + DIV_FIX - 32);
    const uint64_t v1 = base / p;
    const uint64_t v0 = (((base % p) << 32) + p - 1) / p;
    div->mult_ = (v1 << 32) | v0;
    div->shift_ = s - 1;
  } else {
    div->mult_ = 0;
    div->shift_ = 0;
  }
}

static inline ANSStateW FSCDivide(ANSStateW x, inv_t div) {
  return (((unsigned __int128)x * div.mult_) >> DIV_FIX) >> div.shift_;
}

#undef DIV_FIX

#elif (RECIPROCAL_BITS == 16)

typedef struct {
  uint32_t mult_;
  int shift_;
} inv_t;

#define DIV_FIX 32

static inline void FSCInitDivide(ANSProba p, inv_t* div) {
  if (p > 0) {
    int s = 0;
    while (p > (1ul << s)) ++s;
    const uint64_t base = 1ul << (s - 1 + DIV_FIX);
    div->mult_ = (base + p - 1) / p;
    div->shift_ = s - 1;
  } else {
    div->mult_ = 0;
    div->shift_ = 0;
  }
}

static inline ANSStateW FSCDivide(ANSStateW x, inv_t div) {
  return (((uint64_t)x * div.mult_) >> DIV_FIX) >> div.shift_;
}

#undef DIV_FIX

#elif (RECIPROCAL_BITS == 0)

typedef ANSProba inv_t;

static inline void FSCInitDivide(ANSProba p, inv_t* div) { *div = p; }

static inline ANSStateW FSCDivide(ANSStateW x, inv_t div) {
  return x / div;
}

#endif

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
// Tools for implementing Vose's alias sampling method.
//
// http://web.eecs.utk.edu/~vose/Publications/random.pdf
// http://en.wikipedia.org/wiki/Alias_method
//
// Author: Skal (pascal.massimino@gmail.com)

#ifndef ALIAS_H_
#define ALIAS_H_

#include "./fsc.h"

#define LOG2_MAX_SYMBOLS 8   // such that (1 << LOG2_MAX_SYMBOLS) >= MAX_SYMBOLS
#define ALIAS_MAX_SYMBOLS (1 << LOG2_MAX_SYMBOLS)
// #define DEBUG_ALIAS

typedef uint8_t alias_t;        // enough to encode MAX_SYMBOLS
typedef uint16_t alias_tab_t;   // enough to store MAX_TAB_SIZE index

typedef struct {
  alias_tab_t cut_[ALIAS_MAX_SYMBOLS];
  alias_t other_[ALIAS_MAX_SYMBOLS];
  int32_t start_[2 * ALIAS_MAX_SYMBOLS];
} AliasTable;

static inline alias_t AliasSearchSymbol(const AliasTable* const t, uint32_t r,
                                        uint32_t* const rank) {
  const int s = r >> (MAX_LOG_TAB_SIZE - LOG2_MAX_SYMBOLS);
  const int use_alias = (r >= t->cut_[s]);
  *rank = r - t->start_[2 * s + use_alias];
  return use_alias ? t->other_[s] : (alias_t)s;
}

int AliasInit(AliasTable* const t, const uint32_t counts[], int max_symbol);
void AliasGenerateMap(const AliasTable* const t, alias_t map[MAX_TAB_SIZE]);

int AliasVerifyTable(const AliasTable* const t,
                     const uint32_t counts[], int max_symbol);   // debug

// encoding:
int AliasBuildEncMap(const uint32_t counts[], int max_symbol,
                     alias_tab_t map[MAX_TAB_SIZE]);

// Spread function for alias look-up.
int AliasSpreadMap(int max_symbol, const uint32_t counts[],
                   int log_tab_size, uint8_t symbols[]);

#endif   // ALIAS_H_

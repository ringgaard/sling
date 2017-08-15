// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stddef.h>
#include <algorithm>

#include "myelin/dictionary.h"

#include "third_party/jit/assembler.h"
#include "third_party/jit/code.h"

namespace sling {
namespace myelin {

using namespace jit;

#define __ masm->

static const uint64_t mul1 = 0xC6A4A7935BD1E995u;
static const uint64_t mul2 = 0x0228876A7198B743u;
static const uint64_t seed = 0xA5B85C5E198ED849u;

static uint64 Mix(uint64 fp1, uint64 fp2) {
  const uint64 a = fp1 * mul1 + fp2 * mul2;
  return a + (~a >> 47);
}

static uint64 Hash(const char *bytes, size_t len) {
  uint64 fp = seed;
  const char *end = bytes + len;
  while (bytes + 8 <= end) {
    fp = Mix(fp, *(reinterpret_cast<const uint64 *>(bytes)));
    bytes += 8;
  }
  uint64 last_bytes = 0;
  while (bytes < end) {
    last_bytes <<= 8;
    last_bytes |= *bytes;
    bytes++;
  }

  return Mix(fp, last_bytes);
}

// Generate code for mixing two fingerprints.
static void GenerateMix(Register fp1, Register fp2,
                        Register mix1, Register mix2, 
                        Register tmp, Assembler *masm) {
  // Compute a = fp1 * mul1 + fp2 * mul2.
  __ movq(rax, fp1);
  __ mulq(mix1);
  __ movq(tmp, rax);
  __ movq(rax, fp2);
  __ mulq(mix2);
  __ addq(rax, tmp);

  // Compute a = a + (~a >> 47).
  __ movq(tmp, rax);
  __ notq(tmp);
  __ shrq(tmp, Immediate(47));
  __ addq(rax, tmp);
}

// Generate code for computing hash of data buffer.
static void GenerateHash(Assembler *masm) {
  // Hash function takes the buffer and length as arguments.
  Register buffer = arg_reg_1;
  Register len = arg_reg_2;

  // Assign registers.
  Label l1, l2, l3, l4;
  Register tmp = rcx;
  Register end = len;
  Register fp1 = r8;
  Register fp2 = r9;
  Register mix1 = r10;
  Register mix2 = r11;

  // Load mix constants.
  __ movq(mix1, mul1);
  __ movq(mix2, mul2);
  
  // Compute end of buffer.
  __ addq(end, buffer);
  __ subq(end, Immediate(8));

  // Compute hash eight bytes at a time.
  __ movq(fp1, seed);
  __ bind(&l1);
  __ cmpq(buffer, end);
  __ j(greater, &l2);
  __ movq(fp2, Operand(buffer));
  GenerateMix(fp1, fp2, mix1, mix2, tmp, masm);
  __ movq(fp1, rax);
  __ addq(buffer, Immediate(8));
  __ jmp(&l1);

  // Compute hash for residual.
  __ bind(&l2);
  __ addq(end, Immediate(8));
  __ xorq(fp2, fp2);
  __ bind(&l3);
  __ cmpq(buffer, end);
  __ j(equal, &l4);
  __ shlq(fp2, Immediate(8));
  __ movzxbq(rax, Operand(buffer));
  __ orq(fp2, rax);
  __ incq(buffer);
  __ jmp(&l3);
  __ bind(&l4);
  GenerateMix(fp1, fp2, mix1, mix2, tmp, masm);
}

// Generate code for looking up word in dictionary.
void GenerateLookup(DictionaryBucket *buckets, int num_buckets, int oov, 
                    Assembler *masm) {
  // Assign registers.
  Label l1, l2, l3, l4;
  Register item = rsi;
  Register end = rdi;
  Register hash = r8;
  Register size = r9;

  // Compute hash of input.
  GenerateHash(masm);
  __ movq(hash, rax);

  // Compute bucket number (bucket = hash % num_buckets).
  __ movq(size, Immediate(num_buckets));
  __ xorq(rdx, rdx);
  __ divq(size);

  // Get item range for bucket.
  __ movp(rcx, buckets);
  __ movq(item, Operand(rcx, rdx, times_8));
  __ incq(rdx);
  __ movq(end, Operand(rcx, rdx, times_8));

  // Search item range for match.
  __ cmpq(item, end);
  __ j(equal, &l2);
  __ bind(&l1);
  __ cmpq(hash, Operand(item, offsetof(DictionaryItem, hash)));
  __ j(equal, &l3);
  __ addq(item, Immediate(sizeof(DictionaryItem)));
  __ cmpq(item, end);
  __ j(not_equal, &l1);

  // Not found, return OOV.
  __ bind(&l2);
  __ movq(rax, Immediate(oov));
  __ jmp(&l4);
  
  // Match found, return value.
  __ bind(&l3);
  __ movq(rax, Operand(item, offsetof(DictionaryItem, value)));

  __ bind(&l4);
  __ ret(0);
}

Dictionary::~Dictionary() {
  delete [] buckets_;
  delete [] items_;
}

void Dictionary::Init(Flow::Blob *lexicon) {
  // Count the number of items in the lexicon.
  oov_ = lexicon->attrs.Get("oov", -1);
  char delimiter = lexicon->attrs.Get("delimiter", 0);
  size_ = 0;
  for (int i = 0; i < lexicon->size; ++i) {
    if (lexicon->data[i] == delimiter) size_++;
  }
  num_buckets_ = size_;

  // Allocate items and buckets. We allocate one extra bucket to mark the end of
  // the items. This ensures that all items in a bucket b are in the range from
  // bucket[b] to bucket[b + 1], even for the last bucket.
  items_ = new DictionaryItem[size_];
  buckets_ = new DictionaryBucket[num_buckets_ + 1];

  // Build items for each word in the lexicon.
  const char *data = lexicon->data;
  const char *end = data + lexicon->size;
  int64 index = 0;
  while (data < end) {
    // Find next word.
    const char *next = data;
    while (next < end && *next != delimiter) next++;
    if (next == end) break;

    // Initialize item for word.
    items_[index].hash = Hash(data, next - data);
    items_[index].value = index;

    data = next + 1;
    index++;
  }

  // Sort the items in bucket order.
  int modulo = num_buckets_;
  std::sort(items_, items_ + size_, 
    [modulo](const DictionaryItem &a, const DictionaryItem &b) {
      return (a.hash % modulo) < (b.hash % modulo);
    }
  );

  // Build bucket array.
  int bucket = -1;
  for (int i = 0; i < size_; ++i) {
    int b = items_[i].hash % modulo;
    while (bucket < b) buckets_[++bucket] = &items_[i];
  }
  while (bucket < num_buckets_) buckets_[++bucket] = &items_[size_];

  // Generate lookup function.
  Assembler masm(nullptr, 0);
  GenerateLookup(buckets_, num_buckets_, oov_, &masm);
  lookup_.Allocate(&masm);
};

int64 Dictionary::LookupSlow(const string &word) const {
  uint64 hash = Hash(word.data(), word.size());
  int b = hash % num_buckets_;
  DictionaryItem *item = buckets_[b];
  DictionaryItem *end = buckets_[b + 1];
  while (item < end) {
    if (hash == item->hash) return item->value;
    item++;
  }
  return oov_;
}

}  // namespace myelin
}  // namespace sling


// Copyright 2010-2014 Google
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

#include "sling/base/types.h"
#include "sling/base/slice.h"

#ifndef SLING_UTIL_FINGERPRINT_H_
#define SLING_UTIL_FINGERPRINT_H_

namespace sling {

// Concatenate two fingerprints.
uint64 FingerprintCat(uint64 fp1, uint64 fp2);

// Compute 64-bit fingerprint for data. This should be better (collision-wise)
// than the default hash<string>, without being much slower. It never returns
// 0 or 1.
uint64 Fingerprint(const char *bytes, size_t len);
inline uint64 Fingerprint(const Slice &slice) {
  return Fingerprint(slice.data(), slice.size());
}

// Compute 32-bit fingerprint by folding 64-bit fingerprint.
uint32 Fingerprint32(const char *bytes, size_t len);
inline uint32 Fingerprint32(const Slice &slice) {
  return Fingerprint32(slice.data(), slice.size());
}

// A word fingerprints is a 16-bit compressed fingerprints constructed from a
// 64-bit fingerprint. Never returns a reserved fingerprint.

enum ReservedWordFingerprint {
  WORDFP_BREAK      = 0,
  WORDFP_IMPORTANT  = 1,
  WORDFP_FIRST      = 16,
};

uint16 WordFingerprint(uint64 fp);

}  // namespace sling

#endif  // SLING_UTIL_FINGERPRINT_H_

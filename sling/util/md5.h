// Copyright 2020 Ringgaard Research ApS
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

#ifndef SLING_UTIL_MD5_H_
#define SLING_UTIL_MD5_H_

#include "sling/base/types.h"

namespace sling {

struct MD5Context {
	uint32 buf[4];
	uint32 bits[2];
	unsigned char in[64];
};

void MD5Init(MD5Context *context);
void MD5Update(MD5Context *context, const unsigned char *buf, size_t len);
void MD5Final(unsigned char digest[16], MD5Context *context);
void MD5Transform(uint32 buf[4], const unsigned char in[64]);

void MD5Digest(unsigned char digest[16], const void *buf, size_t len);

}  // namespace sling

#endif  // SLING_UTIL_MD5_H_


// Copyright 2025 Ringgaard Research ApS
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

#ifndef SLING_NLP_SEARCH_SEARCH_PROTOCOL_H_
#define SLING_NLP_SEARCH_SEARCH_PROTOCOL_H_

#include "sling/base/types.h"

namespace sling {

// THE SLING search protocol is a client-server protocol with a request packet
// sent from a client and the server reponsing with a response packet. Each
// packet consists of a fixed header followed by a verb-specific body.

// Search protocol protocol verbs.
enum SPVerb : uint32 {
  // Command verbs.
  SPSEARCH    = 0,     // search index
  SPFETCH     = 1,     // fetch items

  // Reply verbs.
  SPOK        = 128,   // success reply
  SPERROR     = 129,   // general error reply
  SPRESULT    = 130,   // search result
  SPITEMS     = 131,   // fetched items
};

// Database protocol packet header.
struct SPHeader {
  SPVerb verb;   // command or reply type
  uint32 size;   // size of packet body

  static SPHeader *from(char *buf) { return reinterpret_cast<SPHeader *>(buf); }
};

// Search protocol exchanges:
//
// SPSEARCH query -> SPRESULT result
//
// query: json {
//   "q": "<query>",
//   "tag": "<shard>",
//   "limit": <limit>
// }
//
// result: json {
//   "total": <total result>,
//   "hits": [
//     { "docid": "<docid>, "score": <score> },
//     ...
//   ]
// }
//
// SPFETCH {key}* -> SPITEMS {record}*
//
//   key: {
//     ksize:uint8;
//     key: byte[ksize];
//   }
//
//   record: {
//     vsize:uint32;
//     value:byte[vsize];
//   }
//
// All requests can return a SPERROR message:char[] reply if an error occurs.

}  // namespace sling

#endif  // SLING_NLP_SEARCH_SEARCH_PROTOCOL_H_

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

#ifndef SLING_NLP_KB_XREF_H_
#define SLING_NLP_KB_XREF_H_

#include <string>
#include <unordered_map>

#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/string/text.h"
#include "sling/util/arena.h"
#include "sling/util/fingerprint.h"

namespace sling {
namespace nlp {

// Cross-reference for identifiers.
class XRef {
 public:
  // Property type for identifier.
  struct Property {
    Handle handle;           // frame handle for property, must be global
    int priority;            // priority for selecting canonical id
    string name;             // property name
    uint64 hash;             // hash code for property
  };

  // Identifier with property type and value. The identifiers are stored in a
  // hash table to facilitate fast lookup by name, and identifiers are also
  // linked into a circular list with all the identifiers for the same entity.
  struct Identifier {
    const Property *type;    // property type for identifier
    const char *value;       // property value for identifier
    uint64 hash;             // hash code for identifier
    bool redirect;           // redirected identifiers have lower priority
    bool visited;            // identifier has been added to cluster frame

    Identifier *chain;       // bucket chain for hash table
    Identifier *ring;        // cluster ring for identifier cluster

    // Check if this is a singleton cluster.
    bool singleton() const { return ring == this; }

    // Order identifiers by priority with redirects after non-redirects.
    int order() const { return type->priority * 2 + redirect; }
  };

  XRef();
  ~XRef();

  // Add property type to cross reference. Identifier property types should be
  // added in priority order. The property frames must be in a global store.
  const Property *AddProperty(const Frame &property);

  // Look up property. Return null if properties is not found.
  const Property *LookupProperty(Handle handle) const;
  const Property *LookupProperty(Text name) const;

  // Get identifier for property type and value. A new identifier is added if
  // it is not already in the cross reference table.
  Identifier *GetIdentifier(const Property *type, Text value,
                            bool redirect = false);

  // Get identifier for reference. If the reference has the form PROP/VALUE or
  // /PROP/VALUE, an identifier for the property is returned. Otherwise a main
  // identifier is returned. Returns null if property is not tracked.
  Identifier *GetIdentifier(Text ref, bool redirect = false);

  // Merge two identifiers into the same cluster.
  void Merge(Identifier *a, Identifier *b);

  // Add identifier cluster to store. Each cluster contains id slots with the
  // identifiers in the cluster in priority order.
  void Build(Store *store);

  // Main property type.
  const Property *main() const { return main_; }

 private:
  // Number of hash buckets for identifier hash table.
  static constexpr uint64 LOG_NUM_BUCKETS = 20;
  static constexpr uint64 NUM_BUCKETS = (1 << LOG_NUM_BUCKETS);

  // Create new property for handle.
  Property *CreateProperty(Handle handle, Text name);

  // Compute hash code for identifier.
  uint64 Hash(const Property *type, Text value) {
    return FingerprintCat(type->hash, Fingerprint(value.data(), value.size()));
  }

  // Properties.
  HandleMap<const Property *> properties_;

  // Property name mapping.
  std::unordered_map<Text, const Property *> property_map_;

  // Property for main identifier property type (e.g. QID).
  const Property *main_;

  // Arena for allocating identifiers.
  Arena<Identifier> id_arena_;

  // String arena for allocating identifer values.
  StringArena value_arena_;

  // Hash table for identifiers.
  Identifier *buckets_[NUM_BUCKETS];
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_KB_XREF_H_


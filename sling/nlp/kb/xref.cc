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

#include "sling/nlp/kb/xref.h"

#include <algorithm>
#include <vector>

#include "sling/frame/serialization.h"
#include "sling/string/strcat.h"

namespace sling {
namespace nlp {

XRef::XRef() {
  // Clear hash table.
  for (int b = 0; b < NUM_BUCKETS; ++b) buckets_[b] = nullptr;

  // Add main identifier property types.
  main_ = CreateProperty(Handle::id(), "");
}

XRef::~XRef() {
  for (auto &it : properties_) delete it.second;
}

XRef::Property *XRef::CreateProperty(Handle handle, Text name) {
  Property *p = new Property();
  p->handle = handle;
  p->priority = properties_.size();
  p->name = name.str();
  p->hash = Fingerprint(name.data(), name.size());
  properties_[handle] = p;
  property_map_[p->name] = p;
  return p;
}

const XRef::Property *XRef::AddProperty(const Frame &property) {
  CHECK(property.IsGlobal()) << property.Id();
  return CreateProperty(property.handle(), property.Id());
}

const XRef::Property *XRef::LookupProperty(Handle handle) const {
  // All properties should be global.
  if (!handle.IsGlobalRef()) return nullptr;

  // Lookup up property handle.
  auto f = properties_.find(handle);
  if (f == properties_.end()) return nullptr;
  return f->second;
}

const XRef::Property *XRef::LookupProperty(Text name) const {
  // Lookup up property name.
  auto f = property_map_.find(name);
  if (f == property_map_.end()) return nullptr;
  return f->second;
}

XRef::Identifier *XRef::GetIdentifier(const Property *type,
                                      Text value,
                                      bool redirect) {
  // Empty values not allowed.
  if (value.empty()) return nullptr;

  // Try to find existing identifier.
  uint64 hash = Hash(type, value);
  uint64 bucket = hash % NUM_BUCKETS;
  Identifier *id = buckets_[bucket];
  while (id != nullptr) {
    if (id->hash == hash && id->type == type && id->value == value) {
      if (redirect) id->redirect = true;
      return id;
    }
    id = id->chain;
  }

  // Create new identifier.
  id = id_arena_.alloc();
  id->type = type;
  id->value = value_arena_.dup(value.data(), value.size());
  id->hash = hash;
  id->redirect = redirect;
  id->fixed = false;
  id->visited = false;
  id->chain = nullptr;
  id->ring = id;
  id->chain = buckets_[bucket];
  buckets_[bucket] = id;

  return id;
}

XRef::Identifier *XRef::GetIdentifier(Text ref, bool redirect) {
  // Try to split reference of the form PROP/VALUE or /PROP/VALUE.
  auto delim = ref.find('/');
  if (delim == 0) delim = ref.find('/', 1);

  if (delim == -1) {
    return GetIdentifier(main_, ref, redirect);
  } else {
    const Property *prop = LookupProperty(ref.substr(0, delim));
    if (prop == nullptr) return nullptr;
    Text value(ref, delim + 1);
    return GetIdentifier(prop, value, redirect);
  }
}

bool XRef::Merge(Identifier *a, Identifier *b) {
  // Check that identifiers are not already in the same cluster.
  bool has_main = false;
  Identifier *id = a;
  do {
    if (id == b) return true;
    if (id->type == main_ && !id->redirect) has_main = true;
    id = id->ring;
  } while (id != a);

  // Check that merging would not lead to two main ids becoming part of the same
  // cluster.
  if (has_main) {
    Identifier *id = b;
    do {
      if (id->type == main_ && !id->redirect) return false;
      id = id->ring;
    } while (id != b);
  }

  // Merge clusters.
  std::swap(a->ring, b->ring);
  return true;
}

void XRef::Build(Store *store) {
  // Run through all identifiers in hash table.
  std::vector<Identifier *> cluster;
  Builder builder(store);
  string name;
  for (int b = 0; b < NUM_BUCKETS; ++b) {
    for (Identifier *i = buckets_[b]; i != nullptr; i = i->chain) {
      // Skip identifiers that have already been visited.
      if (i->visited) continue;

      // Skip singletons.
      if (i->singleton()) continue;

      // Get all identifiers in cluster.
      cluster.clear();
      Identifier *id = i;
      do {
        cluster.push_back(id);
        id->visited = true;
        id = id->ring;
      } while (id != i);

      // Sort identifiers in priority order.
      std::sort(cluster.begin(), cluster.end(),
        [](const Identifier *a, const Identifier *b) {
          int oa = a->order();
          int ob = b->order();
          return oa != ob ? oa < ob : strcmp(a->value, b->value) < 0;
        });

      // Build frame with id slots for all the identifiers in the cluster.
      builder.Clear();
      for (Identifier *id : cluster) {
        if (id->type->name.empty()) {
          builder.AddId(id->value);
        } else {
          name.clear();
          name.append(id->type->name);
          name.push_back('/');
          name.append(id->value);
          builder.AddId(name);
        }
      }
      builder.Create();
    }
  }
}

string XRef::Identifier::ToString() const {
  string str;
  str.push_back('[');
  const Identifier *id = this;
  do {
    if (id != this) str.push_back(' ');
    if (id->redirect) str.push_back('>');
    if (!id->type->name.empty()) {
      str.append(id->type->name);
      str.push_back('/');
    }
    str.append(id->value);
    id = id->ring;
  } while (id != this);
  str.push_back(']');

  return str;
}

Text XRefMapping::Map(Text id) const {
  // Try to look up identifier in cross-reference.
  Handle h = xrefs_.LookupExisting(id);
  if (!h.IsNil()) return xrefs_.FrameId(h);

  // Try to convert property mnemonic.
  int sep = id.find('/');
  if (sep == -1) sep = id.find(':');
  if (sep != -1) {
    Text domain = id.substr(0, sep).trim();
    Text identifier = id.substr(sep + 1).trim();
    if (!domain.empty() && !identifier.empty()) {
      auto f = mnemonics_.find(domain);
      if (f != mnemonics_.end()) domain = f->second;
      string idstr = StrCat(domain, "/", identifier);
      Handle h = xrefs_.LookupExisting(idstr);
      if (!h.IsNil()) return xrefs_.FrameId(h);
    }
  }

  // No mapping found.
  return Text();
}

void XRefMapping::Load(const string &filename) {
  // Load store with cross-references.
  CHECK(!loaded());
  LoadStore(filename, &xrefs_);
  xrefs_.Freeze();

  // Build mapping from mnemonics to property ids.
  Frame mnemonics(&xrefs_, "/w/mnemonics");
  if (mnemonics.valid()) {
    for (const Slot &s : mnemonics) {
      if (s.name.IsId()) continue;
      CHECK(xrefs_.IsString(s.name));
      CHECK(xrefs_.IsString(s.value));
      Text mnemonic = xrefs_.GetString(s.name)->str();
      Text property = xrefs_.GetString(s.value)->str();
      mnemonics_[mnemonic] = property;
    }
  }
}

}  // namespace nlp
}  // namespace sling


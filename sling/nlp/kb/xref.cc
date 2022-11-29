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

void URIMapping::Load(const Frame &frame) {
  Store *store = frame.store();
  Handle n_prefix = store->Lookup("prefix");
  Handle n_suffix = store->Lookup("suffix");
  string uri, prefix, suffix, property;
  for (const Slot &s : frame) {
    if (s.name.IsId()) continue;
    uri = store->GetText(s.name).str();
    if (store->IsFrame(s.value)) {
      Frame f(store, s.value);
      CHECK(f.valid());
      property = f.GetString(Handle::is());
      prefix = f.GetString(n_prefix);
      suffix = f.GetString(n_suffix);
    } else {
      property = store->GetText(s.value).str();
      prefix.clear();
      suffix.clear();
    }
    mappings_.emplace_back(uri, prefix, suffix, property);
  }

  // Sort entries by URI.
  std::sort(mappings_.begin(), mappings_.end());
}

void URIMapping::Save(Builder *builder) {
  Store *store = builder->store();
  Handle n_prefix = store->Lookup("prefix");
  Handle n_suffix = store->Lookup("suffix");

  for (auto &e : mappings_) {
    String uri(store, e.uri);
    String property(store, e.property);
    if (e.prefix.empty() && e.suffix.empty()) {
      builder->Add(uri, property);
    } else {
      Builder b(store);
      b.AddIs(property);
      if (!e.prefix.empty()) b.Add(n_prefix, e.prefix);
      if (!e.suffix.empty()) b.Add(n_suffix, e.suffix);
      builder->Add(uri, b.Create());
    }
  }
}

void URIMapping::Bind(Store *store, bool create) {
  for (auto &e : mappings_) {
    if (!e.property.empty()) {
      if (create) {
        e.pid = store->Lookup(e.property);
      } else {
        e.pid = store->LookupExisting(e.property);
      }
    }
  }
}

int URIMapping::Locate(Text uri) const {
  // Bail out if there are no URI mappings.
  if (empty()) return -1;

  // Find mapping with prefix match.
  int lo = 0;
  int hi = mappings_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    const Entry &e = mappings_[mid];
    if (uri < e.uri) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }

  // Check that the entry is a match for the URI.
  int match = lo - 1;
  if (match < 0) return -1;
  const Entry &e = mappings_[match];
  if (!uri.starts_with(e.uri)) return -1;
  if (!uri.ends_with(e.suffix)) return -1;

  return match;
}

bool URIMapping::Map(Text uri, string *id) const {
  // Find mapping with prefix match.
  int match = Locate(uri);
  if (match == -1) return false;
  const Entry &e = mappings_[match];

  // Construct mapped id.
  id->clear();
  if (!e.property.empty()) {
    id->append(e.property);
    id->push_back('/');
  }
  if (!e.prefix.empty()) id->append(e.prefix);
  int len = uri.size() - e.uri.size() - e.suffix.size();
  uri.substr(e.uri.size(), len).AppendToString(id);

  return true;
}

bool URIMapping::Lookup(Text uri, Handle *pid, string *id) const {
  // Find mapping with prefix match.
  int match = Locate(uri);
  if (match == -1) return false;
  const Entry &e = mappings_[match];

  // Construct mapped id.
  *pid = e.pid;
  id->clear();
  if (!e.prefix.empty()) id->append(e.prefix);
  int len = uri.size() - e.uri.size() - e.suffix.size();
  uri.substr(e.uri.size(), len).AppendToString(id);

  return true;
}

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
  p->count = 0;
  if (!handle.IsNil()) properties_[handle] = p;
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
  uint64 bucket = hash & (NUM_BUCKETS - 1);
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
  type->count++;

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
        id->GetName(&name);
        builder.AddId(name);
      }
      builder.Create();
    }
  }
}

void XRef::Identifier::GetName(string *name) const {
  if (type->name.empty()) {
    *name = value;
  } else {
    name->clear();
    name->append(type->name);
    name->push_back('/');
    name->append(value);
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

XRef::Identifier *XRef::Identifier::Canonical() {
  Identifier *canonical = this;
  Identifier *id = this->ring;
  do {
    if (id->order() < canonical->order()) canonical = id;
    id = id->ring;
  } while (id != this);
  return canonical;
}

bool XRefMapping::Map(string *id) const {
  // Try to map URI.
  if (id->size() > 4 && id->compare(0, 4, "http") == 0) {
    string mapped;
    if (urimap_.Map(*id, &mapped)) {
      Handle h = xrefs_.LookupExisting(mapped);
      if (!h.IsNil()) {
        *id = xrefs_.FrameId(h).str();
      } else {
        *id = mapped;
      }
      return true;
    }
  }

  // Try to look up identifier in cross-reference.
  Handle h = xrefs_.LookupExisting(*id);
  if (!h.IsNil()) {
    *id = xrefs_.FrameId(h).str();
    return true;
  }

  // Try to convert property mnemonic.
  int slash = id->find('/');
  int colon = id->find(':');
  int sep = slash != -1 && slash < colon ? slash : colon;
  if (sep != -1) {
    Text domain = Text(*id, 0, sep).trim();
    Text identifier = Text(*id, sep + 1).trim();
    if (!domain.empty() && !identifier.empty()) {
      auto f = mnemonics_.find(domain);
      if (f != mnemonics_.end()) domain = f->second;
      string idstr = StrCat(domain, "/", identifier);
      Handle h = xrefs_.LookupExisting(idstr);
      if (!h.IsNil()) {
        *id = xrefs_.FrameId(h).str();
      } else {
        *id = idstr;
      }
      return true;
    }
  }

  // No mapping found.
  return false;
}

void XRefMapping::Load(const string &filename) {
  // Load store with cross-references.
  CHECK(!loaded());
  LoadStore(filename, &xrefs_);
  xrefs_.Freeze();

  // Set up URI mapping.
  Frame urimap(&xrefs_, "/w/urimap");
  if (urimap.valid()) {
    urimap_.Load(urimap);
  }

  // Build mapping from mnemonics to property ids.
  Frame mnemonics(&xrefs_, "/w/mnemonics");
  if (mnemonics.valid()) {
    for (const Slot &s : mnemonics) {
      if (s.name.IsId()) continue;
      Text mnemonic = xrefs_.GetText(s.name);
      Text property = xrefs_.GetText(s.value);
      mnemonics_[mnemonic] = property;
    }
  }
}

}  // namespace nlp
}  // namespace sling


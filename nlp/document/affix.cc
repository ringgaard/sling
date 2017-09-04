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

#include "nlp/document/affix.h"

#include <string>

#include "base/logging.h"
#include "util/fingerprint.h"
#include "util/unicode.h"

namespace sling {
namespace nlp {

// Initial number of buckets in term and affix hash maps. This must be a power
// of two.
static const int kInitialBuckets = 1024;

// Fill factor for term and affix hash maps.
static const int kFillFactor = 2;

static int TermHash(const string &term) {
  return Fingerprint(term.data(), term.size());
}

AffixTable::AffixTable(Type type, int max_length) {
  type_ = type;
  max_length_ = max_length;
  Resize(0);
}

AffixTable::~AffixTable() { Reset(0); }

void AffixTable::Reset(int max_length) {
  // Save new maximum affix length.
  max_length_ = max_length;

  // Delete all data.
  for (size_t i = 0; i < affixes_.size(); ++i) delete affixes_[i];
  affixes_.clear();
  buckets_.clear();
  Resize(0);
}

void AffixTable::Read(const AffixTableEntry &table_entry) {
  CHECK_EQ(table_entry.type(), type_ == PREFIX ? "PREFIX" : "SUFFIX");
  CHECK_GE(table_entry.max_length(), 0);
  Reset(table_entry.max_length());

  // First, create all affixes.
  for (int affix_id = 0; affix_id < table_entry.affix_size(); ++affix_id) {
    const auto &affix_entry = table_entry.affix(affix_id);
    CHECK_GE(affix_entry.length(), 0);
    CHECK_LE(affix_entry.length(), max_length_);
    CHECK(FindAffix(affix_entry.form()) == nullptr);  // forbid duplicates
    Affix *affix = AddNewAffix(affix_entry.form(), affix_entry.length());
    CHECK_EQ(affix->id(), affix_id);
  }
  CHECK_EQ(affixes_.size(), table_entry.affix_size());

  // Next, link the shorter affixes.
  for (int affix_id = 0; affix_id < table_entry.affix_size(); ++affix_id) {
    const auto &affix_entry = table_entry.affix(affix_id);
    if (affix_entry.shorter_id() == -1) {
      CHECK_EQ(affix_entry.length(), 1);
      continue;
    }
    CHECK_GT(affix_entry.length(), 1);
    CHECK_GE(affix_entry.shorter_id(), 0);
    CHECK_LT(affix_entry.shorter_id(), affixes_.size());
    Affix *affix = affixes_[affix_id];
    Affix *shorter = affixes_[affix_entry.shorter_id()];
    CHECK_EQ(affix->length(), shorter->length() + 1);
    affix->set_shorter(shorter);
  }
}

void AffixTable::Write(AffixTableEntry *table_entry) const {
  table_entry->Clear();
  table_entry->set_type(type_ == PREFIX ? "PREFIX" : "SUFFIX");
  table_entry->set_max_length(max_length_);
  for (const Affix *affix : affixes_) {
    auto *affix_entry = table_entry->add_affix();
    affix_entry->set_form(affix->form());
    affix_entry->set_length(affix->length());
    affix_entry->set_shorter_id(
        affix->shorter() == nullptr ? -1 : affix->shorter()->id());
  }
}

void AffixTable::Serialize(string *data) const {
  AffixTableEntry table_entry;
  Write(&table_entry);
  *data = table_entry.SerializeAsString();
}

void AffixTable::Deserialize(const string &data) {
  AffixTableEntry table_entry;
  CHECK(table_entry.ParseFromString(data));
  Read(table_entry);
}

Affix *AffixTable::AddAffixesForWord(const char *word, size_t size) {
  // The affix length is measured in characters and not bytes so we need to
  // determine the length in characters.
  int length = UTF8::Length(word, size);

  // Determine longest affix.
  int affix_len = length;
  if (affix_len > max_length_) affix_len = max_length_;
  if (affix_len == 0) return nullptr;

  // Find start and end of longest affix.
  const char *start;
  const char *end;
  if (type_ == PREFIX) {
    start = end = word;
    for (int i = 0; i < affix_len; ++i) end = UTF8::Next(end);
  } else {
    start = end = word + size;
    for (int i = 0; i < affix_len; ++i) start = UTF8::Previous(start);
  }

  // Try to find successively shorter affixes.
  Affix *top = nullptr;
  Affix *ancestor = nullptr;
  string s;
  while (affix_len > 0) {
    // Try to find affix in table.
    s.assign(start, end - start);
    Affix *affix = FindAffix(s);
    if (affix == nullptr) {
      // Affix not found, add new one to table.
      affix = AddNewAffix(s, affix_len);

      // Update ancestor chain.
      if (ancestor != nullptr) ancestor->set_shorter(affix);
      ancestor = affix;
      if (top == nullptr) top = affix;
    } else {
      // Affix found. Update ancestor if needed and return match.
      if (ancestor != nullptr) ancestor->set_shorter(affix);
      if (top == nullptr) top = affix;
      break;
    }

    // Next affix.
    if (type_ == PREFIX) {
      end = UTF8::Previous(end);
    } else {
      start = UTF8::Next(start);
    }

    affix_len--;
  }

  return top;
}

Affix *AffixTable::GetAffix(int id) const {
  if (id < 0 || id >= static_cast<int>(affixes_.size())) {
    return nullptr;
  } else {
    return affixes_[id];
  }
}

string AffixTable::AffixForm(int id) const {
  Affix *affix = GetAffix(id);
  if (affix == nullptr) {
    return "";
  } else {
    return affix->form();
  }
}

int AffixTable::AffixId(const string &form) const {
  Affix *affix = FindAffix(form);
  if (affix == nullptr) {
    return -1;
  } else {
    return affix->id();
  }
}

Affix *AffixTable::AddNewAffix(const string &form, int length) {
  int hash = TermHash(form);
  int id = affixes_.size();
  if (id > static_cast<int>(buckets_.size()) * kFillFactor) Resize(id);
  int b = hash & (buckets_.size() - 1);

  // Create new affix object.
  Affix *affix = new Affix(id, form.c_str(), length);
  affixes_.push_back(affix);

  // Insert affix in bucket chain.
  affix->next_ = buckets_[b];
  buckets_[b] = affix;

  return affix;
}

Affix *AffixTable::FindAffix(const string &form) const {
  // Compute hash value for word.
  int hash = TermHash(form);

  // Try to find affix in hash table.
  Affix *affix = buckets_[hash & (buckets_.size() - 1)];
  while (affix != nullptr) {
    if (strcmp(affix->form_.c_str(), form.c_str()) == 0) return affix;
    affix = affix->next_;
  }
  return nullptr;
}

void AffixTable::Resize(int size_hint) {
  // Compute new size for bucket array.
  int new_size = kInitialBuckets;
  while (new_size < size_hint) new_size *= 2;
  int mask = new_size - 1;

  // Distribute affixes in new buckets.
  buckets_.resize(new_size);
  for (size_t i = 0; i < buckets_.size(); ++i) {
    buckets_[i] = nullptr;
  }
  for (size_t i = 0; i < affixes_.size(); ++i) {
    Affix *affix = affixes_[i];
    int b = TermHash(affix->form_) & mask;
    affix->next_ = buckets_[b];
    buckets_[b] = affix;
  }
}

}  // namespace nlp
}  // namespace sling


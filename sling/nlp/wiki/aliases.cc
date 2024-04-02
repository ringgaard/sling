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

#include <set>
#include <string>
#include <unordered_map>

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/file/textmap.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/string/numbers.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/task/reducer.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Compute Levenshtein edit distance between string in s of length m and string
// in t of length n. See https://en.wikipedia.org/wiki/Levenshtein_distance.
static int LevenshteinDistance(const int *s, int m, const int *t, int n) {
  // Skip common prefix since this does not affect edit distance.
  while (m > 0 && n > 0 && *s == *t) s++, t++, m--, n--;

  // Keep track of previous and current cost.
  std::vector<int> prev(n + 1);
  std::vector<int> curr(n + 1);

  // Initialize the previous distances, which is just the number of characters
  // to delete from t.
  for (int i = 0; i <= n; ++i) prev[i] = i;

  // Perform dynamic program to calculate the edit distance.
  for (int i = 0; i < m; ++i) {
    // Calculate current distances from the previous distances. First, edit
    // distance is delete i+1 characters from s to match empty t.
    curr[0] = i + 1;

    // Compute edit distance for remaining characters.
    for (int j = 0; j < n; ++j) {
      int deletion = prev[j + 1] + 1;
      int insertion = curr[j] + 1;
      int substitution = s[i] == t[j] ? prev[j] : prev[j] + 1;
      int cost = std::min(std::min(deletion, insertion), substitution);
      curr[j + 1] = cost;
    }

    // Swap current and previous for next iteration.
    prev.swap(curr);
  }

  return prev[n];
}

// Extract aliases from items.
class AliasExtractor : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    string languages = task->Get("languages", "en");
    for (Text lang : Text(languages).split(',')) {
      languages_.add(commons_->Lookup("/lang/" + lang.str()));
    }
    skip_aux_ = task->Get("skip_aux", false);
    wikitypes_.Init(commons_);

    // Initialize filter.
    if (skip_aux_) filter_.Init(commons_);
    num_aux_items_ = task->GetCounter("aux_items");
    num_non_entity_items_ = task->GetCounter("non-entity_items");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Optionally skip aux items.
    if (skip_aux_ && filter_.IsAux(frame)) {
      num_aux_items_->Increment();
      return;
    }

    // Create frame with all aliases matching language.
    Store *store = frame.store();
    Builder a(store);
    bool skip = false;
    bool has_primary_name = false;
    Handle fallback_name = Handle::nil();
    for (const Slot &s : frame) {
      Handle property = s.name;
      Handle value = store->Resolve(s.value);

      // Do not extract aliases from non-entity items.
      if (property == n_instance_of_) {
        // Discard alias for non-entity items.
        if (wikitypes_.IsNonEntity(value) || wikitypes_.IsBiographic(value)) {
          num_non_entity_items_->Increment();
          skip = true;
        }
      }

      if (!store->IsString(value)) continue;
      Handle lang = store->GetString(value)->qualifier();
      bool foreign = !lang.IsNil() && !languages_.has(lang);

      if (property == n_name_) {
        if (!foreign) {
          int fanin = frame.GetInt(n_fanin_);
          AddAlias(&a, value, SRC_WIKIDATA_LABEL, fanin);
          has_primary_name = true;
        } else {
          AddAlias(&a, value, SRC_WIKIDATA_FOREIGN);
          if (fallback_name.IsNil()) fallback_name = value;
        }
      } else if (property == n_alias_) {
        if (store->IsFrame(s.value)) {
          Frame alias(store, s.value);
          if (!foreign) {
            // Pass-through alias definition.
            a.Add(n_alias_, alias);
          } else {
            // Add aliases in other languages as foreign alias.
            AddAlias(&a, value, SRC_WIKIDATA_FOREIGN, alias.GetInt(n_count_));
          }
        } else {
          // Add simple (foreign) Wikidata alias.
          AliasSource source = SRC_WIKIDATA_ALIAS;
          if (foreign) source = SRC_WIKIDATA_FOREIGN;
          AddAlias(&a, value, source);
        }
      } else if (property == n_native_name_ ||
                 property == n_native_label_) {
        // Output native names/labels as native aliases.
        AddAlias(&a, value, SRC_WIKIDATA_NATIVE);
      } else if (property == n_nickname_ ||
                 property == n_pseudonym_ ||
                 property == n_short_name_ ||
                 property == n_generic_name_ ||
                 property == n_birth_name_ ||
                 property == n_married_name_ ||
                 property == n_official_name_) {
        // Output alternative names without regards to language.
        AddAlias(&a, value, SRC_WIKIDATA_NAME);
      } else if (property == n_female_form_ ||
                 property == n_male_form_ ||
                 property == n_unit_symbol_) {
        // Output units and forms as alternative or foreign names.
        AliasSource source = SRC_WIKIDATA_NAME;
        if (foreign) source = SRC_WIKIDATA_FOREIGN;
        AddAlias(&a, value, source);
      } else if (property == n_iso3166_country_code_2_ ||
                 property == n_iso3166_country_code_3_) {
        // Output country codes as alternative names.
        AddAlias(&a, value, SRC_WIKIDATA_NAME);
      } else if (property == n_demonym_) {
        // Output (foreign) demonyms.
        AliasSource source = SRC_WIKIDATA_DEMONYM;
        if (foreign) source = SRC_WIKIDATA_FOREIGN;
        AddAlias(&a, value, source);
      }
    }

    // Add fallback alias if no primary name has been found.
    if (!has_primary_name && !fallback_name.IsNil()) {
      AddAlias(&a, fallback_name, SRC_WIKIDATA_LABEL);
    }

    // Add skip type to frame if it all aliases for the item should be skipped.
    // This will filter out all aliases for the item in the alias selector.
    if (skip) a.AddIsA(n_skip_);

    // Output aliases matching language.
    Frame aliases = a.Create();
    if (aliases.size() != 0) {
      Output(key, aliases);
    }
  }

  // Add alias.
  void AddAlias(Builder *aliases,
                Handle name,
                AliasSource source,
                int count = 0) {
    Builder alias(aliases->store());
    alias.Add(Handle::is(), name);
    if (count > 0) alias.Add(n_count_, count);
    alias.Add(n_sources_, 1 << source);
    aliases->Add(n_alias_, alias.Create());
  }

 private:
  // Wiki page types.
  WikimediaTypes wikitypes_;

  // Language(s) for aliases.
  HandleSet languages_;

  // Skip auxiliary items.
  bool skip_aux_ = false;
  AuxFilter filter_;
  task::Counter *num_aux_items_;
  task::Counter *num_non_entity_items_;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_alias_{names_, "alias"};
  Name n_count_{names_, "count"};
  Name n_sources_{names_, "sources"};
  Name n_skip_{names_, "skip"};
  Name n_fanin_{names_, "/w/item/fanin"};

  Name n_native_name_{names_, "P1559"};
  Name n_native_label_{names_, "P1705"};
  Name n_demonym_{names_, "P1549"};
  Name n_short_name_{names_, "P1813"};
  Name n_nickname_{names_, "P1449"};
  Name n_pseudonym_{names_, "P742"};
  Name n_generic_name_{names_, "P2561"};
  Name n_official_name_{names_, "P1448"};
  Name n_birth_name_{names_, "P1477"};
  Name n_married_name_{names_, "P2562"};
  Name n_female_form_{names_, "P2521"};
  Name n_male_form_{names_, "P3321"};
  Name n_iso3166_country_code_2_{names_, "P297"};
  Name n_iso3166_country_code_3_{names_, "P298"};
  Name n_unit_symbol_{names_, "P5061"};

  Name n_instance_of_{names_, "P31"};
};

REGISTER_TASK_PROCESSOR("alias-extractor", AliasExtractor);

// Select aliases for item.
class AliasSelector : public task::Reducer {
 public:
  // Group of aliases with same fingerprint.
  struct Alias {
    // Most common variant.
    string name;

    // Map of alias variants with frequency counts.
    std::unordered_map<string, int> variants;

    // Unicode representation for normalized version of most common variant.
    ustring chars;

    // Frequency counts for case forms.
    int forms[NUM_CASE_FORMS] = {};

    // Bitmask with alias sources.
    int sources = 0;

    // Total number of occurrences for alias.
    int count = 0;

    // Whether the alias has been selected for output.
    bool selected = false;

    // Compute size of common prefix between this alias and another alias.
    int CommonPrefix(const Alias *other) const {
      int n = 0;
      while (n < chars.size() &&
             n < other->chars.size() &&
             chars[n] == other->chars[n]) {
        n++;
      }
      return n;
    }

    // Check if the other alias contains the same sequence of digits.
    bool SameDigits(const Alias *other) {
      int m = 0;
      for (int n = 0; n < chars.size(); ++n) {
        if (Unicode::IsDigit(chars[n])) {
          // Try to find matching digit in other alias.
          bool match = false;
          while (m < other->chars.size()) {
            if (chars[n] == other->chars[m++]) {
              match = true;
              break;
            }
          }
          if (!match) return false;
        }
      }
      while (m < other->chars.size()) {
        if (Unicode::IsDigit(other->chars[m++])) return false;
      }
      return true;
    }

    // Compute edit distance between this alias and another alias.
    int EditDistance(const Alias *other) const {
      return LevenshteinDistance(chars.data(), chars.size(),
                                 other->chars.data(), other->chars.size());
    }
  };

  void Start(task::Task *task) override {
    Reducer::Start(task);
    output_ = task->GetSink("output");

    // Load commons store.
    task::LoadStore(&commons_, task, "corrections");
    names_.Bind(&commons_);

    // Get parameters.
    string lang = task->Get("language", "en");
    language_ = commons_.Lookup("/lang/" + lang);
    task->Fetch("anchor_threshold", &anchor_threshold_);
    task->Fetch("majority_form_fraction", &majority_form_fraction_);
    task->Fetch("min_prefix", &min_prefix_);
    task->Fetch("max_edit_distance", &max_edit_distance_);
    CHECK_GE(majority_form_fraction_, 0.5);

    // Read alias corrections.
    Frame aliases(&commons_, "/w/aliases");
    if (aliases.valid()) {
      // Get corrections for language.
      Frame corrections = aliases.GetFrame(language_);
      if (corrections.valid()) {
        // Make map of alias corrections for each item.
        for (const Slot &s : corrections) {
          item_corrections_[s.name] = s.value;
        }
      }
    }

    commons_.Freeze();
  }

  void Reduce(const task::ReduceInput &input) override {
    Text qid = input.key();
    Store store(&commons_);
    std::unordered_map<uint64, Alias *> aliases;

    // Get alias corrections for item.
    std::set<uint64> blacklist;
    auto f = item_corrections_.find(store.Lookup(qid));
    if (f != item_corrections_.end()) {
      Frame correction_list(&store, f->second);
      for (const Slot &s : correction_list) {
        // Get alias and modifier.
        string name = String(&store, s.name).value();
        Handle modifier = s.value;

        // Compute fingerprint and case form.
        uint64 fp;
        CaseForm form;
        tokenizer_.FingerprintAndForm(name, &fp, &form);
        if (form == CASE_INVALID) continue;

        if (modifier == n_blacklist_) {
          // Blacklist alias for item.
          blacklist.insert(fp);
        } else {
          // Add new alias for item.
          Alias *a = aliases[fp];
          if (a == nullptr) {
            a = new Alias();
            aliases[fp] = a;
          }
          int count = 1;
          if (modifier.IsIndex()) {
            a->sources |= (1 << modifier.AsIndex());
          } else if (modifier.IsInt()) {
            count = modifier.AsInt();
          }
          a->count += count;
          a->variants[name] += count;
          a->forms[form] += count;
        }
      }
    }

    // Collect all the aliases for the item.
    for (task::Message *message : input.messages()) {
      // Get next set of aliases for item.
      Frame batch = DecodeMessage(&store, message);

      // Get all aliases for item.
      for (const Slot &slot : batch) {
        // Check if all aliases for item should be skipped.
        if (slot.name == Handle::isa() && slot.value == n_skip_) {
          for (auto it : aliases) delete it.second;
          return;
        }

        if (slot.name != n_alias_) continue;
        Frame alias(&store, slot.value);
        string name = alias.GetString(Handle::is());
        int count = alias.GetInt(n_count_, 1);
        int sources = alias.GetInt(n_sources_);

        // Check that alias is valid UTF-8.
        if (!UTF8::Valid(name)) {
          VLOG(9) << "Skipping invalid alias for " << qid << ": " << name;
          continue;
        }

        // Compute fingerprint and case form.
        uint64 fp;
        CaseForm form;
        tokenizer_.FingerprintAndForm(name, &fp, &form);
        if (form == CASE_INVALID) continue;

        // Check if alias has been blacklisted.
        if (blacklist.count(fp) > 0) continue;

        // Update alias table.
        Alias *a = aliases[fp];
        if (a == nullptr) {
          a = new Alias();
          aliases[fp] = a;
        }
        a->sources |= sources;
        a->count += count;
        a->variants[name] += count;
        a->forms[form] += count;
      }
    }

    // Find most common variant for each alias.
    for (auto it : aliases) {
      Alias *alias = it.second;

      // Find most common variant.
      int max_count = -1;
      for (auto &variant : alias->variants) {
        if (variant.second > max_count) {
          max_count = variant.second;
          alias->name = variant.first;
        }
      }

      // Normalize name of the most common variant for alias and convert it to
      // Unicode code points for computing edit distance.
      string normalized;
      UTF8::Normalize(alias->name, tokenizer_.normalization(), &normalized);
      UTF8::DecodeString(normalized, &alias->chars);
    }

    // Select aliases based on sources.
    for (auto it : aliases) {
      Alias *alias = it.second;
      if (SelectAlias(alias)) alias->selected = true;
    }

    // Select aliases that are variations over already selected aliases.
    if (max_edit_distance_ > 0) {
      for (auto it : aliases) {
        Alias *alias = it.second;
        if (alias->selected) continue;
        if (alias->sources == WIKIDATA_FOREIGN) continue;

        // Check if alias is a variation over an already selected alias.
        bool variation = false;
        for (auto ot : aliases) {
          Alias *a = ot.second;
          if (!a->selected) continue;
          if (a->sources & VARIATION) continue;

          // A variation must have a common prefix with existing alias of a
          // minimum size and the edit distance must be below a threshold. The
          // variation must also contain the same sequence of digits.
          if (alias->CommonPrefix(a) < min_prefix_) continue;
          if (alias->EditDistance(a) > max_edit_distance_) continue;
          if (!alias->SameDigits(a)) continue;

          // Variation found.
          variation = true;
          break;
        }

        // Select variation.
        if (variation) {
          alias->selected = true;
          alias->sources |= VARIATION;
        }
      }
    }

    // Output selected aliases.
    Handle id = store.Lookup(qid);
    for (auto it : aliases) {
      uint64 fp = it.first;
      Alias *alias = it.second;
      if (!alias->selected) continue;
      if (alias->name.empty()) continue;

      // Find majority form.
      int form = CASE_NONE;
      for (int f = 0; f < NUM_CASE_FORMS; ++f) {
        if (alias->forms[f] >= alias->count * majority_form_fraction_) {
          form = f;
          break;
        }
      }
      if (form == CASE_INVALID) continue;

      // Output alias.
      Builder a(&store);
      a.Add(n_count_, alias->count);
      a.Add(n_sources_, alias->sources);
      if (form != CASE_NONE) a.Add(n_form_, form);
      Builder b(&store);
      b.Add(Handle::is(), alias->name);
      b.Add(id, a.Create());
      output_->Send(task::CreateMessage(SimpleItoa(fp), b.Create()));
    }

    // Delete alias table.
    for (auto it : aliases) delete it.second;
  }

  // Check if alias should be selected.
  bool SelectAlias(Alias *alias) {
    // Keep aliases from "trusted" sources.
    if (alias->sources & (WIKIDATA_LABEL |
                          WIKIPEDIA_TITLE |
                          WIKIPEDIA_REDIRECT |
                          WIKIPEDIA_NAME |
                          WIKIDATA_ALIAS |
                          WIKIDATA_NAME |
                          WIKIDATA_NATIVE)) {
      return true;
    }

    // Keep foreign, demonym, and nickname aliases supported by
    // Wikipedia aliases.
    if (alias->sources & (WIKIDATA_FOREIGN |
                          WIKIDATA_DEMONYM |
                          WIKIPEDIA_NICKNAME)) {
      if (alias->sources & (WIKIPEDIA_ANCHOR |
                            WIKIPEDIA_LINK |
                            WIKIPEDIA_DISAMBIGUATION)) {
        return true;
      }
    }

    // Disambiguation links need to be backed by anchors.
    if (alias->sources & WIKIPEDIA_DISAMBIGUATION) {
      if (alias->sources & (WIKIPEDIA_ANCHOR | WIKIPEDIA_LINK)) return true;
    }

    // Pure anchors need high counts to be selected.
    if (alias->sources & (WIKIPEDIA_ANCHOR | WIKIPEDIA_LINK)) {
      if (alias->count >= anchor_threshold_) return true;
    }

    return false;
  }

 private:
  // Alias source masks.
  enum AliasSourceMask {
    GENERIC = 1 << SRC_GENERIC,
    WIKIDATA_LABEL = 1 << SRC_WIKIDATA_LABEL,
    WIKIDATA_ALIAS = 1 << SRC_WIKIDATA_ALIAS,
    WIKIPEDIA_TITLE  = 1 << SRC_WIKIPEDIA_TITLE,
    WIKIPEDIA_REDIRECT = 1 << SRC_WIKIPEDIA_REDIRECT,
    WIKIPEDIA_ANCHOR = 1 << SRC_WIKIPEDIA_ANCHOR,
    WIKIPEDIA_DISAMBIGUATION = 1 << SRC_WIKIPEDIA_DISAMBIGUATION,
    WIKIDATA_FOREIGN = 1 << SRC_WIKIDATA_FOREIGN,
    WIKIDATA_NATIVE = 1 << SRC_WIKIDATA_NATIVE,
    WIKIDATA_DEMONYM = 1 << SRC_WIKIDATA_DEMONYM,
    WIKIPEDIA_LINK = 1 << SRC_WIKIPEDIA_LINK,
    WIKIDATA_NAME = 1 << SRC_WIKIDATA_NAME,
    WIKIPEDIA_NAME = 1 << SRC_WIKIPEDIA_NAME,
    WIKIPEDIA_NICKNAME = 1 << SRC_WIKIPEDIA_NICKNAME,
    VARIATION = 1 << SRC_VARIATION,
  };

  // Commons store.
  Store commons_;

  // Symbols.
  Names names_;
  Name n_alias_{names_, "alias"};
  Name n_count_{names_, "count"};
  Name n_sources_{names_, "sources"};
  Name n_form_{names_, "form"};
  Name n_blacklist_{names_, "blacklist"};
  Name n_skip_{names_, "skip"};

  // Language.
  Handle language_;

  // Phrase tokenizer for computing phrase fingerprints.
  nlp::PhraseTokenizer tokenizer_;

  // Threshold for pure anchors.
  int anchor_threshold_ = 100;

  // Fraction of aliases that must have a certain case form for this form to
  // be considered the majority form.
  float majority_form_fraction_ = 0.75;

  // Minimum common prefix size for alias variations.
  int min_prefix_ = 2;

  // Maximum edit distance for alias variations (0=disabled).
  int max_edit_distance_ = 0;

  // Mapping from item id to corrections for item.
  HandleMap<Handle> item_corrections_;

  // Output channel.
  task::Channel *output_ = nullptr;
};

REGISTER_TASK_PROCESSOR("alias-selector", AliasSelector);

// Merge item aliases for alias fingerprint.
class AliasMerger : public task::Reducer {
 public:
  void Start(task::Task *task) override {
    Reducer::Start(task);
    task->Fetch("transfer_aliases", &transfer_aliases_);
    task->Fetch("reliable_alias_sources", &reliable_alias_sources_);

    num_missing_items_ = task->GetCounter("missing_items");
    num_unique_aliases_ = task->GetCounter("unique_aliases");
    num_transfers_ = task->GetCounter("alias_transfers");
    num_zero_transfers_ = task->GetCounter("alias_zero_transfers");
    num_instance_transfers_ = task->GetCounter("alias_instance_transfers");

    // Load commons store.
    if (transfer_aliases_) {
      task::LoadStore(&commons_, task, "kb");
    }
    names_.Bind(&commons_);

    // Initialize alias transfer exceptions.
    if (transfer_aliases_) {
      static const char *exceptions[] = {
        "P1889",  // different from
        "P460",   // said to be the same as
        "P1533",  // identical to this given name
        "P138",   // named after
        "P2959",  // permanent duplicated item
        "P734",   // family name
        "P735",   // given name
        "P112",   // founded by
        "P115",   // home venue
        "P144",   // based on
        "P1950",  // second family name in Spanish name
        "P2359",  // Roman nomen gentilicium
        "P2358",  // Roman praenomen
        "P2365",  // Roman cognomen
        "P2366",  // Roman agnomen
        "P941",   // inspired by
        "P629",   // edition or translation of
        "P747",   // has edition or translation
        "P37",    // official language
        "P103",   // native language
        "P566",   // basionym
        "P487",   // Unicode character

        nullptr
      };
      for (const char **p = exceptions; *p != nullptr; ++p) {
        transfer_exceptions_.insert(commons_.LookupExisting(*p));
      }

      // Initialize fact catalog.
      catalog_.Init(&commons_);
    }

    commons_.Freeze();
  }

  void Reduce(const task::ReduceInput &input) override {
    // Pass-through unique aliases.
    if (input.messages().size() == 1) {
      Output(input.shard(), input.release(0));
      num_unique_aliases_->Increment();
      return;
    }

    // Merge all items for alias fingerprint.
    Store store(&commons_);
    Builder aliases(&store);
    std::unordered_map<string, int> names;
    for (task::Message *message : input.messages()) {
      Frame batch = DecodeMessage(&store, message);
      string name;
      int count = 0;
      for (const Slot &slot : batch) {
        if (slot.name == Handle::is()) {
          name = store.GetString(slot.value)->str().str();
        } else {
          aliases.Add(slot.name, slot.value);
          Frame alias(&store, slot.value);
          count += alias.GetInt(n_count_);
        }
      }
      if (!name.empty()) names[name] += count;
    }

    // Transfer aliases.
    if (transfer_aliases_) TransferAliases(aliases);

    // Add representative name.
    string name;
    int maxcount = -1;
    for (auto &it : names) {
      if (it.second > maxcount) {
        name = it.first;
        maxcount = it.second;
      }
    }
    if (!name.empty()) aliases.Add(Handle::is(), name);

    // Output merged alias cluster.
    Output(input.shard(), task::CreateMessage(input.key(), aliases.Create()));
  }

 private:
  // Alias for item.
  struct ItemAlias {
    Handle handle;  // item handle
    Handle alias;   // alias frame for item
    int count;      // alias frequency
    int sources;    // alias sources
    bool reliable;  // reliable alias flag
    int form;       // alias case form
  };

  // Transfer alias counts from source to target.
  bool Transfer(ItemAlias *source, ItemAlias *target) {
    // Check for conflicting case forms.
    if (source->form != CASE_NONE &&
        target->form != CASE_NONE &&
        source->form != target->form) {
      return false;
    }

    // Check for zero transfers.
    if (source->count == 0) {
      num_zero_transfers_->Increment();
      return false;
    }

    // Transfer alias counts from source to target.
    num_transfers_->Increment();
    num_instance_transfers_->Increment(source->count);
    target->count += source->count;
    target->sources |= 1 << SRC_TRANSFER;
    source->count = 0;
    return true;
  }

  // Exchange aliases between items.
  bool Exchange(ItemAlias *a, ItemAlias *b) {
    if (a->reliable && !b->reliable) {
      return Transfer(b, a);
    } else if (b->reliable && !a->reliable) {
      return Transfer(a, b);
    } else {
      return false;
    }
  }

  // Transfer unreliable aliases between related items.
  void TransferAliases(Builder &aliases) {
    // Build item index.
    Store *store = aliases.store();
    int num_items = aliases.size();
    std::vector<ItemAlias> items(num_items);
    HandleMap<int> item_index;
    for (int i = 0; i < num_items; ++i) {
      Slot &slot = aliases[i];
      Frame alias(store, slot.value);

      ItemAlias &item = items[i];
      item.handle = slot.name;
      item.alias = slot.value;
      item.count = alias.GetInt(n_count_);
      item.sources = alias.GetInt(n_sources_);
      item.reliable = (item.sources & reliable_alias_sources_) != 0;
      item.form = alias.GetInt(n_form_);

      // Disregard items that are not in the knowledge base.
      if (item.handle.IsGlobalRef()) {
        item_index[item.handle] = i;
      } else {
        item.handle = Handle::nil();
        num_missing_items_->Increment();
      }
    }

    // Find potential targets for alias transfer.
    bool pruned = false;
    std::set<int> numbers;
    std::set<int> years;
    for (int source = 0; source < num_items; ++source) {
      // Skip item if it is not in the knowledge base.
      if (items[source].handle.IsNil()) continue;

      // Get set of facts for item.
      Facts facts(&catalog_);
      facts.Extract(items[source].handle);
      for (int i = 0; i < facts.size(); ++i) {
        // Get base property and target value.
        Handle p = facts.first(i);
        Handle t = facts.last(i);
        if (!t.IsGlobalRef()) continue;
        CHECK(!t.IsNil());

        // Collect numbers and years.
        if (p == n_instance_of_) {
          if (t == n_natural_number_) {
            numbers.insert(source);
          }
          if (t == n_year_ || t == n_year_bc_ || t == n_decade_) {
            years.insert(source);
          }
        }

        // Check for property exceptions.
        if (transfer_exceptions_.count(p) > 0) continue;

        // Check if target has the phrase as an alias.
        auto f = item_index.find(t);
        if (f == item_index.end()) continue;
        int target = f->second;
        if (target == source) continue;

        // Transfer alias from unreliable to reliable alias.
        if (Exchange(&items[source], &items[target])) pruned = true;
      }
    }

    // Transfer aliases for years.
    if (!years.empty()) {
      for (int source  : years) {
        for (int target : years) {
          if (source == target) continue;
          if (Exchange(&items[source], &items[target])) pruned = true;
        }
      }
    }

    // Transfer aliases for numbers.
    if (!numbers.empty()) {
      for (int source : numbers) {
        for (int target : numbers) {
          if (source == target) continue;
          if (Exchange(&items[source], &items[target])) pruned = true;
        }
      }
    }

    // Prune aliases with zero count.
    if (pruned) {
      std::vector<int> removed;
      for (int i = 0; i < num_items; ++i) {
        ItemAlias &item = items[i];
        Frame f(store, item.alias);
        f.Set(n_count_, item.count);
        f.Set(n_sources_, item.sources);
        if (item.count == 0) removed.push_back(i);
      }
      aliases.Remove(removed);
    }
  }

  // Commons store.
  Store commons_;

  // Fact catalog for alias transfer.
  FactCatalog catalog_;

  // Transfer aliases.
  bool transfer_aliases_ = true;

  // Property exceptions for alias transfer.
  HandleSet transfer_exceptions_;

  // Reliable alias sources.
  int reliable_alias_sources_ =
    (1 << SRC_WIKIDATA_LABEL) |
    (1 << SRC_WIKIDATA_ALIAS) |
    (1 << SRC_WIKIDATA_NAME) |
    (1 << SRC_WIKIDATA_DEMONYM) |
    (1 << SRC_WIKIPEDIA_NAME);

  // Symbols.
  Names names_;
  Name n_count_{names_, "count"};
  Name n_sources_{names_, "sources"};
  Name n_form_{names_, "form"};

  Name n_instance_of_{names_, "P31"};
  Name n_natural_number_{names_, "Q21199"};
  Name n_year_{names_, "Q577"};
  Name n_year_bc_{names_, "Q29964144"};
  Name n_decade_{names_, "Q39911"};

  // Statistics.
  task::Counter *num_missing_items_ = nullptr;
  task::Counter *num_unique_aliases_ = nullptr;
  task::Counter *num_transfers_ = nullptr;
  task::Counter *num_zero_transfers_ = nullptr;
  task::Counter *num_instance_transfers_ = nullptr;
};

REGISTER_TASK_PROCESSOR("alias-merger", AliasMerger);

}  // namespace nlp
}  // namespace sling


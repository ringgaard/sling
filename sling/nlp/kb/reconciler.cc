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

#include <vector>
#include <unordered_set>
#include <utility>

#include "sling/base/types.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/nlp/kb/xref.h"
#include "sling/string/ctype.h"
#include "sling/task/frames.h"
#include "sling/task/reducer.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

// Read items and reconcile the identifiers. The effect of this frame processor
// is largely implicit. The identifier cluster frames are read into the commons
// store. When each item is read into a local store by the frame processor, the
// mapped ids are automatically converted to the reconciled ids because of the
// identifier cluster frames in the commons store. The item is output with a
// key that is mapped in a similar manner.
class ItemReconciler : public task::FrameProcessor {
 public:
  ~ItemReconciler() {
    for (auto &i : inversion_map_) delete i.second;
  }

  void Startup(task::Task *task) override {
    // Read reconciler configuration.
    FileReader reader(commons_, task->GetInputFile("config"));
    Frame config = reader.Read().AsFrame();
    CHECK(config.valid());

    // Get property inversions.
    if (config.Has("inversions")) {
      Frame inversions = config.Get("inversions").AsFrame();
      CHECK(inversions.valid());
      for (const Slot &slot : inversions) {
        Frame inverse(commons_, slot.value);
        Inversion *inversion = new Inversion();
        if (inverse.IsAnonymous()) {
          inversion->inverse = inverse.GetHandle(Handle::is());
          for (const Slot &s : inverse) {
            if (s.name != Handle::is()) {
              inversion->qualifiers.emplace_back(s.name, s.value);
            }
          }
        } else {
          inversion->inverse = slot.value;
        }
        inversion_map_[slot.name] = inversion;
      }
    }

    Frame xcfg(commons_, "/w/xrefs");
    if (xcfg.valid()) {
      // Set up URI mapping.
      Frame urimap(commons_, "/w/urimap");
      if (urimap.valid()) {
        urimap_.Load(urimap);
        urimap_.Bind(commons_, true);
      }

      // Get case insensitive properties.
      Array caseless = xcfg.Get("/w/caseless").AsArray();
      if (caseless.valid()) {
        for (int i = 0; i < caseless.length(); ++i) {
          String pid(commons_, caseless.get(i));
          caseless_.insert(pid.text());
        }
      }
    }

    // Statistics.
    num_mapped_ids_ = task->GetCounter("mapped_ids");
    num_mapped_uris_ = task->GetCounter("mapped_uris");
    num_inverse_properties_ = task->GetCounter("inverse_properties");
    num_inverse_qualifiers_ = task->GetCounter("inverse_qualifiers");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Skip empty frames.
    if (frame.size() == 0) return;

    // Lookup the key in the store to get the reconciled id for the frame.
    Store *store = frame.store();
    string id(key.data(), key.size());
    if (id.empty()) id = frame.Id().str();
    CHECK(!id.empty());
    if (Map(&id)) {
      num_mapped_ids_->Increment();
    }

    // Check for ids and uris.
    bool has_id = false;
    bool has_uris = false;
    for (const Slot &s : frame) {
      if (s.name == Handle::id()) has_id = true;
      if (s.name == n_exact_match_ ||
          s.name == n_equivalent_class_ ||
          s.name == n_equivalent_property_) {
        has_uris = true;
      }
    }

    // Remove all id slots.
    if (has_id) {
      Builder b(frame);
      b.Delete(Handle::id());
      b.Update();
    }

    // Map URIs.
    if (has_uris) {
      Builder b(frame);
      Handle pid;
      string id;
      for (Slot *s = b.begin(); s < b.end(); ++s) {
      if (s->name == n_exact_match_ ||
          s->name == n_equivalent_class_ ||
          s->name == n_equivalent_property_) {
          if (store->IsString(s->value)) {
            String value(store, s->value);
            if (urimap_.Lookup(value.text(), &pid, &id)) {
              s->name = pid;
              s->value = store->AllocateString(id);
              num_mapped_uris_->Increment();
            }
          }
        }
      }
      b.Update();
    }

    // Output inverted property frames.
    for (const Slot &slot : frame) {
      // Check for inverted property.
      auto f = inversion_map_.find(slot.name);
      if (f == inversion_map_.end()) continue;
      Inversion *inversion = f->second;

      // Do not invert non-items and self-relations.
      Handle target = store->Resolve(slot.value);
      if (!target.IsRef()) continue;
      Text target_id = store->FrameId(target);
      if (target_id.empty()) continue;

      // Convert parent to father or mother based on gender.
      Handle inverse = inversion->inverse;
      if (inverse == n_parent_ && target != slot.value) {
        Handle gender = frame.GetHandle(n_gender_);
        if (gender == n_male_) inverse = n_father_.handle();
        if (gender == n_female_) inverse = n_mother_.handle();
      }

      // Build inverted property frame.
      Builder inverted(store);
      if (target != slot.value && !inversion->qualifiers.empty()) {
        // Inverted qualified statement.
        Builder qualified(store);
        Frame qvalue(store, slot.value);
        for (auto &q : inversion->qualifiers) {
          Handle value = qvalue.GetHandle(q.first);
          if (!value.IsNil()) {
            if (qualified.empty()) qualified.AddIs(id);
            qualified.Add(q.second, value);
            num_inverse_qualifiers_->Increment();
          }
        }
        if (qualified.empty()) {
          inverted.AddLink(inverse, id);
        } else {
          inverted.Add(inverse, qualified.Create());
        }
      } else {
        // Output Inverted unqualified statement.
        inverted.AddLink(inverse, id);
      }
      Frame fi = inverted.Create();
      Output(target_id, serial, fi);
      num_inverse_properties_->Increment();
    }

    // Output frame with the reconciled id as key.
    Output(id, serial, frame);
  }

 private:
  // Map identifier.
  bool Map(string *id) const {
    // Lowercase id if property is case insensitive.
    int slash = id->find('/');
    int colon = id->find(':');
    if (slash != -1 && (colon == -1 || slash < colon)) {
      Text pid(id->data(), slash);
      if (caseless_.count(pid)) {
        for (int i = slash + 1; i < id->size(); ++i) {
          char &c = id->at(i);
          c = ascii_tolower(c);
        }
      }
    }

    // Try to look up identifier in cross-reference.
    Handle mapped = commons_->LookupExisting(*id);
    if (!mapped.IsNil()) {
      *id = commons_->FrameId(mapped).str();
      return true;
    }

    return false;
  }

  // Property inversion map.
  struct Inversion {
    // Inverse property.
    Handle inverse;

    // Qualifier inversion map.
    std::vector<std::pair<Handle, Handle>> qualifiers;
  };
  HandleMap<Inversion *> inversion_map_;

  // URI mapping.
  URIMapping urimap_;

  // Case insensitive properties.
  std::unordered_set<Text> caseless_;

  // Symbols.
  Name n_exact_match_{names_, "P2888"};
  Name n_equivalent_class_{names_, "P1709"};
  Name n_equivalent_property_{names_, "P1628"};
  Name n_parent_{names_, "P8810"};
  Name n_father_{names_, "P22"};
  Name n_mother_{names_, "P25"};
  Name n_gender_{names_, "P21"};
  Name n_male_{names_, "Q6581097"};
  Name n_female_{names_, "Q6581072"};

  // Statistics.
  task::Counter *num_mapped_ids_ = nullptr;
  task::Counter *num_mapped_uris_ = nullptr;
  task::Counter *num_inverse_properties_ = nullptr;
  task::Counter *num_inverse_qualifiers_ = nullptr;
};

REGISTER_TASK_PROCESSOR("item-reconciler", ItemReconciler);

// Set of item statements implemented as a hash table for fast checking of
// duplicates.
class Statements {
 public:
  Statements(Store *store) : store_(store), slots_(store) {
    limit_ = INITIAL_CAPACITY;
    mask_ = limit_ - 1;
    size_ = 0;
    slots_.resize(limit_);
  }

  // Ensure capacity for inserting up to 'n' statements.
  void Ensure(int n) {
    // Check if there is enough space with a fill factor of 50%.
    int needed = (size_ + n) * 2;
    if (needed <= limit_) return;

    // Expand hash table.
    Slots slots(store_);
    slots.swap(slots_);
    while (limit_ < needed) limit_ *= 2;
    mask_ = limit_ - 1;
    slots_.resize(limit_);
    for (int i = 0; i < slots.size(); ++i) {
      if (slots[i].name.IsNil()) continue;
      int pos = NameHash(slots[i].name) & mask_;
      for (;;) {
        Slot &s = slots_[pos];
        if (s.name.IsNil()) {
          s = slots[i];
          break;
        }
        pos = (pos + 1) & mask_;
      }
    }
  }

  // Insert statement. Return false if the statement is already in the table.
  bool Insert(Handle name, Handle value) {
    int pos = NameHash(name) & mask_;
    for (;;) {
      Slot &s = slots_[pos];
      if (s.name == name && store_->Equal(s.value, value)) {
        // Match found.
        return false;
      } else if (s.name.IsNil()) {
        // Insert new slot.
        s.name = name;
        s.value = value;
        size_++;
        return true;
      }
      pos = (pos + 1) & mask_;
    }
  }

 private:
  // Initial size for hash hable. Must be power of two.
  static const uint64 INITIAL_CAPACITY = 1024;

  // Compute hash for name.
  static Word NameHash(Handle name) {
    return name.raw() >> Handle::kTagBits;
  }

  // Store for table.
  Store *store_;

  // Hash table with linear probing.
  Slots slots_;
  uint64 size_;
  uint64 limit_;
  uint64 mask_;
};

// Merge items with the same ids.
class ItemMerger : public task::Reducer {
 public:
  void Start(task::Task *task) override {
    task::Reducer::Start(task);

    // Bind symbols.
    CHECK(names_.Bind(&commons_));
    temporal_modifiers_.add(n_point_in_time_.handle());
    temporal_modifiers_.add(n_start_time_.handle());
    temporal_modifiers_.add(n_end_time_.handle());
    commons_.Freeze();

    // Statistics.
    num_orig_statements_ = task->GetCounter("original_statements");
    num_final_statements_ = task->GetCounter("final_statements");
    num_dup_statements_ = task->GetCounter("duplicate_statements");
    num_deprecated_statements_ = task->GetCounter("deprecated_statements");
    num_merged_statements_ = task->GetCounter("merged_statements");
    num_reconciled_dates_ = task->GetCounter("reconciled_dates");
    num_reconciled_names_ = task->GetCounter("reconciled_names");
    num_merged_items_ = task->GetCounter("merged_items");
    num_unnames_ = task->GetCounter("unnames");
  }

  void Reduce(const task::ReduceInput &input) override {
    // Create frame with reconciled id.
    Store store(&commons_);
    Builder builder(&store);
    builder.AddId(input.key());
    builder.Update();
    Handle proxy = builder.handle();

    // Merge all item sources.
    Statements statements(&store);
    bool has_unnames = false;
    int birth_names = 0;
    int birth_dates = 0;
    int death_dates = 0;
    for (task::Message *message : input.messages()) {
      // Decode item.
      Frame item = DecodeMessage(&store, message);
      num_orig_statements_->Increment(item.size());

      // Since the merged frames are anonymous, self-references need to be
      // updated to the reconciled frame.
      Handle self = item.handle();
      item.TraverseSlots([self, proxy](Slot *s) {
        if (s->name == self) s->name = proxy;
        if (s->value == self) s->value = proxy;
      });

      // Add/merge statements.
      statements.Ensure(item.size());
      for (const Slot &slot : item) {
        // Skip redirects and comments.
        if (slot.name == Handle::is()) continue;
        if (slot.name.IsNil()) continue;

        // Add auxiliary ids.
        if (slot.name == n_auxid_) {
          String auxid(&store, slot.value);
          if (auxid.valid()) builder.AddId(auxid.text());
          continue;
        }

        // Check for unnames.
        if (slot.name == n_unname_) has_unnames = true;

        // Remove statements with deprecated rank.
        Handle qvalue = store.Resolve(slot.value);
        if (qvalue != slot.value) {
          Handle rank = store.Get(slot.value, n_rank_.handle());
          if (rank == Handle::zero()) {
            num_deprecated_statements_->Increment();
            continue;
          }
        }

        // Count properties that can be reconciled.
        if (slot.name == n_birth_name_) birth_names++;
        if (slot.name == n_date_of_birth_) birth_dates++;
        if (slot.name == n_date_of_death_) death_dates++;

        // Check for existing match(es).
        bool isnew = statements.Insert(slot.name, qvalue);
        if (isnew) {
          // Add new statement.
          builder.Add(slot.name, slot.value);
        } else if (qvalue == slot.value) {
          // Skip unqualified duplicate statement.
          num_dup_statements_->Increment();
        } else {
          // Try to find compatible statement for merging.
          Handle compatible = Handle::nil();
          bool replaced = false;
          for (Slot *s = builder.begin(); s < builder.end(); ++s) {
            if (s->name != slot.name) continue;
            Handle qv = store.Resolve(s->value);
            if (qv != qvalue) continue;

            if (qv == s->value) {
              // Replace existing unqualified statement.
              s->value = slot.value;
              replaced = true;
              break;
            }

            // Skip new statement if it is the same as an exising one.
            if (store.Equal(s->value, slot.value)) {
              replaced = true;
              break;
            }

            // Check if new qualified statement is compatible with existing
            // qualified statement.
            if (Compatible(&store, slot.value, s->value)) {
              compatible = s->value;
              break;
            }
          }

          if (replaced) {
            num_dup_statements_->Increment();
          } else if (compatible.IsNil()) {
            // No compatible existing statement; add new.
            builder.Add(slot.name, slot.value);
          } else {
            // Merge new qualified statement into existing statement.
            Frame existing_value(&store, compatible);
            Frame new_value(&store, slot.value);
            Merge(existing_value, new_value);
            num_merged_statements_->Increment();
          }
        }
      }
    }

    // Replace names if item has unname statements.
    if (has_unnames) Unname(builder);

    // Reconcile names and dates.
    bool prune = false;
    if (birth_names > 1) {
      if (ReconcileNames(builder, n_birth_name_)) {
        prune = true;
        num_reconciled_names_->Increment();
      }
    }
    if (birth_dates > 1) {
      if (ReconcileDates(builder, n_date_of_birth_)) {
        prune = true;
        num_reconciled_dates_->Increment();
      }
    }
    if (death_dates > 1) {
      if (ReconcileDates(builder, n_date_of_death_)) {
        prune = true;
        num_reconciled_dates_->Increment();
      }
    }
    if (prune) builder.Prune();

    // Output merged frame for item.
    Frame merged = builder.Create();
    Output(input.shard(), task::CreateMessage(input.key(), merged));
    num_merged_items_->Increment();
    num_final_statements_->Increment(merged.size());

    // Add properties to property catalog.
    if (merged.IsA(n_property_)) {
      MutexLock lock(&mu_);
      string pid = merged.Id().str();
      properties_.push_back(pid);
    }
  }

  // Output property catalog.
  void Flush(task::Task *task) override {
    Store store(&commons_);
    Builder catalog(&store);
    catalog.AddId("/w/entity");
    catalog.AddIs(n_schema_);
    catalog.Add(n_name_, "Wikidata entity");
    catalog.AddLink(n_family_, "/schema/wikidata");
    for (const string &id : properties_) {
      catalog.AddLink(n_role_, id);
    }
    Output(0, task::CreateMessage(catalog.Create()));
  }

  // Check if two qualified values are compatible.
  bool Compatible(Store *store, Handle first, Handle second) {
    // Check for temporal compatibility.
    Frame f(store, first);
    Frame s(store, second);
    for (Handle property : temporal_modifiers_) {
      if (Same(f, s, property)) return true;
    }

    return false;
  }

  // Check for compatible property.
  static bool Same(Frame &f1, Frame &f2, Handle property) {
    Handle v1 = f1.GetHandle(property);
    Handle v2 = f2.GetHandle(property);
    if (v1.IsNil() || v2.IsNil()) return false;
    if (f1.store()->Equal(v1, v2)) return true;
    if (v1.IsInt() && v2.IsInt()) {
      Date d1(v1.AsInt());
      Date d2(v2.AsInt());
      if (d1.Contains(d2) || d2.Contains(d1)) return true;
    }
    return false;
  }

  // Reconcile dates. Returns true if some dates have been reconciled.
  bool ReconcileDates(Builder &builder, Name &property) {
    // Collect property values.
    Store *store = builder.store();
    std::vector<Slot *> slots;
    for (Slot *s = builder.begin(); s < builder.end(); s++) {
      if (s->name == property) slots.push_back(s);
    }

    // Find best date.
    Slot *winner = nullptr;
    Date best;
    for (Slot *s : slots) {
      Handle value = store->Resolve(s->value);
      if (!value.IsInt()) continue;
      Date date(value.AsInt());
      if (value != s->value &&
          store->Get(s->value, n_rank_.handle()) == Handle::two()) {
        // Make preferred date the winner.
        winner = s;
        best = date;
      } else if (winner == nullptr || (date != best && best.Contains(date))) {
        winner = s;
        best = date;
      }
    }
    if (winner == nullptr) return false;

    // Only keep best date.
    int pruned = 0;
    for (Slot *s : slots) {
      // Keep winner.
      if (s == winner) continue;

      // Prune dates that contain the winner, i.e. are less precise.
      Handle value = store->Resolve(s->value);
      if (!value.IsInt()) continue;
      Date date(value.AsInt());
      if (date.Contains(best)) {
        s->name = Handle::nil();
        pruned++;
      }
    }

    // Remove redundant qualifiers from winner.
    if (pruned == slots.size() - 1 && store->IsFrame(winner->value)) {
      Frame qualifiers(store, winner->value);
      bool compact = true;
      for (const Slot &s : qualifiers) {
        if (s.name == Handle::is()) continue;
        if (s.name == n_rank_) continue;
        if (s.name == n_preferred_rank_reason_) continue;
        compact = false;
        break;
      }
      if (compact) {
        winner->value = qualifiers.GetHandle(Handle::is());
      }
    }

    return pruned > 0;
  }

  // Reconcile names. Returns true if some names have been reconciled.
  bool ReconcileNames(Builder &builder, Name &property) {
    // Collect qualified names.
    Store *store = builder.store();
    int pruned = 0;
    std::unordered_set<Text> names;
    for (Slot *s = builder.begin(); s < builder.end(); s++) {
      if (s->name == property && store->IsString(s->value)) {
        StringDatum *name = store->GetString(s->value);
        if (name->qualified()) names.insert(name->str());
      }
    }

    // Delete unqualified names with qualified counterparts.
    for (Slot *s = builder.begin(); s < builder.end(); s++) {
      if (s->name == property && store->IsString(s->value)) {
        StringDatum *name = store->GetString(s->value);
        if (!name->qualified() && names.count(name->str()) > 0) {
          s->name = Handle::nil();
          pruned++;
        }
      }
    }

    return pruned > 0;
  }

  // Merge second frame into the first frame.
  void Merge(Frame &f1, Frame &f2) {
    CHECK(f1.valid());
    CHECK(f2.valid());
    Store *store = f1.store();
    Builder merged(f1);
    for (const Slot &s2 : f2) {
      bool found = false;
      for (int i = 0; i < merged.size(); ++i) {
        Slot &s1 = merged[i];
        if (s1.name == s2.name) {
          if (store->Equal(s1.value, s2.value)) {
            found = true;
            break;
          }
          if (s1.value.IsInt() &&
              s2.value.IsInt() &&
              temporal_modifiers_.contains(s1.name)) {
            // Keep most precise modifier.
            Date d1(s1.value.AsInt());
            Date d2(s2.value.AsInt());
            if (d1.Contains(d2)) {
              s1.value = s2.value;
              found = true;
              break;
            } else if (d2.Contains(d1)) {
              found = true;
              break;
            }
          }
        }
      }
      if (!found) merged.Add(s2.name, s2.value);
    }
    merged.Update();
  }

  // Unname item.
  void Unname(Builder &builder) {
    Store *store = builder.store();
    for (int i = 0; i < builder.size(); ++i) {
      if (builder[i].name == n_unname_) {
        Handle old_name = store->Resolve(builder[i].value);
        Handle new_name = Handle::nil();
        if (old_name != builder[i].value) {
          new_name = Frame(store, builder[i].value).GetHandle(n_name_);
        }

        for (int j = 0; j < builder.size(); ++j) {
          if (builder[j].name == n_name_ &&
              store->Equal(old_name, builder[j].value)) {
            if (new_name.IsNil()) {
              builder[j].name = n_alias_.handle();
            } else {
              builder[j].value = new_name;
            }
            num_unnames_->Increment();
          }
        }
      }
    }
  }

 private:
  // Property ids.
  std::vector<string> properties_;
  Mutex mu_;

  // Symbols.
  Store commons_;
  Names names_;
  Name n_auxid_{names_, "PAUXID"};
  Name n_unname_{names_, "PUNME"};
  Name n_schema_{names_, "schema"};
  Name n_name_{names_, "name"};
  Name n_alias_{names_, "alias"};
  Name n_family_{names_, "family"};
  Name n_role_{names_, "role"};
  Name n_rank_{names_, "rank"};
  Name n_property_{names_, "/w/property"};

  Name n_point_in_time_{names_, "P585"};
  Name n_start_time_{names_, "P580"};
  Name n_end_time_{names_, "P582"};
  Name n_date_of_birth_{names_, "P569"};
  Name n_date_of_death_{names_, "P570"};
  Name n_preferred_rank_reason_{names_, "P7452"};
  Name n_birth_name_{names_, "P1477"};
  Handles temporal_modifiers_{&commons_};

  // Statistics.
  task::Counter *num_orig_statements_ = nullptr;
  task::Counter *num_final_statements_ = nullptr;
  task::Counter *num_dup_statements_ = nullptr;
  task::Counter *num_merged_statements_ = nullptr;
  task::Counter *num_deprecated_statements_ = nullptr;
  task::Counter *num_reconciled_dates_ = nullptr;
  task::Counter *num_reconciled_names_ = nullptr;
  task::Counter *num_merged_items_ = nullptr;
  task::Counter *num_unnames_ = nullptr;
};

REGISTER_TASK_PROCESSOR("item-merger", ItemMerger);

}  // namespace nlp
}  // namespace sling

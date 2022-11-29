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

#include "sling/frame/snapshot.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/kb/xref.h"
#include "sling/task/frames.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

// Extract identifiers from frame ids, redirects, and properties and build an
// identifier cross reference.
class XRefBuilder : public task::FrameProcessor {
 public:
  void Startup(task::Task *task) override {
    // Read xref configuration.
    FileReader reader(commons_, task->GetInputFile("config"));
    Frame config = reader.Read().AsFrame();
    CHECK(config.valid());

    // Get property priority list.
    sys_property_ = xref_.CreateProperty(Handle::nil(), "");
    Array properties = config.Get("properties").AsArray();
    CHECK(properties.valid());
    for (int i = 0; i < properties.length(); ++i) {
      Frame property(commons_, properties.get(i));
      xref_.AddProperty(property);
    }

    // Get URI mapping.
    Frame urimap = config.GetFrame("urimap");
    if (urimap.valid()) {
      urimap_.Load(urimap);
      urimap_.Bind(commons_, false);
    }
    uri_property_ = xref_.CreateProperty(Handle::nil(), "");

    // Add properties for cases and topics.
    xref_.CreateProperty(Handle::nil(), "c");
    xref_.CreateProperty(Handle::nil(), "t");
    Handle pcase = commons_->LookupExisting("PCASE");
    if (!pcase.IsNil()) xref_.CreateProperty(pcase, "PCASE");

    // Add fixed URIs.
    Frame uris = config.GetFrame("uris");
    if (uris.valid()) {
      for (const Slot &s : uris) {
        if (s.name.IsId()) continue;
        Text uri = commons_->GetText(s.name);
        Text prop = commons_->GetText(s.value);
        auto *pid = xref_.GetIdentifier(sys_property_, prop);
        auto *uid = xref_.GetIdentifier(uri_property_, uri);
        xref_.Merge(pid, uid);
      }
    }

    // Get pre-resolved reference mappings.
    Frame mappings = config.GetFrame("mappings");
    CHECK(mappings.valid());
    for (const Slot &s : mappings) {
      auto *ref =  xref_.GetIdentifier(commons_->FrameId(s.name), false);
      CHECK(ref != nullptr);
      ref->fixed = true;
      auto *item =  xref_.GetIdentifier(commons_->FrameId(s.value), false);
      CHECK(item != nullptr);

      bool merged = xref_.Merge(ref, item);
      if (!merged) {
        LOG(WARNING) << "Mapping conflict between " << ref->ToString()
                     << " and " << item->ToString();
      }
    }

    // Get xref property mnemonics.
    mnemonics_ = config.GetFrame("mnemonics");

    // Statistics.
    num_tracked_ = task->GetCounter("tracked");
    num_ids_ = task->GetCounter("ids");
    num_redirects_ = task->GetCounter("redirects");
    num_skipped_ = task->GetCounter("skipped");
    num_conflicts_ = task->GetCounter("conflicts");
    num_property_ids_ = task->GetCounter("property_ids");
    num_uris_ = task->GetCounter("uris");
    num_mapped_uris_ = task->GetCounter("mapped_uris");
    num_indexed_uris_ = task->GetCounter("indexed_uris");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Check if frame has any ids, redirects, or tracked properties. All
    // tracked properties are in the global store, so all local properties can
    // be skipped.
    int num_ids = 0;
    int num_props = 0;
    bool redirect = false;
    for (const Slot &s : frame) {
      if (s.name == Handle::id()) {
        num_ids++;
      } else if (s.name == Handle::is()) {
        redirect = true;
      } else if (s.name.IsGlobalRef() && s.name != Handle::isa()) {
        num_props++;
      }
    }

    // Skip frame unless it has multiple ids, is redirected, or it has one or
    // more tracked properties.
    if (num_ids < 2 && !redirect && num_props == 0) return;
    num_tracked_->Increment();

    // Add all ids and tracked properties to cross reference.
    MutexLock lock(&mu_);
    XRef::Identifier *anchor = nullptr;
    Store *store = frame.store();
    bool merging = false;
    string idstr;
    for (const Slot &s : frame) {
      if (s.name == Handle::id()) {
        // Add id to cross reference.
        Text ref = store->SymbolName(s.value);
        anchor = Merge(anchor, xref_.GetIdentifier(ref, redirect));
        num_ids_->Increment();
      } else if (s.name == Handle::is()) {
        // Redirect ids.
        Text ref = store->FrameId(store->Resolve(s.value));
        XRef::Identifier *id = xref_.GetIdentifier(ref, merging);
        XRef::Identifier *merged = Merge(anchor, id);
        if (merged == nullptr) {
          conflicts_.emplace_back(anchor, id);
        } else {
          anchor = merged;
        }
        num_redirects_->Increment();
      } else if (s.name == Handle::isa()) {
        if (s.value == n_merge_) merging = true;
      } else if (s.name.IsGlobalRef()) {
        // Add identifiers for tracked properties.
        if (s.name == n_exact_match_) {
          Handle value = store->Resolve(s.value);
          if (store->IsString(value)) {
            Text uri = store->GetString(value)->str();

            // Map URI to xref property.
            Handle pid;
            XRef::Identifier *id = nullptr;
            if (urimap_.Lookup(uri, &pid, &idstr)) {
              if (!pid.IsNil()) {
                const XRef::Property *property = xref_.LookupProperty(pid);
                if (property) {
                  id = xref_.GetIdentifier(property, idstr);
                  num_indexed_uris_->Increment();
                }
              }
              num_mapped_uris_->Increment();
            } else {
              id = xref_.GetIdentifier(uri_property_, uri);
              num_uris_->Increment();
            }

            if (id) {
              XRef::Identifier *merged = Merge(anchor, id);
              if (merged == nullptr) {
                conflicts_.emplace_back(anchor, id);
              } else {
                anchor = merged;
              }
            }
          }
        } else if (s.name == n_equivalent_class_ ||
                   s.name == n_equivalent_property_) {
          // Add URI as alias for item id.
          Handle value = store->Resolve(s.value);
          if (store->IsString(value)) {
            Text uri = store->GetString(value)->str();
            XRef::Identifier *id = xref_.GetIdentifier(uri_property_, uri);
            XRef::Identifier *merged = Merge(anchor, id);
            if (merged == nullptr) {
              conflicts_.emplace_back(anchor, id);
            } else {
              anchor = merged;
            }
            num_uris_->Increment();
          }
        } else {
          // Look up property to see if it is tracked.
          const XRef::Property *property = xref_.LookupProperty(s.name);
          if (property != nullptr) {
            // Get identifier value.
            Handle value = store->Resolve(s.value);
            if (store->IsString(value)) {
              Text ref = store->GetString(value)->str();
              XRef::Identifier *id = xref_.GetIdentifier(property, ref);
              XRef::Identifier *merged = Merge(anchor, id);
              if (merged == nullptr) {
                conflicts_.emplace_back(anchor, id);
              } else {
                anchor = merged;
              }
              num_property_ids_->Increment();
            }
          }
        }
      }
    }
  }

  void Flush(task::Task *task) override {
    // Get output file name.
    task::Binding *file = task->GetOutput("output");
    CHECK(file != nullptr);

    // Build xref frames.
    bool snapshot = task->Get("snapshot", false);
    Store store;
    xref_.Build(&store);
    if (mnemonics_.valid()) {
      Builder b(&store);
      b.AddId("/w/mnemonics");
      Store *commons = mnemonics_.store();
      for (const Slot &s : mnemonics_) {
        CHECK(commons->IsString(s.name));
        CHECK(commons->IsString(s.value));
        Text mnemonic = commons->GetString(s.name)->str();
        Text property = commons->GetString(s.value)->str();
        b.Add(String(&store, mnemonic), property);
      }
      b.Create();
    }
    if (!urimap_.empty()) {
      Builder b(&store);
      b.AddId("/w/urimap");
      urimap_.Save(&b);
      b.Create();
    }
    if (snapshot) store.AllocateSymbolHeap();
    store.GC();

    // Save xref store to file.
    FileEncoder encoder(&store, file->resource()->name());
    encoder.encoder()->set_shallow(true);
    encoder.EncodeAll();
    CHECK(encoder.Close());

    // Write snapshot if requested.
    if (snapshot) {
      CHECK(Snapshot::Write(&store, file->resource()->name()));
    }

    // Write conflict report.
    if (task->GetOutput("conflicts")) {
      WriteReport(task->GetOutputFile("conflicts"));
    }
  }

  // Output SLING store with merge conflicts.
  void WriteReport(const string &reportfn) {
    // Output frame for each cluster with conflicting clusters.
    LOG(INFO) << "Write conflicts report";
    Store store;
    string first_cluster_name;
    string second_cluster_name;
    string first_id_name;
    string second_id_name;
    for (auto &conflict : conflicts_) {
      conflict.first->GetName(&first_id_name);
      conflict.second->GetName(&second_id_name);

      XRef::Identifier *first_cluster = conflict.first->Canonical();
      XRef::Identifier *second_cluster = conflict.second->Canonical();

      first_cluster->GetName(&first_cluster_name);
      second_cluster->GetName(&second_cluster_name);

      Handle first = store.Lookup(first_cluster_name);
      Handle second = store.Lookup(second_cluster_name);

      store.Add(first, second, store.AllocateString(second_id_name));
      store.Add(second, first, store.AllocateString(first_id_name));
    }

    // Write conflict store to file.
    FileEncoder encoder(&store, reportfn);
    encoder.encoder()->set_shallow(true);
    encoder.EncodeAll();
    CHECK(encoder.Close());
    LOG(INFO) << "Conflicts report done";
  }

 private:
  // Merge identifier with anchor. Returns new anchor or null if merging would
  // lead to a conflict.
  XRef::Identifier *Merge(XRef::Identifier *anchor, XRef::Identifier *id) {
    if (id == nullptr || id == anchor) {
      return anchor;
    } else if (anchor == nullptr) {
      return id;
    } else {
      bool merged = xref_.Merge(anchor, id);
      if (!merged) {
        if (anchor->fixed || id->fixed) {
          VLOG(1) << "Skipped merging of " << anchor->ToString()
                  << " and " << id->ToString();
          num_skipped_->Increment();
        } else {
          VLOG(1) << "Merge conflict between " << anchor->ToString()
                  << " and " << id->ToString();
          num_conflicts_->Increment();
          return nullptr;
        }
      }
      return anchor;
    }
  }

  // Identifier cross-reference.
  XRef xref_;

  // URI mapping.
  URIMapping urimap_;
  XRef::Property *sys_property_ = nullptr;
  XRef::Property *uri_property_ = nullptr;

  // List of conflicting identifier pairs.
  std::vector<std::pair<XRef::Identifier *, XRef::Identifier *>> conflicts_;

  // Property mnemonics.
  Frame mnemonics_;

  // Symbols.
  Name n_merge_{names_, "merge"};
  Name n_exact_match_{names_, "P2888"};
  Name n_equivalent_class_{names_, "P1709"};
  Name n_equivalent_property_{names_, "P1628"};

  // Statistics.
  task::Counter *num_tracked_ = nullptr;
  task::Counter *num_ids_ = nullptr;
  task::Counter *num_redirects_ = nullptr;
  task::Counter *num_skipped_ = nullptr;
  task::Counter *num_conflicts_ = nullptr;
  task::Counter *num_property_ids_ = nullptr;
  task::Counter *num_uris_ = nullptr;
  task::Counter *num_mapped_uris_ = nullptr;
  task::Counter *num_indexed_uris_ = nullptr;

  // Mutex for serializing access to cross-reference table.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("xref-builder", XRefBuilder);

}  // namespace nlp
}  // namespace sling


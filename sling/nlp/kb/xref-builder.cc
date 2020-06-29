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
    // Read xref property priority list.
    FileInputStream stream(task->GetInputFile("config"));
    InputParser parser(commons_, &stream);
    Array properties = parser.Read().AsArray();
    CHECK(properties.valid());
    for (int i = 0; i < properties.length(); ++i) {
      Frame property(commons_, properties.get(i));
      xref_.AddProperty(property);
    }

    // Statistics.
    num_ids_ = task->GetCounter("ids");
    num_redirects_ = task->GetCounter("redirects");
    num_conflicts_ = task->GetCounter("conflicts");
    num_property_ids_ = task->GetCounter("property_ids");
  }

  void Process(Slice key, const Frame &frame) override {
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

    // Add all ids and tracked properties to cross reference.
    MutexLock lock(&mu_);
    XRef::Identifier *anchor = nullptr;
    Store *store = frame.store();
    for (const Slot &s : frame) {
      if (s.name == Handle::id()) {
        // Add id to cross reference.
        Text ref = store->SymbolName(s.value);
        anchor = Merge(anchor, xref_.GetIdentifier(ref, redirect));
        num_ids_->Increment();
      } else if (s.name == Handle::is()) {
        // Redirect ids.
        Text ref = store->FrameId(store->Resolve(s.value));
        anchor = Merge(anchor, xref_.GetIdentifier(ref));
        num_redirects_->Increment();
      } else if (s.name.IsGlobalRef() && s.name != Handle::isa()) {
        // Add identifiers for tracked properties. Look up property to see if
        // it is tracked.
        const XRef::Property *property = xref_.LookupProperty(s.name);
        if (property != nullptr) {
          // Get identifier value.
          Handle value = store->Resolve(s.value);
          if (store->IsString(value)) {
            Text ref = store->GetString(value)->str();
            anchor = Merge(anchor, xref_.GetIdentifier(property, ref));
            num_property_ids_->Increment();
          }
        }
      }
    }
  }

  void Flush(task::Task *task) override {
    // Build xref frames.
    Store store(commons_);
    xref_.Build(&store);
    store.GC();

    // Save xref store to file.
    FileEncoder encoder(&store, task->GetOutputFile("output"));
    encoder.encoder()->set_shallow(true);
    encoder.EncodeAll();
    CHECK(encoder.Close());
  }

 private:
  // Merge identifier with anchor. Returns new anchor.
  XRef::Identifier *Merge(XRef::Identifier *anchor, XRef::Identifier *id) {
    if (id == nullptr || id == anchor) {
      return anchor;
    } else if (anchor == nullptr) {
      return id;
    } else {
      bool merged = xref_.Merge(anchor, id);
      if (!merged) {
        LOG(WARNING) << "Merge conflict between " << anchor->ToString()
                     << " and " << id->ToString();
        num_conflicts_->Increment();
      }
      return anchor;
    }
  }

  // Identifier cross-reference.
  XRef xref_;

  // Statistics.
  task::Counter *num_ids_ = nullptr;
  task::Counter *num_redirects_ = nullptr;
  task::Counter *num_conflicts_ = nullptr;
  task::Counter *num_property_ids_ = nullptr;

  // Mutex for serializing access to cross-reference table.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("xref-builder", XRefBuilder);

}  // namespace nlp
}  // namespace sling


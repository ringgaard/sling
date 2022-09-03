// Copyright 2022 Ringgaard Research ApS
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

#include <algorithm>
#include <string>
#include <vector>
#include <utility>

#include "sling/base/types.h"
#include "sling/file/recordio.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/frame/store.h"
#include "sling/task/task.h"
#include "sling/task/process.h"
#include "sling/util/mutex.h"

namespace sling {
namespace nlp {

// Collect usage statistics for properties.
class PropertyUsage : public task::Process {
 public:
  typedef std::pair<Handle, int> TypeCount;
  struct PropertyStats {
    std::vector<TypeCount> types;
    int remainder = 0;
  };

  void Run(task::Task *task) override {
    // Get parameters.
    int maxtypes = task->Get("maxtypes", 100);
    int mincount = task->Get("mincount", 30);

    // Load knoweldge base.
    Store store;
    LoadStore(task->GetInputFile("kb"), &store);

    // Resolve symbols.
    Handle n_item = store.Lookup("/w/item");
    Handle n_property = store.Lookup("/w/property");
    Handle n_instance_of = store.Lookup("P31");
    Handle n_usage = store.Lookup("usage");

    // Property usage table (prop,type)->count.
    HandlePairMap<int> usage;
    auto add_type = [&](Handle prop, Handle value) {
      auto *item = store.GetFrame(value);
      for (Slot *s = item->begin(); s < item->end(); ++s) {
        if (s->name == n_instance_of) {
          Handle type = store.Resolve(s->value);
          usage[std::make_pair(prop, type)]++;
        }
      }
    };

    // Collect property statistics from items.
    task::Counter *num_items = task->GetCounter("items");
    store.ForAll([&](Handle handle) {
      Frame item(&store, handle);
      if (!item.IsA(n_item)) return;

      for (const Slot &s : item) {
        Handle prop = s.name;
        Handle value = s.value;
        if (!store.IsPublic(prop)) continue;
        if (!store.IsFrame(value)) continue;
        if (store.IsPublic(value)) {
          add_type(prop, value);
        } else {
          Frame qualifiers(&store, value);
          for (const Slot &qs : qualifiers) {
            Handle qprop = qs.name;
            Handle qvalue = qs.value;
            if (!store.IsPublic(qprop)) continue;
            if (!store.IsFrame(qvalue)) continue;
            if (store.IsAnonymous(qvalue)) continue;
            if (qprop == Handle::is()) qprop = prop;
            add_type(qprop, qvalue);
          }
        }
      }
      num_items->Increment();
    });

    // Group usage per property.
    HandleMap<PropertyStats> propstat;
    for (const auto &it : usage) {
      Handle prop = it.first.first;
      Handle type = it.first.second;
      int count = it.second;
      PropertyStats &ps = propstat[prop];
      if (count < mincount) {
        ps.remainder += count;
      } else {
        ps.types.push_back(std::make_pair(type, count));
      }
    }

    // Sort and prune property statistics.
    for (auto &it : propstat) {
      PropertyStats &ps = it.second;

      std::sort(ps.types.begin(), ps.types.end(),
        [](const TypeCount &a, const TypeCount &b) {
          return a.second > b.second;
        });

      if (ps.types.size() > maxtypes) {
        for (int i = maxtypes; i < ps.types.size(); ++i) {
          ps.remainder += ps.types[i].second;
        }
        ps.types.resize(maxtypes);
      }
    }

    // Write property usage to output.
    RecordWriter output(task->GetOutputFile("output"));
    for (auto &it : propstat) {
      Handle prop = it.first;
      if (!store.GetFrame(prop)->isa(n_property)) continue;

      PropertyStats &ps = it.second;
      const auto &types = ps.types;
      if (types.empty()) continue;

      Builder tb(&store);
      for (int i = 0; i < types.size(); ++i) {
        tb.Add(types[i].first, types[i].second);
      }
      if (ps.remainder > 0) {
        tb.Add(Handle::nil(), ps.remainder);
      }
      Builder pb(&store);
      pb.Add(n_usage, tb.Create());
      string data = Encode(pb.Create());

      CHECK(output.Write(store.FrameId(prop).slice(), data));
    }
    CHECK(output.Close());
  }
};

REGISTER_TASK_PROCESSOR("property-usage", PropertyUsage);

}  // namespace nlp
}  // namespace sling


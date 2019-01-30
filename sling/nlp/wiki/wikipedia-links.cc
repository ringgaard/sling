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

#include "sling/base/logging.h"
#include "sling/base/types.h"
#include "sling/nlp/document/document.h"
#include "sling/task/documents.h"

namespace sling {
namespace nlp {

class WikipediaLinkExtractor : public task::DocumentProcessor {
 public:
  void Startup(task::Task *task) override {
  }

  void Process(Slice key, const nlp::Document &document) override {
    // Collect outbound links from document.
    HandleMap<int> links;

    // Collect all links in mentions.
    Store *store = document.store();
    Handles evoked(store);
    for (const Span *span : document.spans()) {
      span->AllEvoked(&evoked);
      for (Handle link : evoked) {
        link = store->Resolve(link);
        if (!store->IsFrame(link)) continue;
        if (store->GetFrame(link)->IsAnonymous()) continue;
        links[link]++;
      }
    }

    // Collect all thematic links.
    for (Handle link : document.themes()) {
      link = store->Resolve(link);
      if (!store->IsFrame(link)) continue;
      if (store->GetFrame(link)->IsAnonymous()) continue;
      links[link]++;
    }
  }

 private:
  //Name n_page_item_{&names_, "/wp/page/item"};
};

REGISTER_TASK_PROCESSOR("wikipedia-link-extractor", WikipediaLinkExtractor);

}  // namespace nlp
}  // namespace sling


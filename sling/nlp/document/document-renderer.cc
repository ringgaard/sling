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

#include <algorithm>
#include <iostream>
#include <string>

#include "sling/base/flags.h"
#include "sling/base/init.h"
#include "sling/base/logging.h"
#include "sling/file/recordio.h"
#include "sling/frame/store.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/document/document.h"

DEFINE_string(commons, "", "Commons store");
DEFINE_string(key, "", "Document key");

using namespace sling;
using namespace sling::nlp;

class DocumentRenderer {
 public:
  DocumentRenderer(const Document &document)
      : document_(document),
        frames_(document.store()),
        mentions_(document.store()),
        themes_(document.store()) {}

  void Render() {
    // Build list of spans in nesting order.
    std::vector<Span *> spans;
    for (int i = 0; i < document_.num_spans(); ++i) {
      Span *span = document_.span(i);
      if (!span->deleted()) spans.push_back(span);
    }
    std::sort(spans.begin(), spans.end(),
              [](const Span *a, const Span *b) -> bool {
                if (a->begin() != b->begin()) {
                  return a->begin() < b->begin();
                } else {
                  return a->end() > b->end();
                }
              });

    for (const Token &token : document_.tokens()) {
      OutputBreak(token);
      OutputToken(token);
    }
  }

  void OutputBreak(const Token &token) {
    BreakType brk = token.brk();
    if (brk >= CHAPTER_BREAK) {
      html_.append("\n<hr>\n");
    } else if (brk >= SECTION_BREAK) {
      html_.append("\n<center>***</center>\n");
    } else if (brk >= PARAGRAPH_BREAK) {
      html_.append("\n<p>");
    } else if (brk >= SENTENCE_BREAK) {
      html_.append("&ensp;");
    } else if (brk >= SPACE_BREAK) {
      html_.append(" ");
    }
  }

  void OutputToken(const Token &token) {
    // Convert special punctuation tokens.
    const string &word = token.word();
    if (word == "``") {
      html_.append("“");
    } else if (word == "''") {
      html_.append("”");
    } else if (word == "--") {
      html_.append("—");
    } else if (word == "...") {
      html_.append("…");
    } else {
      // TODO: Escape token word.
      html_.append(word);
    }
  }

  void BuildFrameList() {
    // Builds client-side frame list.
    Store *store = document_.store();
    Handle n_evokes = store->Lookup("evokes");
    Handle n_name = store->Lookup("name");

    // Add standard values.
    Add(Handle::isa());
    Add(Handle::is());
    Add(n_name);

    // Add all evoked frames.
    Handles queue(store);
    for (int i = 0; i < document_.num_spans(); ++i) {
      Span *span = document_.span(i);
      if (span->deleted()) continue;
      const Frame &mention = span->mention();

      // Add the mention frame.
      if (Add(mention.handle())) {
        queue.push_back(mention.handle());
        mentions_.push_back(mention.handle());
      }

      // Add all evoked frames.
      for (const Slot &slot : mention) {
        if (slot.name != n_evokes) continue;

        // Queue all evoked frames.
        Handle h = slot.value;
        if (!store->IsFrame(h)) continue;
        if (Add(h)) {
          queue.push_back(h);
        }
      }
    }

    // Add thematic frames.
    for (Handle h : document_.themes()) {
      if (!store->IsFrame(h)) continue;
      if (Add(h)) {
        queue.push_back(h);
      }
      themes_.push_back(h);
    }

    // Process queue.
    int current = 0;
    while (current < queue.size()) {
      // Process all slot names and values for next frame in queue.
      Frame frame(store, queue[current++]);
      for (const Slot &slot : frame) {
        if (store->IsFrame(slot.name)) {
          if (Add(slot.name)) {
            // Only add local frames to queue.
            if (slot.name.IsLocalRef()) queue.push_back(slot.name);
          }
        }
        if (store->IsFrame(slot.value)) {
          if (Add(slot.value)) {
            // Only add local frames to queue.
            if (slot.value.IsLocalRef()) queue.push_back(slot.value);
          }
        }
      }
    }
  }

  // Adds frame to frame list.
  bool Add(Handle h) {
    // Do not add frame if it is already in the list.
    if (mapping_.find(h) != mapping_.end()) return false;

    // Add frame to list and mapping.
    mapping_[h] = frames_.size();
    frames_.push_back(h);
    return true;
  }

  const string &html() const { return html_; }

 private:
  const Document &document_;
  string html_;
  Handles frames_;          // frames by index
  Handles mentions_;        // mentions evoking frames
  Handles themes_;          // thematic frames
  HandleMap<int> mapping_;  // mapping from frame to index
};

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
  if (argc < 2) {
    std::cerr << argv[0] << "[OPTIONS] [FILE] ...\n";
    return 1;
  }

  Store commons;
  if (!FLAGS_commons.empty()) {
    LoadStore(FLAGS_commons, &commons);
  }
  commons.Freeze();

  std::vector<string> files;
  for (int i = 1; i < argc; ++i) {
    File::Match(argv[i], &files);
  }

  RecordFileOptions options;
  RecordDatabase db(files, options);
  Record record;
  CHECK(db.Lookup(FLAGS_key, &record));

  Store store(&commons);
  Frame top = Decode(&store, record.value).AsFrame();
  Document document(top);

  LOG(INFO) << ToText(document.top(), 2);

  DocumentRenderer renderer(document);
  renderer.Render();

  LOG(INFO) << "HTML:" << renderer.html();

  return 0;
}

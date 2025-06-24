// Copyright 2025 Ringgaard Research ApS
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

// Plain-text snippet generator.

#include "sling/frame/store.h"
#include "sling/frame/object.h"
#include "sling/frame/serialization.h"
#include "sling/nlp/search/search-engine.h"


namespace sling {
namespace nlp {

class PlainSnippetGenerator : public SnippetGenerator {
 public:
  void Init() override {
    names_.Bind(&commons_);
    commons_.Freeze();
  }

  string Generate(Text query, Slice record, int length) override {
    // Decode record.
    Store store(&commons_);
    Frame item = Decode(&store, Text(record)).AsFrame();
    if (!item.valid()) return "";

    // Get plain text.
    Text text = item.GetText(n_text_);
    if (text.empty()) return "";

    // Find first match.
    int pos = text.find(query);
    if (pos == -1) return "";

    // Return snippet centered around match. Make sure the snippet is cut on
    // UTF-8 boundaries.
    int half = length / 2;
    int begin = pos - half;
    while (begin > 0 && text[begin - 1] != ' ') begin--;
    if (begin < 0) {
      begin = 0;
    } else {
      while (begin > 0) {
        if ((text[--begin] & 0xc0) != 0x80) break;
      }
    }
    int end = begin + length;
    while (end < text.size() - 1 && text[end + 1] != ' ') end++;
    if (end > text.size()) {
      end = text.size();
    } else {
      while (end > begin) {
        if ((text[--end] & 0xc0) != 0x80) break;
      }
    }

    return text.substr(begin, end - begin).str();
  }

 private:
  Store commons_;
  Names names_;
  Name n_text_{names_, "text"};
};

REGISTER_SNIPPET_GENERATOR("plain", PlainSnippetGenerator);

}  // namespace task
}  // namespace sling

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

#include "sling/nlp/document/lex.h"

#include "sling/base/types.h"
#include "sling/nlp/document/document.h"

namespace sling {
namespace nlp {

bool DocumentLexer::Lex(Document *document, Text lex) const {
  // Extract the plain text from the LEX-encoded text and keep track of
  // mention boundaries.
  std::vector<Span> spans;
  string text;
  string frames;
  std::vector<int> stack;
  std::vector<int> themes;
  int current_span = -1;
  int current_frame = -1;
  int frame_level = 0;
  for (char c : lex) {
    if (frame_level > 0) {
      // Inside frame. Parse until outer '}' found.
      frames.push_back(c);
      if (c == '{') {
        frame_level++;
      } else if (c == '}') {
        if (--frame_level == 0) {
          if (current_span != -1) {
            // Evoke frame for current span.
            spans[current_span].evoked.push_back(current_frame);
          } else {
            // Add frame as theme.
            themes.push_back(current_frame);
          }
        }
      }
    } else {
      switch (c) {
        case '[':
          // Start new span.
          current_span = spans.size();
          stack.emplace_back(current_span);
          spans.emplace_back(text.size());
          break;
        case ']':
          // End current span.
          if (stack.empty()) return false;
          current_span = stack.back();
          stack.pop_back();
          spans[current_span].end = text.size();
          break;
        case '{':
          // Start new frame.
          current_frame++;
          frames.push_back(c);
          frame_level++;
          break;
        default:
          // Add character to plain text. Intervening text resets the current
          // span.
          text.push_back(c);
          current_span = -1;
      }
    }
  }
  if (!stack.empty()) return false;

  // Tokenize plain text and add tokens to document.
  tokenizer_->Tokenize(document, text);

  LOG(INFO) << "text:" << text;
  LOG(INFO) << "frames:" << frames;
  for (auto &span : spans) {
    LOG(INFO) << "span " << span.begin << "-" << span.end << " [" << text.substr(span.begin, span.end - span.begin) << "]";
    for (int frame : span.evoked) LOG(INFO) << "  evoke " << frame;
  }
  for (int theme : themes) LOG(INFO) << "theme " << theme;

  document->Update();
  return true;
}

}  // namespace nlp
}  // namespace sling


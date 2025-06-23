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

// Document snippet generator.

#include "sling/nlp/search/search-engine.h"

namespace sling {
namespace nlp {

class DocumentSnippetGenerator : public SnippetGenerator {
 public:
  ~DocumentSnippetGenerator() {
    LOG(INFO) << "delete document snippet generator";
  }

  void Init() override {
    LOG(INFO) << "init document snippet generator";
  }

  string Generate(Text query, Slice item) override {
    return "<<snippet>>";
  }
};

REGISTER_SNIPPET_GENERATOR("document", DocumentSnippetGenerator);

}  // namespace task
}  // namespace sling

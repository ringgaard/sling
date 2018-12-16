// Copyright 2018 Google Inc.
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

#include "sling/nlp/wiki/wiki-annotator.h"

namespace sling {
namespace nlp {

class TextTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    text_ = config.GetString("text");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    annotator->Content(text_);
  }

 private:
  string text_;
};

REGISTER_WIKI_MACRO("text", TextTemplate);

class FracTemplate : public WikiMacro {
 public:
  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    switch (templ.NumArgs()) {
      case 0:
        annotator->Content("/");
        break;
      case 1:
        annotator->Content("1/");
        annotator->Content(templ.GetValue(1));
        break;
      case 2:
        annotator->Content(templ.GetValue(1));
        annotator->Content("/");
        annotator->Content(templ.GetValue(2));
        break;
      case 3:
        annotator->Content(templ.GetValue(1));
        annotator->Content("&nbsp;");
        annotator->Content(templ.GetValue(2));
        annotator->Content("/");
        annotator->Content(templ.GetValue(3));
        break;
    }
  }
};

REGISTER_WIKI_MACRO("frac", FracTemplate);

class TagTemplate : public WikiMacro {
 public:
  void Init(const Frame &config) override {
    arg_ = config.GetString("arg");
    argnum_ = config.GetInt("argnum", 0);
    open_ = config.GetString("open");
    close_ = config.GetString("close");
  }

  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    annotator->Content(open_);
    const Node *content = nullptr;
    if (!arg_.empty()) {
      content = templ.GetArgument(arg_);
    }
    if (content == nullptr && argnum_ != 0) {
      content = templ.GetArgument(argnum_);
    }
    if (content != nullptr) {
      templ.extractor()->ExtractNode(*content);
    }
    annotator->Content(close_);
  }

 private:
  string arg_;
  int argnum_;
  string open_;
  string close_;
};

REGISTER_WIKI_MACRO("tag", TagTemplate);

class DefineTemplate : public WikiMacro {
 public:
  void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) override {
    const Node *abbreviation = templ.GetArgument(1);
    if (abbreviation != nullptr) {
      templ.extractor()->ExtractNode(*abbreviation);
    }
    const Node *meaning = templ.GetArgument(2);
    if (meaning != nullptr) {
      annotator->Content(" (");
      templ.extractor()->ExtractNode(*meaning);
      annotator->Content(")");
    }
  }
};

REGISTER_WIKI_MACRO("define", DefineTemplate);

}  // namespace nlp
}  // namespace sling


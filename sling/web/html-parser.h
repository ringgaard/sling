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

#ifndef SLING_WEB_HTML_PARSER_H_
#define SLING_WEB_HTML_PARSER_H_

#include "sling/web/xml-parser.h"

namespace sling {

class HTMLParser : public XMLParser {
 public:
  HTMLParser() {}
  virtual ~HTMLParser() {}

  // Parse HTML from input and call callbacks.
  virtual bool Parse(Input *input);

  // Callbacks.
  virtual bool DocType(const char *str);
  virtual bool CData(const char *str);

 private:
  // HTML tag type.
  enum TagType {REGULAR, UNPARSED, SINGLE, IMPLICIT};

  // Special tag type for tags starting with exclamation mark.
  enum SpecialTagType {EMPTY, COMMENT, DOCTYPE, CDATA};

  // Check if character is a HTML name character.
  static bool IsNameChar(int ch);

  // Get the type for an HTML tag.
  static TagType GetTagType(const char *tag);
};

}  // namespace sling

#endif  // SLING_WEB_HTML_PARSER_H_


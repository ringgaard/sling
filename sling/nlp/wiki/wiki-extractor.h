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

#ifndef SLING_NLP_WIKI_WIKI_EXTRACTOR_H_
#define SLING_NLP_WIKI_WIKI_EXTRACTOR_H_

#include "sling/nlp/wiki/wiki-parser.h"

namespace sling {
namespace nlp {

// Extract text and annotations from Wikipedia page.
class WikiExtractor {
 public:
  typedef WikiParser::Node Node;

  // Initialize Wikipedia text extractor.
  WikiExtractor(const WikiParser &parser) : parser_(parser) {}

  // Extract text by traversing the nodes in the AST.
  void Extract();

  // Return extracted text.
  const string &text() const { return text_; }

 protected:
  // Extract text and annotations from node. This uses the handler for the node
  // type for extraction.
  void ExtractNode(const Node &node);

  // Extract text and annotations from all children of the parent node.
  void ExtractChildren(const Node &parent);

  // Handlers for extracting text and annotations for specific AST node types.
  // The can be overridden in subclasses to customize the extraction.
  virtual void ExtractDocument(const Node &node);
  virtual void ExtractArg(const Node &node);
  virtual void ExtractAttr(const Node &node);
  virtual void ExtractText(const Node &node);
  virtual void ExtractFont(const Node &node);
  virtual void ExtractTemplate(const Node &node);
  virtual void ExtractLink(const Node &node);
  virtual void ExtractImage(const Node &node);
  virtual void ExtractCategory(const Node &node);
  virtual void ExtractUrl(const Node &node);
  virtual void ExtractComment(const Node &node);
  virtual void ExtractTag(const Node &node);
  virtual void ExtractBeginTag(const Node &node);
  virtual void ExtractEndTag(const Node &node);
  virtual void ExtractMath(const Node &node);
  virtual void ExtractGallery(const Node &node);
  virtual void ExtractReference(const Node &node);
  virtual void ExtractHeading(const Node &node);
  virtual void ExtractIndent(const Node &node);
  virtual void ExtractTerm(const Node &node);
  virtual void ExtractListBegin(const Node &node);
  virtual void ExtractListItem(const Node &node);
  virtual void ExtractListEnd(const Node &node);
  virtual void ExtractRuler(const Node &node);
  virtual void ExtractSwitch(const Node &node);
  virtual void ExtractTable(const Node &node);
  virtual void ExtractTableCaption(const Node &node);
  virtual void ExtractTableRow(const Node &node);
  virtual void ExtractTableHeader(const Node &node);
  virtual void ExtractTableCell(const Node &node);
  virtual void ExtractTableBreak(const Node &node);

  // Extraction of annotations from skipped AST nodes.
  virtual void ExtractSkip(const Node &node);
  virtual void ExtractUnanchoredLink(const Node &node);

  // Get attribute value from child nodes.
  Text GetAttr(const Node &node, Text attrname);

  // Reset font back to normal.
  void ResetFont();

  // Emit text to text buffer.
  void Emit(const char *begin, const char *end);
  void Emit(const char *str) { Emit(str, str + strlen(str)); }
  void Emit(Text str) { Emit(str.data(), str.data() +str.size()); }
  void Emit(const Node &node) { Emit(node.begin, node.end); }

  // Wiki text parser with AST.
  const WikiParser &parser_;

  // Extracted text.
  string text_;

  // String for text output.
  string *output_ = &text_;

  // Number of pending line breaks.
  int line_breaks_ = 0;

  // Current font.
  int font_ = 0;

  // Deferred lead text that is emitted if any other text is emitted.
  const char *deferred_text_ = nullptr;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_WIKI_WIKI_EXTRACTOR_H_


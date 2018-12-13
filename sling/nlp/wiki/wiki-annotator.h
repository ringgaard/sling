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

#ifndef SLING_NLP_WIKI_WIKI_ANNOTATOR_H_
#define SLING_NLP_WIKI_WIKI_ANNOTATOR_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "sling/base/registry.h"
#include "sling/base/types.h"
#include "sling/frame/object.h"
#include "sling/nlp/document/document.h"
#include "sling/nlp/wiki/wiki-extractor.h"
#include "sling/string/text.h"

namespace sling {
namespace nlp {

class WikiAnnotator;

// Abstract class for resolving Wikipedia links.
class WikiLinkResolver {
 public:
  virtual ~WikiLinkResolver() = default;

  // Resolve link to Wikipedia article returning Wikidata QID for item.
  virtual Text ResolveLink(Text link) = 0;

  // Resolve link to Wikipedia template returning Wikidata QID for item.
  virtual Text ResolveTemplate(Text link) = 0;

  // Resolve link to Wikipedia category returning Wikidata QID for item.
  virtual Text ResolveCategory(Text link) = 0;
};

// Wrapper around wiki template node.
class WikiTemplate {
 public:
  typedef WikiParser::Node Node;

  WikiTemplate(const Node &node, WikiExtractor *extractor)
      : node_(node), extractor_(extractor) {}

  // Return template name.
  Text name() const { return node_.name(); }

  // Return the number of positional (i.e. unnamed) arguments.
  int NumArgs() const;

  // Return node for named template argument, or null if it is not found.
  const Node *GetArgument(Text name);

  // Return node for positional template argument.
  const Node *GetArgument(int index);

  // Return plain text value for named template argument.
  string GetValue(Text name);

  // Return plain text value for positional template argument.
  string GetValue(int index);

  // Return template extractor.
  WikiExtractor *extractor() const { return extractor_; }

 private:
  // Template node.
  const Node &node_;

  // Extractor for extracting template argument values.
  WikiExtractor *extractor_;
};

// A wiki macro processor is used for expanding wiki templates into text and
// annotations.
class WikiMacro : public Component<WikiMacro> {
 public:
  virtual ~WikiMacro() = default;

  // Initialize wiki macro processor from configuration.
  virtual void Init(const Frame &config) {}

  // Expand template by adding content and annotations to annotator.
  virtual void Generate(const WikiTemplate &templ, WikiAnnotator *annotator) {}
};

#define REGISTER_WIKI_MACRO(type, component) \
    REGISTER_COMPONENT_TYPE(sling::nlp::WikiMacro, type, component)

// Repository of wiki macro configurations for a language for expanding wiki
// templates when processing a Wikipedia page.
class WikiTemplateRepository {
 public:
  ~WikiTemplateRepository();

  // Intialize repository from configuration.
  void Init(const Frame &frame);

  // Look up macro processor for temaplate name .
  WikiMacro *Lookup(Text name);

 private:
  // Get fingerprint for template name. Template names are case-insensitive.
  static uint64 Fingerprint(Text name);

  // Mapping from (fingerprint of) template name to wiki macro procesor.
  std::unordered_map<uint64, WikiMacro *> repository_;
};

// Wiki extractor sink for collecting text and annotators for Wikipedia page.
// It collects text span information about evoked frames than can later be added
// to a SLING document when the text has been tokenized. It also collects
// thematic frames for unanchored annotations.
class WikiAnnotator : public WikiTextSink {
 public:
  // Initialize document annotator. The frame annotations will be created in
  // the store and links will be resolved using the resolver.
  WikiAnnotator(Store *store, WikiLinkResolver *resolver);

  // Wiki sink interface receiving the annotations from the extractor.
  void Link(const Node &node,
            WikiExtractor *extractor,
            bool unanchored) override;
  void Template(const Node &node,
                WikiExtractor *extractor,
                bool unanchored) override;
  void Category(const Node &node,
                WikiExtractor *extractor,
                bool unanchored) override;

  // Add annotations to tokenized document.
  void AddToDocument(Document *document);

  // Add frame evoked from span.
  void AddMention(int begin, int end, Handle frame);

  // Add thematic frame.
  void AddTheme(Handle theme);

  // Add category.
  void AddCategory(Handle category);

  // Return store for annotations.
  Store *store() { return store_; }

  // Return link resolver.
  WikiLinkResolver *resolver() { return resolver_; }

  // Get/set template repository.
  WikiTemplateRepository *templates() const { return templates_; }
  void set_templates(WikiTemplateRepository *templates) {
    templates_ = templates;
  }

 private:
  // Annotated span with byte-offset interval for the phrase in the text as well
  // as the evoked frame. The begin and end offsets are encoded as integer
  // handles to allow tracking by the frame store.
  struct Annotation {
    Annotation(int begin, int end, Handle evoked)
        : begin(Handle::Integer(begin)),
          end(Handle::Integer(end)),
          evoked(evoked) {}

    Handle begin;
    Handle end;
    Handle evoked;
  };

  // Vector of annotations that are tracked as external references.
  class Annotations : public std::vector<Annotation>, public External {
   public:
    explicit Annotations(Store *store) : External(store) {}

    void GetReferences(Range *range) override {
      range->begin = reinterpret_cast<Handle *>(data());
      range->end = reinterpret_cast<Handle *>(data() + size());
    }
  };

  // Store for frame annotations.
  Store *store_;

  // Link resolver.
  WikiLinkResolver *resolver_;

  // Template generator.
  WikiTemplateRepository *templates_;

  // Annotated spans.
  Annotations annotations_;

  // Thematic frames.
  Handles themes_;

  // Categories.
  Handles categories_;

  // Symbols.
  Names names_;
  Name n_name_{names_, "name"};
  Name n_link_{names_, "/wp/link"};
  Name n_page_category_{names_, "/wp/page/category"};
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_WIKI_WIKI_ANNOTATOR_H_


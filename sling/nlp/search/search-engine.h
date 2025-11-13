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

#ifndef SLING_NLP_SEARCH_ENGINE_H_
#define SLING_NLP_SEARCH_ENGINE_H_

#include "sling/base/registry.h"
#include "sling/base/types.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/search/search-index.h"
#include "sling/string/ctype.h"
#include "sling/util/top.h"

namespace sling {
namespace nlp {

class SearchEngine {
 public:
  typedef SearchIndex::Term Term;
  typedef SearchIndex::Document Document;

  // Query hit.
  struct Hit {
    Hit(const Document *document) : document(document), score(0) {}
    Text id() const { return document->id(); }
    const Document *document;
    int score;
  };

  // Search hit comparison operator.
  struct HitCompare {
    bool operator()(const Hit &a, const Hit &b) {
      return a.score > b.score;
    }
  };

  // Search results for selecting top-k results.
  typedef Top<Hit, HitCompare> Hits;

  // Search query.
  enum QueryType { EXCLUDE, OR, AND, PHRASE, TERMS };

  class Parser {
   public:
    Parser(Text query) : query(query), pos(0) {}

    char current() { return more() ? query[pos] : 0; }

    void next() { pos++; }

    void skipws() {
      while (more() && ascii_isblank(query[pos])) pos++;
    }

    Text phrase() {
      int start = pos;
      while (more() && query[pos] != '"') pos++;
      return query.substr(start, pos - start);
    }

    Text terms() {
      int start = pos;
      while (more()) {
        char ch = current();
        if (ch == '!' || ch == '\\' ||
            ch == '&' || ch == '|' ||
            ch == '(' || ch == ')') {
          break;
        }
        pos++;
      }
      return query.substr(start, pos - start);
    }

    Text rest() { return query.substr(pos); }

    bool more() { return pos < query.size(); }

   private:
    Text query;
    int pos;

  };

  struct Query {
    Query(QueryType type, Query *left = nullptr, Query *right = nullptr)
      : type(type), left(left), right(right) {}

    ~Query() {
      delete left;
      delete right;
    }

    QueryType type;
    Query *left;
    Query *right;
    string terms;
    std::vector<uint64> fingerprints;
  };

  // Posting list with document ids.
  struct Matches {
    // Initialize posting list from repository term.
    Matches(const Term *term = nullptr) : term(term) {}

    // Start of match list.
    const uint32 *begin() {
      return term != nullptr ? term->documents() : docids.data();
    }

    // End of match list.
    const uint32 *end() {
      if (term != nullptr) {
        return term->documents() + term->num_documents();
      } else {
        return docids.data() + docids.size();
      }
    }

    // Return size of posting list.
    int size() const { return term ? term->num_documents() : docids.size();}

    // Check if posting list is empty.
    bool empty() const { return size() == 0; }

    // Swap this posting list with another.
    void swap(Matches &other) {
      std::swap(term, other.term);
      docids.swap(other.docids);
    }

    // Add match.
    void add(uint32 docid) { docids.push_back(docid); }

    // Repository term with matching documents.
    const Term *term = nullptr;

    // List of matching document ids.
    std::vector<uint32> docids;
  };

  // Search results.
  class Results {
   public:
    Results(int limit, int maxambig) : hits_(limit), maxambig_(maxambig) {}

    // Return search matches.
    const Hits &hits() const { return hits_; }

    // Score document against query.
    int Score(const Document *document);

    // Maximum query ambiguity.
    int maxambig() const { return maxambig_; }

   private:
    // Check for unigram query match.
    bool Unigram(uint16 term) const;

    // Check for bigram query match.
    bool Bigram(uint16 term1, uint16 term2) const;

    // Work fingerprints for search terms.
    std::vector<uint16> query_terms_;

    // Search hits.
    Hits hits_;

    // Total number of matches.
    int total_hits_ = 0;

    // Maximum ambiguity.
    int maxambig_ = 0;

    friend class SearchEngine;
  };

  // Load search engine index.
  void Load(const string &filename);

  // Search for matches in search index and put the k-best matches into the
  // result list. Returns the total number of matches.
  int Search(Text query, Results *results);

  // Parse query
  Query *ParseQuery(Parser *parser);
  Query *ParseUnion(Parser *parser);
  Query *ParseIntersection(Parser *parser);
  Query *ParseFactor(Parser *parser);
  void ExtractTerms(Query *query, std::vector<uint16> *terms);
  void QueryToString(Query *query, string *str);

  // Find documents matching query.
  void Match(Query *query, Matches *matches);
  void MatchTerms(Query *query, Matches *matches);

  // Check if search index has been loaded.
  bool loaded() const { return index_.loaded(); }

  // Tokenize text.
  void tokenize(Text text, std::vector<uint64> *tokens) const {
    tokenizer_.TokenFingerprints(text, tokens);
  }

 private:
  // Search index.
  SearchIndex index_;

  // Tokenizer for tokenizing query.
  PhraseTokenizer tokenizer_;
};

// A snippet generator extracts a query-dependent snippet from a search result.
class SnippetGenerator : public Component<SnippetGenerator> {
 public:
  virtual ~SnippetGenerator() = default;

  // Initialize snippet generator.
  virtual void Init() = 0;

  // Generate snippet for query and result.
  virtual string Generate(Text query, Slice item, int length) = 0;
};

#define REGISTER_SNIPPET_GENERATOR(type, component) \
    REGISTER_COMPONENT_TYPE(SnippetGenerator, type, component)

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_ENGINE_H_

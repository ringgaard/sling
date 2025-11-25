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

#include <algorithm>
#include <string>
#include <vector>

#include "sling/nlp/search/search-engine.h"

#include "sling/util/unicode.h"

REGISTER_COMPONENT_REGISTRY("snippet generator", sling::nlp::SnippetGenerator);

namespace sling {
namespace nlp {

void SearchEngine::Load(const string &filename) {
  // Load search index.
  index_.Load(filename);

  // Initialize tokenizer.
  tokenizer_.set_normalization(ParseNormalization(index_.normalization()));
}

SearchEngine::Query *SearchEngine::ParseQuery(Parser *parser) {
  Query *query = ParseUnion(parser);
  parser->skipws();
  while (parser->current() == '\\' || parser->current() == '!') {
    parser->next();
    Query *sub = ParseUnion(parser);
    query = new Query(EXCLUDE, query, sub);
    parser->skipws();
  }

  return query;
}

SearchEngine::Query *SearchEngine::ParseUnion(Parser *parser) {
  Query *query = ParseIntersection(parser);
  parser->skipws();
  while (parser->current() == '|') {
    parser->next();
    Query *sub = ParseIntersection(parser);
    query = new Query(OR, query, sub);
    parser->skipws();
  }

  return query;
}

SearchEngine::Query *SearchEngine::ParseIntersection(Parser *parser) {
  Query *query = ParseFactor(parser);
  parser->skipws();
  while (parser->current() == '&') {
    parser->next();
    Query *sub = ParseFactor(parser);
    query = new Query(AND, query, sub);
    parser->skipws();
  }

  return query;
}

SearchEngine::Query *SearchEngine::ParseFactor(Parser *parser) {
  parser->skipws();
  if (parser->current() == '(') {
    parser->next();
    Query *query = ParseQuery(parser);
    parser->skipws();
    if (parser->current() == ')') parser->next();
    return query;
  } else if (parser->current() == '"') {
    parser->next();
    Text phrase = parser->phrase().trim();
    Query *query = new Query(PHRASE);
    query->terms.assign(phrase.data(), phrase.size());
    tokenize(query->terms, &query->fingerprints);
    parser->skipws();
    if (parser->current() == '"') parser->next();
    return query;
  } else {
    Text terms = parser->terms().trim();
    Query *query = new Query(TERMS);
    query->terms.assign(terms.data(), terms.size());
    tokenize(query->terms, &query->fingerprints);
    return query;
  }
}

void SearchEngine::QueryToString(Query *query, string *str) {
  switch (query->type) {
    case EXCLUDE:
      str->append("EXCLUDE(");
      QueryToString(query->left, str);
      str->append(",");
      QueryToString(query->right, str);
      str->append(")");
      break;

    case OR:
      str->append("OR(");
      QueryToString(query->left, str);
      str->append(",");
      QueryToString(query->right, str);
      str->append(")");
      break;

    case AND:
      str->append("AND(");
      QueryToString(query->left, str);
      str->append(",");
      QueryToString(query->right, str);
      str->append(")");
      break;

    case TERMS:
      str->append(tokenizer_.Normalize(query->terms));
      break;

    case PHRASE:
      str->append("\"");
      str->append(tokenizer_.Normalize(query->terms));
      str->append("\"");
      break;
  }
}

void SearchEngine::ExtractTerms(Query *query, std::vector<uint16> *terms) {
  switch (query->type) {
    case EXCLUDE:
      ExtractTerms(query->left, terms);
      break;

    case AND:
    case OR:
      ExtractTerms(query->left, terms);
      ExtractTerms(query->right, terms);
      break;

    case TERMS:
    case PHRASE:
      for (uint64 fp : query->fingerprints) {
        terms->push_back(WordFingerprint(fp));
      }
      break;
  }
}


int SearchEngine::Search(Text query, Results *results) {
  // Return empty result if index has not been loaded.
  if (!loaded()) return 0;

  // Find matches.
  Parser parser(query);
  Query *expression = ParseQuery(&parser);

  string str;
  QueryToString(expression, &str);
  LOG(INFO) << "Query: " << query << " -> " << str;

  Matches matches;
  Match(expression, &matches);
  ExtractTerms(expression, &results->query_terms_);
  delete expression;

  // Rank hits.
  const uint32 *begin =  matches.begin();
  const uint32 *end =  matches.end();
  int hits = end - begin;
  results->total_hits_ = hits;
  if (hits > results->maxambig()) {
    // Limit the number of scored documents for very ambiguous queries.
    end = begin + results->maxambig();
  }
  for (const uint32 *c = begin; c != end; ++c) {
    Hit hit(index_.GetDocument(*c));
    hit.score = results->Score(hit.document);
    results->hits_.push(hit);
  }
  results->hits_.sort();
  return hits;
}

void SearchEngine::Match(Query *query, Matches *matches) {
  switch (query->type) {
    case EXCLUDE: {
      // Find all in left except those that match right,
      Matches left;
      Matches right;
      Match(query->left, &left);
      Match(query->right, &right);

      if (right.empty()) {
        matches->swap(left);
      } else {
        const uint32 *l = left.begin();
        const uint32 *lend = left.end();
        const uint32 *r = right.begin();
        const uint32 *rend = right.end();

        while (l < lend && r < rend) {
          if (*l < *r) {
            matches->add(*l);
            l++;
          } else if (*r < *l) {
            r++;
          } else {
            r++;
            l++;
          }
        }
        while (l < lend) {
          matches->add(*l);
          l++;
        }
      }
      break;
    }

    case OR: {
      // Find all that are either in left or right,
      Matches left;
      Matches right;
      Match(query->left, &left);
      Match(query->right, &right);

      if (left.empty()) {
        matches->swap(right);
      } if (right.empty()) {
        matches->swap(left);
      } else {
        const uint32 *l = left.begin();
        const uint32 *lend = left.end();
        const uint32 *r = right.begin();
        const uint32 *rend = right.end();

        while (l < lend && r < rend) {
          if (*l < *r) {
            matches->add(*l);
            l++;
          } else if (*r < *l) {
            matches->add(*r);
            r++;
          } else {
            matches->add(*l);
            r++;
            l++;
          }
        }
        while (l < lend) {
          matches->add(*l);
          l++;
        }
        while (r < rend) {
          matches->add(*r);
          r++;
        }
      }
      break;
    }

    case AND: {
      // Find all matches that are both in left and right,
      Matches left;
      Matches right;
      Match(query->left, &left);
      Match(query->right, &right);

      if (!left.empty() && !right.empty()) {
        const uint32 *l = left.begin();
        const uint32 *lend = left.end();
        const uint32 *r = right.begin();
        const uint32 *rend = right.end();

        while (l < lend && r < rend) {
          if (*l < *r) {
            l++;
          } else if (*r < *l) {
            r++;
          } else {
            matches->add(*l);
            r++;
            l++;
          }
        }
      }
      break;
    }

    case PHRASE:
    case TERMS:
      MatchTerms(query, matches);
      break;
  }
}

void SearchEngine::MatchTerms(Query *query, Matches *matches) {
  // Look up posting lists for tokens in search index.
  std::vector<const SearchIndex::Term *> terms;
  for (uint64 token : query->fingerprints) {
    if (index_.stopword(token)) continue;

    const SearchIndex::Term *term = index_.Find(token);
    if (term == nullptr) return;
    terms.push_back(term);
  }
  if (terms.empty()) return;

  // Sort search terms by frequency starting with the most rare terms.
  std::sort(terms.begin(), terms.end(),
    [](const SearchIndex::Term *a, const SearchIndex::Term *b) {
        return a->num_documents() < b->num_documents();
    });

  // Initialize candidates from first term.
  Matches candidates(terms[0]);

  // Match the rest of the search terms.
  for (int i = 1;  i < terms.size(); ++i) {
    Matches next(terms[i]);

    const uint32 *c = candidates.begin();
    const uint32 *cend = candidates.end();
    const uint32 *n = next.begin();
    const uint32 *nend = next.end();

    // Intersect current candidates with postings for term.
    Matches intersection;
    while (c < cend && n < nend) {
      if (*n < *c) {
        n++;
      } else if (*c < *n) {
        c++;
      } else {
        intersection.add(*c);
        c++;
        n++;
      }
    }

    // Bail out if there are no more candidates.
    if (intersection.empty()) return;

    // Swap intersection and candidates.
    candidates.swap(intersection);
  }

  matches->swap(candidates);
}

int SearchEngine::Results::Score(const Document *document) {
  int unigrams = 0;
  int bigrams = 0;
  int importance = 1;
  const uint16 *begin = document->tokens();
  const uint16 *end = begin + document->num_tokens();
  uint16 prev = WORDFP_BREAK;

  for (const uint16 *t = begin; t < end; ++t) {
    uint16 token = *t;
    if (Unigram(token)) {
      unigrams += importance;
      if (prev != WORDFP_BREAK && Bigram(prev, token)) {
        bigrams += importance;
      }
    }
    if (token == WORDFP_BREAK) importance = 1;
    if (token == WORDFP_IMPORTANT) importance = 50;
    prev = token;
  }

  int boost = 100 * bigrams + 10 * unigrams + 1;
  if (unigrams == query_terms_.size()) boost++;
  return (document->score() + 1) * boost;
}

bool SearchEngine::Results::Unigram(uint16 term) const {
  for (int i = 0; i < query_terms_.size(); ++i) {
    if (query_terms_[i] == term) return true;
  }
  return false;
}

bool SearchEngine::Results::Bigram(uint16 term1, uint16 term2) const {
  for (int i = 0; i < query_terms_.size() - 1; ++i) {
    if (query_terms_[i] == term1 && query_terms_[i + 1] == term2) return true;
  }
  return false;
}

}  // namespace nlp
}  // namespace sling

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

#ifndef SLING_NLP_SEARCH_INDEX_H_
#define SLING_NLP_SEARCH_INDEX_H_

#include <string>
#include <unordered_set>

#include "sling/base/types.h"
#include "sling/file/repository.h"
#include "sling/string/text.h"
#include "sling/util/json.h"

namespace sling {
namespace nlp {

// Search index with posting lists for each search term.
class SearchIndex {
 public:
  // Document item in repository.
  class Document : public RepositoryObject {
   public:
    // Document id.
    Text id() const { return Text(id_ptr(), *idlen_ptr()); }

    // Base score for document, e.g. popularity or frequency.
    uint32 score() const { return *score_ptr(); }

    // Number of document tokens.
    uint32 num_tokens() const { return *tokenlen_ptr(); }

    // List of document 16-bit token fingerprints.
    const uint16 *tokens() const { return tokens_ptr(); }

   private:
    // Document score.
    REPOSITORY_FIELD(uint32, score, 1, 0);

    // Document id size.
    REPOSITORY_FIELD(uint8, idlen, 1, AFTER(score));

    // Document token array length.
    REPOSITORY_FIELD(uint32, tokenlen, 1, AFTER(idlen));

    // Document id.
    REPOSITORY_FIELD(char, id, *idlen_ptr(), AFTER(tokenlen));

    // Document tokens.
    REPOSITORY_FIELD(uint16, tokens, *tokenlen_ptr(), AFTER(id));
  };

  // Term with posting list in repository.
  class Term : public RepositoryObject {
   public:
    // Return fingerprint.
    uint64 fingerprint() const { return *fingerprint_ptr(); }

    // Return number of documents matching term.
    int num_documents() const { return *doclen_ptr(); }

    // Return array of documents matching term.
    const uint32 *documents() const { return documents_ptr(); }

    // Return next term in list.
    const Term *next() const {
      int size = sizeof(uint64) + sizeof(uint32) +
                 num_documents() * sizeof(uint32);
      const char *self = reinterpret_cast<const char *>(this);
      return reinterpret_cast<const Term *>(self + size);
    }

   private:
    // Term fingerprint.
    REPOSITORY_FIELD(uint64, fingerprint, 1, 0);

    // Document list.
    REPOSITORY_FIELD(uint32, doclen, 1, AFTER(fingerprint));
    REPOSITORY_FIELD(uint32, documents, num_documents(), AFTER(doclen));
  };

  // Load search index from file.
  void Load(const string &filename);

  // Find matching term in term table. Return null if term is not found.
  const Term *Find(uint64 fp) const;

  // Get document from document index.
  const Document *GetDocument(int index) const {
    return document_index_.GetDocument(index);
  }

  // Search query normalization.
  string normalization() const { return params_["normalization"]; }

  // Check if term is a stopword.
  bool stopword(uint64 fp) const {
    return fp == 1 || stopwords_.count(fp) > 0;
  }

  // Check if search index has been loaded.
  bool loaded() const { return repository_.loaded(); }

 private:
  // Document index in repository.
  class DocumentIndex : public RepositoryIndex<uint64, Document> {
   public:
    // Initialize name index.
    void Initialize(const Repository &repository) {
      Init(repository, "DocumentIndex", "DocumentItems", false);
    }

    // Return document from document index.
    const Document *GetDocument(int index) const {
      return GetObject(index);
    }
  };

  // Term index in repository.
  class TermIndex : public RepositoryMap<Term> {
   public:
    // Initialize phrase index.
    void Initialize(const Repository &repository) {
      Init(repository, "Term");
    }

    // Return first element in bucket.
    const Term *GetBucket(int bucket) const { return GetObject(bucket); }
  };

  // Repository with search index.
  Repository repository_;

  // Document index.
  DocumentIndex document_index_;

  // Term index.
  TermIndex term_index_;

  // Number of term buckets.
  int num_buckets_ = 0;

  // Search index parameters.
  JSON params_;

  // Stopwords.
  std::unordered_set<uint64> stopwords_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_INDEX_H_

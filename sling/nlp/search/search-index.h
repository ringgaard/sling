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

namespace sling {
namespace nlp {

// Search index with item posting lists for each search term.
class SearchIndex {
 public:
  // Entity item in repository.
  class Entity : public RepositoryObject {
   public:
    // Entity id.
    Text id() const { return Text(id_ptr(), *idlen_ptr()); }

    // Base score for entity, e.g. popularity or frequency.
    uint32 score() const { return *score_ptr(); }

   private:
    // Entity score.
    REPOSITORY_FIELD(uint32, score, 1, 0);

    // Entity id.
    REPOSITORY_FIELD(uint8, idlen, 1, AFTER(score));
    REPOSITORY_FIELD(char, id, *idlen_ptr(), AFTER(idlen));
  };

  // Term with posting list in repository.
  class Term : public RepositoryObject {
   public:
    // Return fingerprint.
    uint64 fingerprint() const { return *fingerprint_ptr(); }

    // Return number of entities matching term.
    int num_entities() const { return *entlen_ptr(); }

    // Return array of entities matching term.
    const uint32 *entities() const { return entities_ptr(); }

    // Return next term in list.
    const Term *next() const {
      int size = sizeof(uint64) + sizeof(uint32) +
                 num_entities() * sizeof(uint32);
      const char *self = reinterpret_cast<const char *>(this);
      return reinterpret_cast<const Term *>(self + size);
    }

   private:
    // Term fingerprint.
    REPOSITORY_FIELD(uint64, fingerprint, 1, 0);

    // Entity list.
    REPOSITORY_FIELD(uint32, entlen, 1, AFTER(fingerprint));
    REPOSITORY_FIELD(uint32, entities, num_entities(), AFTER(entlen));
  };

  // Load search index from file.
  void Load(const string &filename);

  // Find matching term in term table. Return null if term is not found.
  const Term *Find(uint64 fp) const;

  // Get entity from entity index.
  const Entity *GetEntity(int index) const {
    return entity_index_.GetEntity(index);
  }

  // Search query normalization.
  string normalization() const {
    return repository_.GetBlockString("normalization");
  }

  // Check if term is a stopword.
  bool stopword(uint64 fp) const {
    return fp == 1 || stopwords_.count(fp) > 0;
  }

  // Check if search index has been loaded.
  bool loaded() const { return repository_.loaded(); }

 private:
  // Entity index in repository.
  class EntityIndex : public RepositoryIndex<uint32, Entity> {
   public:
    // Initialize name index.
    void Initialize(const Repository &repository) {
      Init(repository, "EntityIndex", "EntityItems", false);
    }

    // Return entity from entity index.
    const Entity *GetEntity(int index) const {
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

  // Entity index.
  EntityIndex entity_index_;

  // Term index.
  TermIndex term_index_;

  // Number of term buckets.
  int num_buckets_ = 0;

  // Stopwords.
  std::unordered_set<uint64> stopwords_;
};

}  // namespace nlp
}  // namespace sling

#endif  // SLING_NLP_SEARCH_INDEX_H_


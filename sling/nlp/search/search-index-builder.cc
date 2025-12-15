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

#include <arpa/inet.h>  // for htonl
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "sling/nlp/document/lex.h"
#include "sling/nlp/document/phrase-tokenizer.h"
#include "sling/nlp/kb/calendar.h"
#include "sling/nlp/search/search-dictionary.h"
#include "sling/nlp/search/search-config.h"
#include "sling/nlp/wiki/wiki.h"
#include "sling/task/frames.h"
#include "sling/task/task.h"
#include "sling/string/text.h"
#include "sling/util/json.h"
#include "sling/util/iobuffer.h"
#include "sling/util/mutex.h"
#include "sling/util/unicode.h"

namespace sling {
namespace nlp {

// Extract term lists for items.
class SearchIndexMapper : public task::FrameProcessor {
 public:
  typedef std::unordered_set<uint64> Terms;
  typedef std::vector<uint16> Words;

  void Startup(task::Task *task) override {
    // Read search index configuration.
    config_.Load(commons_, task->GetInputFile("config"));

    // Get output channels.
    documents_ = task->GetSink("documents");
    terms_ = task->GetSink("terms");

    // Load search dictionary.
    LOG(INFO) << "Load search dictionary";
    const string &dictfn = task->GetInput("dictionary")->resource()->name();
    dictionary_.Load(dictfn);
    LOG(INFO) << "Dictionary loaded";

    // Initialize year terms.
    for (int y = 0; y < MAX_YEAR; ++y) {
      string year = std::to_string(y);
      year_terms_[y] = Fingerprint(year.data(), year.size());
    }

    // Initialize aux filter.
    task->Fetch("aux", &aux_);
    if (aux_ != 2) filter_.Init(commons_);

    // Statistics.
    num_items_ = task->GetCounter("items");
    num_terms_ = task->GetCounter("terms");
    num_words_ = task->GetCounter("words");
    num_stopwords_ = task->GetCounter("stopwords");
    num_discarded_ = task->GetCounter("discarded");
  }

  void Process(Slice key, uint64 serial, const Frame &frame) override {
    // Skip non-entity items.
    Store *store = frame.store();
    for (const Slot &s : frame) {
      if (s.name == n_instance_of_) {
        Handle type = store->Resolve(s.value);
        if (config_.skipped(type)) return;
      }
    }

    // Skip/keep aux items; 0=skip aux, 1=keep aux, 2=discard none.
    if (aux_ != 2) {
      bool discard = filter_.IsAux(frame);
      if (aux_ == 1) discard = !discard;
      if (discard) {
        num_discarded_->Increment();
        return;
      }
    }

    // Compute frequency count for item.
    int popularity = frame.GetInt(n_popularity_);
    int fanin = frame.GetInt(n_fanin_);
    int count = popularity + fanin;

    // Collect search terms for item.
    Terms terms;
    Words words;
    bool omit_properties = config_.omit(key.str());
    for (const Slot &s : frame) {
      // Check if properties should be indexed.
      Handle type = config_.index(s.name);
      if (type.IsNil()) continue;
      if (omit_properties && type != n_name_ && type != n_text_) continue;
      Handle value = store->Resolve(s.value);

      if (type == n_name_ || type == n_text_) {
        // Skip names in foreign languages.
        if (store->IsString(value)) {
          Handle lang = store->GetString(value)->qualifier();
          if (!config_.foreign(lang)) {
            Text text = store->GetString(value)->str();
            bool important = type == n_name_;
            Collect(&terms, &words, text, important);
          }
        }
      } else if (type == n_item_) {
        if (store->IsFrame(value)) {
          Text id = store->FrameId(value);
          const SearchDictionary::Item *item = dictionary_.Find(id);
          Collect(&terms, item);
        } else if (store->IsString(value)) {
          Text str = store->GetString(value)->str();
          Collect(&terms, &words, str, false);
        }
      } else if (type == n_date_) {
        Date date(Object(store, value));
        if (date.precision >= Date::YEAR) {
          if (date.year > 0 && date.year < MAX_YEAR) {
            terms.insert(year_terms_[date.year]);
          }
        }
      } else if (type == n_lex_) {
        if (store->IsString(value)) {
          Handle lang = store->GetString(value)->qualifier();
          if (!config_.foreign(lang)) {
            Text lex = store->GetString(value)->str();
            Document document(store);
            if (lexer_.Lex(&document, lex)) {
              Collect(&terms, &words, document);
            }
          }
        }
      }
    }
    num_items_->Increment();
    num_terms_->Increment(terms.size());
    num_words_->Increment(words.size());

    // Get entity id for item.
    uint32 entityid = OutputEntity(key, count, words);
    CHECK(entityid >= 0);

    // Output search terms for item.
    for (uint64 term : terms) OutputTerm(entityid, term);
  }

  uint32 OutputEntity(Slice id, uint32 count, const Words &words) {
    task::TaskContext ctxt("OutputEntity", id);
    IOBuffer buffer;
    buffer.Write(&count, sizeof(uint32));
    buffer.Write(words.data(), words.size() * sizeof(uint16));
    MutexLock lock(&mu_);
    documents_->Send(new task::Message(id, buffer.data()));
    return next_entityid_++;
  }

  void OutputTerm(uint32 entityid, uint64 term) {
    uint32 b = htonl(term % config_.buckets());
    terms_->Send(new task::Message(
      Slice(&b, sizeof(uint32)),
      term,
      Slice(&entityid, sizeof(uint32))));
  }

  void Collect(Terms *terms, Words *words, Text text, bool important) {
    if (!UTF8::Valid(text.data(), text.size())) return;
    if (important) {
      words->push_back(WORDFP_IMPORTANT);
    } else if (!words->empty()) {
      words->push_back(WORDFP_BREAK);
    }
    std::vector<uint64> tokens;
    config_.tokenizer().TokenFingerprints(text, &tokens);
    for (uint64 token : tokens) {
      if (config_.stopword(token)) {
        num_stopwords_->Increment();
      } else {
        token = config_.map(token);
        terms->insert(token);
        words->push_back(WordFingerprint(token));
      }
    }
  }

  void Collect(Terms *terms, const SearchDictionary::Item *item) {
    if (item == nullptr) return;
    const uint64 *t = item->terms();
    for (int i = 0; i < item->num_terms(); ++i) {
      terms->insert(*t++);
    }
  }

  void Collect(Terms *terms, Words *words, const Document &document) {
    // Add break before document start.
    if (!words->empty()) {
      words->push_back(WORDFP_BREAK);
    }

    // Add text to terms.
    for (const Token &token : document.tokens()) {
      uint64 term = config_.fingerprint(token.word());
      if (config_.stopword(term)) {
        num_stopwords_->Increment();
      } else {
        term = config_.map(term);
        terms->insert(term);
        if (token.brk() >= PARAGRAPH_BREAK) words->push_back(WORDFP_BREAK);
        words->push_back(WordFingerprint(term));
      }
    }

    // Add links to terms.
    Store *store = document.store();
    for (const Span *span : document.spans()) {
      Handle link = span->evoked();
      if (link.IsNil()) continue;
      Text id = store->FrameId(link);
      const SearchDictionary::Item *item = dictionary_.Find(id);
      Collect(terms, item);
    }
  }

 private:
  // Maximum year for date indexing.
  static const int MAX_YEAR = 3000;

  // Search engine configuration.
  SearchConfiguration config_;

  // Search dictionary with search terms for items.
  SearchDictionary dictionary_;

  // Document lexer.
  DocumentTokenizer tokenizer_;
  DocumentLexer lexer_{&tokenizer_};

  // Auxiliary items filter.
  AuxFilter filter_;
  int aux_ = 2;

  // Output channels.
  task::Channel *documents_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Next entity id.
  uint32 next_entityid_ = 0;

  // Fingerprints for years.
  uint64 year_terms_[MAX_YEAR];

  // Document generation.
  bool score_names_ = false;
  bool score_lex_ = false;

  // Symbols.
  Name n_name_{names_, "name"};
  Name n_text_{names_, "text"};
  Name n_item_{names_, "item"};
  Name n_date_{names_, "date"};
  Name n_lex_{names_, "lex"};
  Name n_popularity_{names_, "/w/item/popularity"};
  Name n_fanin_{names_, "/w/item/fanin"};
  Name n_instance_of_{names_, "P31"};

  // Statistics.
  task::Counter *num_items_ = nullptr;
  task::Counter *num_terms_ = nullptr;
  task::Counter *num_words_ = nullptr;
  task::Counter *num_stopwords_ = nullptr;
  task::Counter *num_discarded_ = nullptr;

  // Mutex for serializing access to entity ids.
  Mutex mu_;
};

REGISTER_TASK_PROCESSOR("search-index-mapper", SearchIndexMapper);

// Build search index with item posting lists for each search term.
class SearchIndexBuilder : public task::Processor {
 public:
  ~SearchIndexBuilder() {
    ClearStreams();
  }

  void Start(task::Task *task) override {
    // Read search index configuration.
    Store store;
    SearchConfiguration config;
    config.Load(&store, task->GetInputFile("config"));
    num_buckets_ = config.buckets();

    // Add search configuration to repository.
    JSON::Object params;
    params.Add("normalization", config.normalization());
    repository_.AddBlock("params", params.AsString());

    // Add stopwords to repository.
    std::vector<uint64> stopwords;
    for (uint64 fp : config.stopwords()) {
      stopwords.push_back(fp);
    }
    repository_.AddBlock("stopwords",
                         stopwords.data(),
                         stopwords.size() * sizeof(uint64));

    // Add synonyms to repository.
    std::vector<uint64> synonyms;
    for (auto it : config.synonyms()) {
      synonyms.push_back(it.first);
      synonyms.push_back(it.second);

    }
    repository_.AddBlock("synonyms",
                         synonyms.data(),
                         synonyms.size() * sizeof(uint64));

    // Repository streams.
    document_index_ = AddStream("DocumentIndex");
    document_items_ = AddStream("DocumentItems");
    term_buckets_ = AddStream("TermBuckets");
    term_items_ = AddStream("TermItems");

    // Get input channels.
    documents_ = task->GetSource("documents");
    terms_ = task->GetSource("terms");

    // Statistics.
    num_posting_lists_ = task->GetCounter("posting_lists");
    num_postings_ = task->GetCounter("postings");
    num_documents_ = task->GetCounter("documents");
  }

  void Receive(task::Channel *channel, task::Message *message) override {
    if (channel == documents_) {
      ProcessDocument(message->key(), message->value());
    } else if (channel == terms_) {
      ProcessTerm(message->serial(), message->value());
    }

    delete message;
  }

  void ProcessDocument(Slice docid, Slice data) {
    // Write document index entry.
    document_index_->Write(&document_offset_, sizeof(uint64));

    // Write entry.
    CHECK_LT(docid.size(), 256);
    uint8 idlen = docid.size();
    int32 token_bytes = data.size() - sizeof(uint32);
    uint32 num_tokens = token_bytes / sizeof(uint16);

    document_items_->Write(data.data(), sizeof(uint32));
    document_items_->Write(&idlen, sizeof(uint8));
    document_items_->Write(&num_tokens, sizeof(num_tokens));
    document_items_->Write(docid.data(), idlen);
    document_items_->Write(data.data() + sizeof(uint32), token_bytes);

    // Compute offset of next entry.
    document_offset_ += 2 * sizeof(uint32) + sizeof(uint8) +
                        idlen + token_bytes;
    num_documents_->Increment();
  }

  void ProcessTerm(uint64 term, Slice doc) {
    // Parse input.
    CHECK_EQ(doc.size(), sizeof(uint32));
    int bucket = term % num_buckets_;
    uint32 docid = *reinterpret_cast<const uint32 *>(doc.data());

    // Check for new term.
    if (term != current_term_) {
      FlushTerm();
      current_term_ = term;
    }

    // Update bucket table.
    while (next_bucket_ <= bucket) {
      term_buckets_->Write(&term_offset_, sizeof(uint64));
      next_bucket_++;
    }

    // Add new posting to term posting list.
    posting_list_.push_back(docid);
  }

  void Done(task::Task *task) override {
    // Flush last term.
    if (!posting_list_.empty()) FlushTerm();

    // Flush buckets. We allocate one extra bucket to mark the end of the
    // term items.
    while (next_bucket_ <= num_buckets_) {
      term_buckets_->Write(&term_offset_, sizeof(uint64));
      next_bucket_++;
    }

    // Flush repository streams.
    for (auto *stream : streams_) stream->Flush();

    // Write repository.
    const string &filename = task->GetOutput("repository")->resource()->name();
    CHECK(!filename.empty());
    LOG(INFO) << "Write search dictionary repository to " << filename;
    repository_.Write(filename);
    LOG(INFO) << "Repository done";

    // Clean up.
    ClearStreams();
    posting_list_.clear();
  }

  void FlushTerm() {
    // Sort posting list.
    std::sort(posting_list_.begin(), posting_list_.end());

    // Write term posting list.
    uint32 size = posting_list_.size();
    term_items_->Write(&current_term_, sizeof(uint64));
    term_items_->Write(&size, sizeof(uint32));
    term_items_->Write(posting_list_.data(), size * sizeof(uint32));
    term_offset_ += sizeof(uint64) + (size + 1) * sizeof(uint32);

    posting_list_.clear();
    num_posting_lists_->Increment();
    num_postings_->Increment(size);
  }

 private:
  OutputBuffer *AddStream(const string &name) {
    auto *stream = new OutputBuffer(repository_.AddBlock(name));
    streams_.push_back(stream);
    return stream;
  }

  void ClearStreams() {
    for (auto *stream : streams_) delete stream;
    streams_.clear();
  }

  // Input channels.
  task::Channel *documents_ = nullptr;
  task::Channel *terms_ = nullptr;

  // Seach index repository.
  Repository repository_;

  // Output buffers for entity table.
  OutputBuffer *document_index_ = nullptr;
  OutputBuffer *document_items_ = nullptr;
  OutputBuffer *term_buckets_ = nullptr;
  OutputBuffer *term_items_ = nullptr;
  std::vector<OutputBuffer *> streams_;

  // Number of term buckets.
  int num_buckets_ = 1 << 20;

  // Current bucket and term.
  int next_bucket_ = 0;
  uint64 current_term_ = 0;

  // Entities for current term.
  std::vector<uint32> posting_list_;

  // Offset for next document item.
  uint64 document_offset_ = 0;

  // Offset for next term entry.
  uint64 term_offset_ = 0;

  // Statistics.
  task::Counter *num_posting_lists_ = nullptr;
  task::Counter *num_postings_ = nullptr;
  task::Counter *num_documents_ = nullptr;
};

REGISTER_TASK_PROCESSOR("search-index-builder", SearchIndexBuilder);

}  // namespace nlp
}  // namespace sling

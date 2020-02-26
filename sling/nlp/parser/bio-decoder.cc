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

#include <math.h>
#include <unordered_map>
#include <vector>

#include "sling/base/types.h"
#include "sling/frame/store.h"
#include "sling/frame/object.h"
#include "sling/myelin/builder.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// BIO tag for sequence tagging.
struct BIO {
  enum Tag : int32 {
    OUTSIDE = 0,  // no chunk
    BEGIN = 1,    // begin new chunk
    INSIDE = 2,   // inside chunk started by BEGIN
    END = 3,      // end of chunk started by BEGIN optionally followed by INSIDE
    SINGLE = 4,   // singleton chunk; cannot follow BEGIN/INSIDE
  };

  // Initialize BIO tag.
  BIO() {}
  BIO(Tag tag, int type) : tag(tag), type(type)  {}

  // Initialize BIO tag from index. BIO tags are numbered as follows:
  //  0 = OUTSIDE
  //  1 = BEGIN(0), 2 = INSIDE(0), 3 = END(0), 4 = SINGLE(0)
  //  5 = BEGIN(1), 6 = INSIDE(1), 7 = END(1), 8 = SINGLE(1)
  //  ...
  BIO(int index) {
    if (index != 0) {
      tag = static_cast<Tag>((index - 1) % 4 + 1);
      type = (index - 1) / 4;
    }
  }

  // Return index of tag.
  int index() const {
    if (tag == OUTSIDE) return 0;
    return type * 4 + tag;
  }

  // Check if this tag can follow another tag.
  bool CanFollow(BIO previous) const {
    switch (previous.tag) {
      case OUTSIDE:
      case END:
      case SINGLE:
        return tag == OUTSIDE || tag == BEGIN || tag == SINGLE;

      case BEGIN:
      case INSIDE:
        return previous.type == type && (tag == INSIDE || tag == END);
    }
    return false;
  }

  // Reset tag to default value (OUTSIDE).
  void clear() {
    tag = OUTSIDE;
    type = 0;
  }

  // Compute the number of tags for a given number of types.
  static int tags(int types) { return 1 + 4 * types; }

  Tag tag = OUTSIDE;  // basic tag action
  int type = 0;       // tag type
};

// BIO tagging decoder.
class BIODecoder : public ParserDecoder {
 public:
  ~BIODecoder() override {}

  // Set up BIO decoder.
  void Setup(task::Task *task, Store *commons) override {
    // Get parameters.
    task->Fetch("ff_dims", &ff_dims_);

    // Get entity types.
    FactCatalog catalog;
    catalog.Init(commons);
    Taxonomy *types = catalog.CreateEntityTaxonomy();
    type_map_[Handle::nil()] = 0;
    types_.push_back(Handle::nil());
    for (auto &it : types->typemap()) {
      Handle type = it.first;
      type_map_[type] = types_.size();
      types_.push_back(type);
    }
    delete types;
    num_tags_ = BIO::tags(types_.size());
  }

  // Build model for BIO decoder.
  void Build(Flow *flow, Flow::Variable *encodings, bool learn) override {
    // Get token enmbedding dimensions.
    int token_dim = encodings->elements();

    // Build tagger.
    FlowBuilder f(flow, "tagger");

    // Add token encoding input.
    auto *token = f.Placeholder("token", encodings->type, {1, token_dim}, true);
    token->set_unique();

    // Feed-forward layer(s).
    std::vector<int> layers = ff_dims_;
    layers.push_back(num_tags_);
    auto *scores = f.Name(f.FNN(token, layers, true), "scores");
    scores->set_out();

    // Build tagger gradient.
    if (learn) {
      Gradient(flow, f.func());
      auto *dscores = flow->GradientVar(scores);
      loss_.Build(flow, scores, dscores);
    }

    // Link recurrences.
    flow->Connect({token, encodings});
  }

  // Save model.
  void Save(Flow *flow, Builder *spec) override {
    spec->Set("type", "bio");
    spec->Set("types", Array(spec->store(), types_));
  }

  // Load model.
  void Load(Flow *flow, const Frame &spec) override {
    // Initialize types.
    Array types = spec.Get("types").AsArray();
    if (types.valid()) {
      for (int i = 0; i < types.length(); ++i) {
        types_.push_back(types.get(i));
      }
    }
    num_tags_ = BIO::tags(types_.size());
  }

  // Initialize model.
  void Initialize(const Network &model) override {
    // Get decoder cells and tensors.
    cell_ = model.GetCell("tagger");
    token_ = cell_->GetParameter("tagger/token");
    scores_ = cell_->GetParameter("tagger/scores");

    gcell_ = cell_->Gradient();
    if (gcell_ != nullptr) {
      primal_ = cell_->Primal();
      dtoken_ = token_->Gradient();
      dscores_ = scores_->Gradient();
      loss_.Initialize(model);
    }
  }

  // Decoder predictor.
  class Predictor : public ParserDecoder::Predictor {
   public:
    Predictor(const BIODecoder *decoder)
        : decoder_(decoder),
          forward_(decoder->cell_) {}
    ~Predictor() override {}

    void Switch(Document *document) override {
      document_ = document;
    }

    void Decode(int begin, int end, Channel *encodings) override {
      // Predict tag seqence for document part.
      int length = end - begin;
      BIO prev;
      std::vector<BIO> tagging(length);
      float *logits = forward_.Get<float>(decoder_->scores_);
      for (int t = 0; t < length; ++t) {
        // Compute logits from token encoding.
        forward_.Set(decoder_->token_, encodings, t);
        forward_.Compute();

        // Find tag with highest score that is allowed.
        BIO best;
        float highest = -INFINITY;
        for (int i = 0; i < decoder_->num_tags_; ++i) {
          if (logits[i] > highest) {
            BIO bio(i);
            if (bio.CanFollow(prev)) {
              best = bio;
              highest = logits[i];
            }
          }
        }
        tagging[t] = best;
        prev = best;
      }

      // Decode tag sequence.
      int t = 0;
      while (t < length) {
        if (tagging[t].tag == BIO::SINGLE) {
          // Add single-token mention.
          Handle type = decoder_->types_[tagging[t].type];
          Span *span = document_->AddSpan(begin + t, begin + t + 1);
          Builder builder(document_->store());
          if (!type.IsNil()) builder.AddIsA(type);
          span->Evoke(builder.Create());
        } else if (tagging[t].tag == BIO::BEGIN) {
          // Find end tag.
          int b = t++;
          while (t < length && tagging[t].tag != BIO::END) t++;
          int e = t < length ? t + 1 : length;

          // Add multi-token mention.
          Handle type = decoder_->types_[tagging[b].type];
          Span *span = document_->AddSpan(begin + b, begin + e);
          Builder builder(document_->store());
          if (!type.IsNil()) builder.AddIsA(type);
          span->Evoke(builder.Create());
        }
        t++;
      }
    }

  private:
    const BIODecoder *decoder_;
    Document *document_;
    myelin::Instance forward_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Decoder learner.
  class Learner : public ParserDecoder::Learner {
   public:
    Learner(const BIODecoder *decoder)
        : decoder_(decoder),
          forward_(decoder->cell_),
          backward_(decoder->gcell_),
          dencodings_(decoder->dtoken_) {}
    ~Learner() override {}

    void Switch(Document *document) override {
      // Generate golden tags for document. First, set all the tags to OUTSIDE
      // and then go over all the spans marking these with BEGIN-END,
      // BEGIN-INSIDE-END, or SINGLE tags.
      golden_.resize(document->length());
      for (int i = 0; i < document->length(); i++) {
        golden_[i].clear();
      }
      for (Span *span : document->spans()) {
        // Get type for evoked frame.
        Frame frame = span->Evoked();
        if (!frame.valid()) continue;
        int type = decoder_->GetType(frame);
        if (type == -1) continue;

        // Add tags for span.
        if (span->length() == 1) {
          golden_[span->begin()] = BIO(BIO::SINGLE, type);
        } else {
          for (int t = span->begin(); t < span->end(); ++t) {
            BIO &bio = golden_[t];
            if (t == span->begin()) {
              bio.tag = BIO::BEGIN;
            } else if (t == span->end() - 1) {
              bio.tag = BIO::END;
            } else {
              bio.tag = BIO::INSIDE;
            }
            bio.type = type;
          }
        }
      }
    }

    Channel *Learn(int begin, int end, Channel *encodings) override {
      // Compute forward and backward pass for all tokens in document part.
      int length = end - begin;
      float *logits = forward_.Get<float>(decoder_->scores_);
      float *dlogits = backward_.Get<float>(decoder_->dscores_);
      dencodings_.reset(length);
      for (int t = 0; t < length; ++t) {
        // Compute logits from token encoding.
        forward_.Set(decoder_->token_, encodings, t);
        forward_.Compute();

        // Compute loss.
        int target = golden_[begin + t].index();
        float loss = decoder_->loss_.Compute(logits, target, dlogits);
        loss_sum_ += loss;
        loss_count_++;

        // Backpropagate loss.
        backward_.Set(decoder_->primal_, &forward_);
        backward_.Set(decoder_->dtoken_, &dencodings_, t);
        backward_.Compute();
      }

      return &dencodings_;
    }

    void UpdateLoss(float *loss_sum, int *loss_count) override {
      *loss_sum += loss_sum_;
      *loss_count += loss_count_;
      loss_sum_ = 0.0;
      loss_count_ = 0;
    }

    void CollectGradients(Instances *gradients) override {
      gradients->Add(&backward_);
    }

   private:
    const BIODecoder *decoder_;

    std::vector<BIO> golden_;

    myelin::Instance forward_;
    myelin::Instance backward_;
    myelin::Channel dencodings_;

    float loss_sum_ = 0.0;
    int loss_count_ = 0;
  };

  Learner *CreateLearner() override { return new Learner(this); }

 private:
  // Get type id for frame. Return -1 if type is missing.
  int GetType(const Frame &frame) const {
    Handle type = frame.GetHandle(Handle::isa());
    auto it = type_map_.find(type);
    if (it == type_map_.end()) return -1;
    return it->second;
  }

  // Entity types.
  std::vector<Handle> types_;
  HandleMap<int> type_map_;

  // Number of BIO tags.
  int num_tags_;

  // Feed-forward hidden layer dimensions.
  std::vector<int> ff_dims_;

  // Tagger model.
  myelin::Cell *cell_ = nullptr;
  myelin::Tensor *token_ = nullptr;
  myelin::Tensor *scores_ = nullptr;

  myelin::Cell *gcell_ = nullptr;
  myelin::Tensor *primal_ = nullptr;
  myelin::Tensor *dtoken_ = nullptr;
  myelin::Tensor *dscores_ = nullptr;

  // Loss function.
  myelin::CrossEntropyLoss loss_;
};

REGISTER_PARSER_DECODER("bio", BIODecoder);

}  // namespace nlp
}  // namespace sling


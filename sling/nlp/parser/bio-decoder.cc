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
#include "sling/myelin/crf.h"
#include "sling/myelin/gradient.h"
#include "sling/myelin/learning.h"
#include "sling/nlp/kb/facts.h"
#include "sling/nlp/parser/parser-codec.h"

namespace sling {
namespace nlp {

using namespace sling::myelin;

// BIO tags.
enum BIOTag : int32 {
  OUTSIDE = 0,  // no chunk
  BEGIN = 1,    // begin new chunk
  INSIDE = 2,   // inside chunk started by BEGIN
  END = 3,      // end of chunk started by BEGIN optionally followed by INSIDE
  SINGLE = 4,   // singleton chunk; cannot follow BEGIN/INSIDE
};

// BIO label for sequence tagging.
struct BIOLabel {
  // Initialize BIO label.
  BIOLabel() {}
  BIOLabel(BIOTag tag, int type) : tag(tag), type(type)  {}

  // Initialize BIO label from index. BIO labels are numbered as follows:
  //  0 = OUTSIDE
  //  1 = BEGIN(0), 2 = INSIDE(0), 3 = END(0), 4 = SINGLE(0)
  //  5 = BEGIN(1), 6 = INSIDE(1), 7 = END(1), 8 = SINGLE(1)
  //  ...
  BIOLabel(int index) {
    if (index != 0) {
      tag = static_cast<BIOTag>((index - 1) % 4 + 1);
      type = (index - 1) / 4;
    }
  }

  // Return index of label.
  int index() const {
    if (tag == OUTSIDE) return 0;
    return type * 4 + tag;
  }

  // Check if this label can follow another label.
  bool CanFollow(BIOLabel previous) const {
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

  // Reset label to default value (OUTSIDE).
  void clear() {
    tag = OUTSIDE;
    type = 0;
  }

  // Compute the number of labels for a given number of types.
  static int labels(int types) { return 1 + 4 * types; }

  BIOTag tag = OUTSIDE;  // tag for label
  int type = 0;          // entity type for label
};

// BIO tagging decoder.
class BIODecoder : public ParserDecoder {
 public:
  ~BIODecoder() override {}

  // Set up BIO decoder.
  void Setup(task::Task *task, Store *commons) override {
    // Get parameters.
    task->Fetch("ff_dims", &ff_dims_);
    task->Fetch("crf", &use_crf_);

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
    num_labels_ = BIOLabel::labels(types_.size());
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
    layers.push_back(num_labels_);
    auto *scores = f.Name(f.FNN(token, layers, true), "scores");
    scores->set_out();

    // Build CRF.
    if (use_crf_) {
      crf_.Build(flow, scores, learn);
    }

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
    num_labels_ = BIOLabel::labels(types_.size());
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
      // Predict label seqence for document part.
      int length = end - begin;
      BIOLabel prev;
      std::vector<BIOLabel> labels(length);
      float *logits = forward_.Get<float>(decoder_->scores_);
      for (int t = 0; t < length; ++t) {
        // Compute logits from token encoding.
        forward_.Set(decoder_->token_, encodings, t);
        forward_.Compute();

        // Find label with highest score that is allowed.
        BIOLabel best;
        float highest = -INFINITY;
        for (int i = 0; i < decoder_->num_labels_; ++i) {
          if (logits[i] > highest) {
            BIOLabel label(i);
            if (label.CanFollow(prev)) {
              best = label;
              highest = logits[i];
            }
          }
        }
        labels[t] = best;
        prev = best;
      }

      // Decode label sequence.
      int t = 0;
      while (t < length) {
        if (labels[t].tag == SINGLE) {
          // Add single-token mention.
          Handle type = decoder_->types_[labels[t].type];
          Span *span = document_->AddSpan(begin + t, begin + t + 1);
          Builder builder(document_->store());
          if (!type.IsNil()) builder.AddIsA(type);
          span->Evoke(builder.Create());
        } else if (labels[t].tag == BEGIN) {
          // Find end tag.
          int b = t++;
          while (t < length && labels[t].tag != END) t++;
          int e = t < length ? t + 1 : length;

          // Add multi-token mention.
          Handle type = decoder_->types_[labels[b].type];
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
      // Generate golden labels for document. First, set all the tags to OUTSIDE
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

        // Add labels for span.
        if (span->length() == 1) {
          golden_[span->begin()] = BIOLabel(SINGLE, type);
        } else {
          for (int t = span->begin(); t < span->end(); ++t) {
            BIOLabel &label = golden_[t];
            if (t == span->begin()) {
              label.tag = BEGIN;
            } else if (t == span->end() - 1) {
              label.tag = END;
            } else {
              label.tag = INSIDE;
            }
            label.type = type;
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

    std::vector<BIOLabel> golden_;

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

  // Number of BIO labels.
  int num_labels_;

  // Feed-forward hidden layer dimensions.
  std::vector<int> ff_dims_;

  // CRF decoder.
  bool use_crf_ = false;
  CRF crf_;

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


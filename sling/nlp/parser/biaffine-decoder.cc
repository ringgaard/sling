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
#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#include "sling/base/types.h"
#include "sling/base/bitcast.h"
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

// Float predicate value for mask.
static const float PRED_TRUE = bit_cast<float>(-1);

// Biaffine decoder.
class BiaffineDecoder : public ParserDecoder {
 public:
  ~BiaffineDecoder() override {}

  // Set up biaffine decoder.
  void Setup(task::Task *task, Store *commons) override {
    // Get parameters.
    task->Fetch("max_sentence_length", &max_sentence_length_);
    task->Fetch("max_phrase_length", &max_phrase_length_);
    task->Fetch("ff_dims", &ff_dims_);
    task->Fetch("ff_l2reg", &ff_l2reg_);
    task->Fetch("ff_dropout", &ff_dropout_);
    task->Fetch("ff_bias", &ff_bias_);

    // Get entity types.
    if (task->Get("conll", false)) {
      types_.push_back(commons->Lookup("PER"));
      types_.push_back(commons->Lookup("LOC"));
      types_.push_back(commons->Lookup("ORG"));
      types_.push_back(commons->Lookup("MISC"));
    } else {
      FactCatalog catalog;
      catalog.Init(commons);
      Taxonomy *types = catalog.CreateEntityTaxonomy();
      types_.push_back(Handle::nil());
      for (auto &it : types->typemap()) {
        Handle type = it.first;
        types_.push_back(type);
      }
      delete types;
    }

    for (int i = 0; i < types_.size(); ++i) type_map_[types_[i]] = i;
  }

  // Build model for biaffine decoder.
  void Build(Flow *flow, Flow::Variable *encodings, bool learn) override {
    // Get token enmbedding dimensions.
    int token_dim = encodings->elements();
    auto dt = encodings->type;

    // The number of labels is the number of types plus one additional label
    // for "no span".
    int K = types_.size() + 1;

    // Build biaffine scorer.
    FlowBuilder f(flow, "biaffine");

    // Add token encoding input. The input sentences are capped at a maximum
    // sentence length.
    auto *tokens = f.Placeholder("tokens", dt, {1, token_dim});
    tokens->set_dynamic()->set_unique();
    tokens = f.Resize(tokens, {max_sentence_length_, token_dim});

    // FFNNs for start and end.
    std::vector<int> layers = ff_dims_;
    int D = layers.back();
    auto *start = FFNN(&f, tokens, layers, "S");
    auto *end = FFNN(&f, tokens, layers, "E");
    if (learn && ff_dropout_ != 0.0) {
      // Apply dropout.
      auto *dropout = f.Placeholder("dropout", DT_FLOAT, {1, D}, true);
      dropout->set(Flow::Variable::NOGRADIENT);
      start = f.Mul(start, dropout);
      end = f.Mul(end, dropout);

      // The no-dropout mask is used for testing during training when no dropout
      // should be applied.
      std::vector<float> ones(D, 1.0);
      auto *nodropout = f.Name(f.Const(ones), "nodropout");
      nodropout->set_out();
      flow->Connect({nodropout, dropout});
    }
    f.Name(start, "start");
    f.Name(end, "end");

    // Bilinear mapping to compute scores.
    int L = max_sentence_length_;
    auto *U = f.Parameter("U", dt, {D, K * D});
    f.RandomNormal(U);
    auto *bilin =
        f.Reshape(
          f.MatMul(
            f.Reshape(f.MatMul(start, U), {L * K, D}),
            f.Transpose(end)),
          {L, K, L});
    f.Name(bilin, "bilin");

    // Bias terms for biaffine scorer.
    auto *bs = f.Parameter("bs", dt, {D, K});
    auto *be = f.Parameter("be", dt, {D, K});
    auto *bc = f.Parameter("bc", dt, {1, K, 1});
    f.RandomNormal(bs);
    f.RandomNormal(be);
    auto *start_bias = f.Reshape(f.MatMul(start, bs), {L, K, 1});
    auto *end_bias = f.Reshape(f.Transpose(f.MatMul(end, be)), {1, K, L});
    auto *bias = f.Name(f.Add(f.Add(bc, start_bias), end_bias), "bias");

    auto *scores = f.Add(bilin, bias);
    f.Name(scores, "scores")->set_out();

    // Build loss and loss gradient.
    if (learn) {
      FlowBuilder l(flow, "loss");

      // The logits are the scores from the biaffine mapping.
      auto *logits = l.Placeholder("logits", dt, scores->shape);
      logits->set_ref();

      // The true labels are set to 1.0 in y.
      auto *y = l.Placeholder("y", dt, scores->shape);

      // Mask for selecting the spans that the loss is computed over.
      auto *mask = l.Placeholder("mask", dt, scores->shape.reduced(1));

      // Compute softmax for logits.
      auto *softmax = l.SoftMax(logits, 1);
      auto *dlogits = l.Select(l.ExpandDims(mask, 1), l.Sub(softmax, y));
      l.Name(dlogits, "d_logits");
      dlogits->set_ref();

      // Compute loss (negative log-likelihood). Multiply the softmax with the
      // true labels (0/1) to get the probability of the true label and zero for
      // the false labels, and then sum these over the labels to reduce it to
      // one loss per span. Then compute the negative log-likelihood.
      auto *p = l.Sum(l.Mul(y, softmax), 1);
      auto *nll = l.Neg(l.Log(p));
      auto *loss = l.Sum(l.Select(mask, nll));
      l.Name(loss, "loss");
      flow->Connect({scores, logits});

      // Build gradient for biaffine scorer.
      Gradient(flow, f.func());
      auto *dscores = flow->GradientVar(scores);
      flow->Connect({dlogits, dscores});
    }

    // Build labeler for finding maximum score and best label for each span.
    FlowBuilder l(flow, "labeler");
    auto *ll = l.Placeholder("logits", dt, scores->shape);
    ll->set_ref();
    Flow::Variable *max;
    l.Name(l.ArgMax(ll, 1, &max), "label");
    l.Name(max, "score");
    flow->Connect({scores, ll});
  }

  // Build FFNN for input transformation.
  Flow::Variable *FFNN(FlowBuilder *f,
                       Flow::Variable *input,
                       std::vector<int> &layers,
                       const string &prefix) {
    Flow::Variable *v = input;
    for (int l = 0; l < layers.size(); ++l) {
      int height = v->dim(1);
      int width = layers[l];

      string idx = std::to_string(l);
      auto *W = f->Parameter(prefix + "W" + idx, v->type, {height, width});
      f->RandomNormal(W);
      if (ff_l2reg_ != 0.0) W->SetAttr("l2reg", ff_l2reg_);
      v = f->MatMul(v, W);

      if (ff_bias_) {
        auto *b = f->Parameter(prefix + "b" + idx, v->type, {width});
        v = f->Add(v, b);
      }

      if (l != layers.size() - 1) v = f->Relu(v);
    }
    return v;
  }

  // Save model.
  void Save(Flow *flow, Builder *spec) override {
    spec->Set("type", "biaffine");
    spec->Set("types", Array(spec->store(), types_));
    spec->Set("max_sentence_length", max_sentence_length_);
    spec->Set("max_phrase_length", max_phrase_length_);
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

    max_sentence_length_ = spec.GetInt("max_sentence_length");
    max_phrase_length_ = spec.GetInt("max_phrase_length");
  }

  // Initialize model.
  void Initialize(const Network &model) override {
    biaffine_ = model.GetCell("biaffine");
    tokens_ = biaffine_->GetParameter("biaffine/tokens");
    scores_ = biaffine_->GetParameter("biaffine/scores");

    gbiaffine_ = biaffine_->Gradient();
    if (gbiaffine_ != nullptr) {
      dropout_ = model.LookupParameter("biaffine/dropout");
      nodropout_ = model.LookupParameter("biaffine/nodropout");

      primal_ = biaffine_->Primal();
      dtokens_ = tokens_->Gradient();
      dscores_ = scores_->Gradient();

      loss_ = model.GetCell("loss");
      loss_logits_ = loss_->GetParameter("loss/logits");
      loss_y_ = loss_->GetParameter("loss/y");
      loss_mask_ = loss_->GetParameter("loss/mask");
      loss_dlogits_ = loss_->GetParameter("loss/d_logits");
      loss_loss_ = loss_->GetParameter("loss/loss");
    }

    labeler_ = model.GetCell("labeler");
    labeler_logits_ = labeler_->GetParameter("labeler/logits");
    labeler_score_ = labeler_->GetParameter("labeler/score");
    labeler_label_ = labeler_->GetParameter("labeler/label");
  }

  // Biaffine decoder predictor.
  class Predictor : public ParserDecoder::Predictor {
   public:
    Predictor(const BiaffineDecoder *decoder)
        : decoder_(decoder),
          biaffine_(decoder->biaffine_),
          labeler_(decoder->labeler_) {}

    void Switch(Document *document) override {
      document_ = document;
    }

    void Decode(int begin, int end, Channel *encodings) override {
      // Crop sentence if it is too long.
      int max_sent = decoder_->max_sentence_length_;
      int max_phrase = decoder_->max_phrase_length_;
      int length = end - begin;
      if (length > max_sent) {
        end = begin + max_sent;
        length = max_sent;
      }

      // Set pass-through dropout mask.
      if (decoder_->dropout_) {
        char *ones = decoder_->nodropout_->data();
        biaffine_.SetReference(decoder_->dropout_, ones);
      }

      // Compute scores for all spans, i.e. [begin;end] intervals.
      biaffine_.SetChannel(decoder_->tokens_, encodings);
      biaffine_.Compute();

      // Find the best label for each span.
      float *logits = biaffine_.Get<float>(decoder_->scores_);
      labeler_.SetReference(decoder_->labeler_logits_, logits);
      labeler_.Compute();
      int *label = labeler_.Get<int>(decoder_->labeler_label_);
      float *score = labeler_.Get<float>(decoder_->labeler_score_);

      // Create list of all predicted spans.
      candidates_.clear();
      for (int b = 0; b < length; ++b) {
        int limit = b + max_phrase;
        if (limit > length) limit = length;
        for (int e = b; e < limit; ++e) {
          // Ignore if prediction is "no span".
          if (label[e] == 0) continue;
          candidates_.emplace_back(b, e, label[e], score[e]);
        }
        label += max_sent;
        score += max_sent;
      }

      // Sort candidate list in score order.
      std::sort(candidates_.begin(), candidates_.end(),
        [](const Candidate &a, const Candidate &b) {
          return a.score > b.score;
        });

      // Add all spans that do not conflict with higher scoring spans.
      for (Candidate &c : candidates_) {
        int b = begin + c.begin;
        int e = begin + c.end + 1;
        Handle type = decoder_->types_[c.label - 1];
        Span *span = document_->AddSpan(b, e);
        if (span != nullptr) {
          Builder builder(document_->store());
          if (!type.IsNil()) builder.AddIsA(type);
          span->Evoke(builder.Create());
        }
      }
    }

  private:
    // Span candiate.
    struct Candidate {
      Candidate(int b, int e, int l, float s)
          : begin(b), end(e), label(l), score(s) {}
      int begin;
      int end;
      int label;
      float score;
    };

    const BiaffineDecoder *decoder_;
    Document *document_;

    Instance biaffine_;
    Instance labeler_;
    std::vector<Candidate> candidates_;
  };

  Predictor *CreatePredictor() override { return new Predictor(this); }

  // Biaffine decoder learner.
  class Learner : public ParserDecoder::Learner {
   public:
    Learner(const BiaffineDecoder *decoder)
        : decoder_(decoder),
          biaffine_(decoder->biaffine_),
          gbiaffine_(decoder->gbiaffine_),
          loss_(decoder_->loss_),
          dencodings_(decoder->tokens_),
          dropout_(decoder->dropout_) {
      mask_ = loss_.Get<float>(decoder->loss_mask_);
      y_ = loss_.Get<float>(decoder->loss_y_);
      if (decoder->dropout_) {
        dropout_.resize(1);
      }
    }

    void NextBatch() override {
      // Set up dropout mask.
      if (decoder_->dropout_) {
        float *mask = reinterpret_cast<float *>(dropout_.at(0));
        float rate = decoder_->ff_dropout_;
        float scaler = 1.0 / (1.0 - rate);
        int size = dropout_.format()->elements();
        for (int i = 0; i < size; ++i) {
          mask[i] = Random() < rate ? 0.0 : scaler;
        }
      }
    }

    void Switch(Document *document) override {
      document_ = document;
    }

    Channel *Learn(int begin, int end, Channel *encodings) override {
      // Crop sentence if it is too long.
      int max_sent = decoder_->max_sentence_length_;
      int max_phrase = decoder_->max_phrase_length_;
      int length = end - begin;
      if (length > max_sent) {
        end = begin + max_sent;
        length = max_sent;
      }

      // Compute scores for all spans, i.e. [begin;end] intervals.
      biaffine_.SetChannel(decoder_->tokens_, encodings);
      if (decoder_->dropout_) {
        biaffine_.Set(decoder_->dropout_, &dropout_, 0);
      }
      biaffine_.Compute();

      // Set up mask for spans that are considered for the loss and gradient
      // computation. The begin and end must be inside the sentence, i.e.
      // begin <= end < sentence_length, and only spans up the the maximum
      // span length are used, i.e. end <= begin + max_span_length.
      loss_.Clear();
      for (int b = 0; b < length; ++b) {
        int limit = b + max_phrase;
        if (limit > length) limit = length;
        loss_count_ += limit - b + 1;
        for (int e = b; e < limit; ++e) {
          mask_[b * max_sent + e] = PRED_TRUE;
        }
      }

      // Set up the golden labels for the loss computation. This is 3D tensor
      // with shape [begin, label, end]. Token intervals without a span use
      // label 0 to indicate no span.
      int num_labels = decoder_->types_.size() + 1;
      float *y = y_;
      for (int b = 0; b < length; ++b) {
        // Set all spans to the no span label which is the first label.
        for (int e = 0; e < length; ++e) y[e] = 1.0;

        // Find all spans starting at token.
        int left  = begin + b;
        Span *span = document_->GetSpanAt(left);
        while (span != nullptr) {
          int e = span->end() - begin - 1;
          if (span->begin() == left && e < length) {
            // Get span type.
            int type = decoder_->GetType(span);
            if (type != -1) {
              // Add span to golden labels.
              y[e] = 0.0;
              y[(type + 1) * max_sent + e] = 1.0;
            }
          }
          span = span->parent();
        }

        y += max_sent * num_labels;
      }

      // Compute loss.
      float *logits = biaffine_.Get<float>(decoder_->scores_);
      float *dlogits = gbiaffine_.Get<float>(decoder_->dscores_);
      loss_.SetReference(decoder_->loss_logits_, logits);
      loss_.SetReference(decoder_->loss_dlogits_, dlogits);
      loss_.Compute();
      loss_sum_ += *loss_.Get<float>(decoder_->loss_loss_);

      // Backpropagate gradients.
      dencodings_.reset(encodings->size());
      gbiaffine_.Set(decoder_->primal_, &biaffine_);
      gbiaffine_.SetChannel(decoder_->dtokens_, &dencodings_);
      gbiaffine_.Compute();

      return &dencodings_;
    }

    void UpdateLoss(float *loss_sum, int *loss_count) override {
      *loss_sum += loss_sum_;
      *loss_count += loss_count_;
      loss_sum_ = 0.0;
      loss_count_ = 0;
    }

    void CollectGradients(Instances *gradients) override {
      gradients->Add(&gbiaffine_);
    }

   private:
    // Generate uniform random number between 0 and 1.
    float Random() { return prob_(prng_); }

    const BiaffineDecoder *decoder_;
    Document *document_;

    Instance biaffine_;
    Instance gbiaffine_;
    Instance loss_;
    Channel dencodings_;

    float *mask_;
    float *y_;

    float loss_sum_ = 0.0;
    int loss_count_ = 0;

    // Dropout mask.
    Channel dropout_;
    std::mt19937_64 prng_;
    std::uniform_real_distribution<float> prob_{0.0, 1.0};
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

  // Get type id for span. Return -1 if type is missing.
  int GetType(const Span *span) const {
    Frame frame = span->Evoked();
    if (!frame.valid()) return -1;
    return GetType(frame);
  }

  // Entity types.
  std::vector<Handle> types_;
  HandleMap<int> type_map_;

  // Maximum sentence length.
  int max_sentence_length_ = 128;

  // Maximum phrase length.
  int max_phrase_length_ = 15;

  // Feed-forward hidden layer hyperparameters.
  std::vector<int> ff_dims_;
  float ff_l2reg_ = 0.0;
  float ff_dropout_ = 0.0;
  bool ff_bias_ = false;

  // Biaffine model.
  Cell *biaffine_ = nullptr;
  Tensor *tokens_ = nullptr;
  Tensor *scores_ = nullptr;

  Tensor *dropout_ = nullptr;
  Tensor *nodropout_ = nullptr;

  Cell *gbiaffine_ = nullptr;
  Tensor *primal_ = nullptr;
  Tensor *dtokens_ = nullptr;
  Tensor *dscores_ = nullptr;

  Cell *loss_ = nullptr;
  Tensor *loss_logits_ = nullptr;
  Tensor *loss_y_ = nullptr;
  Tensor *loss_mask_ = nullptr;
  Tensor *loss_dlogits_ = nullptr;
  Tensor *loss_loss_ = nullptr;

  Cell *labeler_ = nullptr;
  Tensor *labeler_logits_ = nullptr;
  Tensor *labeler_score_ = nullptr;
  Tensor *labeler_label_ = nullptr;
};

REGISTER_PARSER_DECODER("biaffine", BiaffineDecoder);

}  // namespace nlp
}  // namespace sling


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

#include "sling/nlp/parser/parser.h"

#include "sling/base/logging.h"
#include "sling/frame/serialization.h"

namespace sling {
namespace nlp {

using namespace myelin;

Parser::~Parser() {
  delete encoder_;
  delete decoder_;
}

void Parser::Load(Store *store, const string &filename) {
  // Load and compile parser flow.
  Flow flow;
  CHECK(flow.Load(filename));
  compiler_.Compile(&flow, &model_);

  // Load commons store from parser model.
  Flow::Blob *commons = flow.DataBlock("commons");
  if (commons != nullptr) {
    StringDecoder decoder(store, commons->data, commons->size);
    decoder.DecodeAll();
  }

  // Get parser specification.
  Flow::Blob *spec_data = flow.DataBlock("parser");
  CHECK(spec_data != nullptr) << "No parser in model: " << filename;
  StringDecoder spec_decoder(store, spec_data->data, spec_data->size);
  Frame spec = spec_decoder.Decode().AsFrame();
  CHECK(spec.valid());
  skip_mask_ = spec.GetInt("skip_mask", skip_mask_);

  // Get parser model hyperparameters.
  Frame hparams = spec.GetFrame("hparams");
  if (hparams.valid()) {
    for (const Slot &p : hparams) {
      string name = String(store, p.name).value();
      string value = String(store, p.value).value();
      hparams_.emplace_back(name, value);
    }
  }

  // Initialize encoder.
  Frame encoder_spec = spec.GetFrame("encoder");
  CHECK(encoder_spec.valid());
  string encoder_type = encoder_spec.GetString("type");
  encoder_ = ParserEncoder::Create(encoder_type);
  encoder_->Load(&flow, encoder_spec);
  encoder_->Initialize(model_);

  // Initialize decoder.
  Frame decoder_spec = spec.GetFrame("decoder");
  CHECK(decoder_spec.valid());
  string decoder_type = decoder_spec.GetString("type");
  decoder_ = ParserDecoder::Create(decoder_type);
  decoder_->Load(&flow, decoder_spec);
  decoder_->Initialize(model_);
}

void Parser::Parse(Document *document) const {
  // Create encoder and decoder predictors.
  ParserEncoder::Predictor *encoder = encoder_->CreatePredictor();
  ParserDecoder::Predictor *decoder = decoder_->CreatePredictor();

  // Parse each sentence of the document.
  decoder->Switch(document);
  for (SentenceIterator s(document, skip_mask_); s.more(); s.next()) {
    // Encode tokens in the sentence using encoder.
    Channel *encodings = encoder->Encode(*document, s.begin(), s.end());

    // Decode sentence using decoder.
    decoder->Decode(s.begin(), s.end(), encodings);
  }

  delete encoder;
  delete decoder;
}

}  // namespace nlp
}  // namespace sling


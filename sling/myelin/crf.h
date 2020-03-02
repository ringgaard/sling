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

#ifndef SLING_MYELIN_CRF_H_
#define SLING_MYELIN_CRF_H_

#include "sling/myelin/compute.h"
#include "sling/myelin/flow.h"

namespace sling {
namespace myelin {

// Conditional Random Field (CRF) cell.
class CRF {
 public:
  CRF(const string &name = "crf") : name_(name) {}

  // Build flow for CRF. Returns gradient output in learning mode.
  Flow::Variable *Build(Flow *flow, Flow::Variable *input, bool learn);

  // Initialize CRF.
  void Initialize(const Network &net);

  class Predictor {
  };

  class Learner {
  };

 private:
  string name_;                     // CRF cell name
};

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_CRF_H_


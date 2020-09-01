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

#include "sling/myelin/kernel/library.h"

#include <mutex>

#include "sling/myelin/compute.h"

namespace sling {
namespace myelin {

static std::once_flag gradients_initialized;

// Register standard kernels.
void RegisterStandardLibrary(Library *library, int flags) {
  RegisterArithmeticTransforms(library);
  RegisterGenericLibrary(library);
  RegisterConcatKernels(library);
  RegisterGatherKernels(library);
  RegisterReduceKernels(library);
  RegisterTransposeKernels(library);
  RegisterArrayKernels(library);
  RegisterArgMax(library);
  RegisterSIMDMatMulLibrary(library);
  RegisterArithmeticLibrary(library);
  if ((flags & LIBRARY_NOPRECOMPUTE) == 0) {
    RegisterPrecomputeLibrary(library);
  }
  RegisterGenericTransforms(library);
  RegisterTransposeTransforms(library);

  std::call_once(gradients_initialized, RegisterStandardGradients);
}

}  // namespace myelin
}  // namespace sling


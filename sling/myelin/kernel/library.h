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

#ifndef SLING_MYELIN_KERNEL_LIBRARY_H_
#define SLING_MYELIN_KERNEL_LIBRARY_H_

#include "sling/myelin/compute.h"
#include "sling/myelin/express.h"

namespace sling {
namespace myelin {

// Library registration flags.
enum LibraryOptions {
  LIBRARY_NOPRECOMPUTE = 1,
};

// argmax.cc
void RegisterArgMax(Library *library);

// arithmetic.cc
void RegisterArithmeticLibrary(Library *library);
void RegisterArithmeticTransforms(Library *library);
void InitExpression(const Step *step, Express *expr);

// array.cc
void RegisterArrayKernels(Library *library);

// concat.cc
void RegisterConcatKernels(Library *library);

// gather.cc
void RegisterGatherKernels(Library *library);

// generic.cc
void RegisterGenericTransforms(Library *library);
void RegisterGenericLibrary(Library *library);

// gradients.cc
void RegisterStandardGradients();

// precompute.cc
void RegisterPrecomputeLibrary(Library *library);

// reduce.cc
void RegisterReduceKernels(Library *library);

// transpose.cc
void RegisterTransposeTransforms(Library *library);
void RegisterTransposeKernels(Library *library);

// simd-matmul.cc
void RegisterSIMDMatMulLibrary(Library *library);

// Register standard kernel library.
void RegisterStandardLibrary(Library *library, int flags = 0);

}  // namespace myelin
}  // namespace sling

#endif  // SLING_MYELIN_KERNEL_LIBRARY_H_


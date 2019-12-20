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

#include <math.h>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/base/types.h"

using namespace sling;
//using namespace sling::myelin;

#include <random>

/*

def gram_schmidt(A):
  """Orthogonalize a set of vectors stored as the columns of matrix A."""
  # Get the number of vectors.
  n = A.shape[1]
  for j in range(n):
      # To orthogonalize the vector in column j with respect to the
      # previous vectors, subtract from it its projection onto
      # each of the previous vectors.
      for k in range(j):
          A[:, j] -= np.dot(A[:, k], A[:, j]) * A[:, k]
      A[:, j] = A[:, j] / np.linalg.norm(A[:, j])
  return A

*/

// Orthogonalize a set of vectors stored as the columns of matrix A (m x n)
// using the Gram-Schmidt process.
void orthogonalize(float *A, int m, int n) {
  // Orthogonalize one column vector at the time.
  float *aj, *ak;
  for (int j = 0; j < n; ++j) {
    // To orthogonalize the vector in column j with respect to the previous 
    // vectors, subtract from it its projection onto each of the previous 
    // vectors.
    for (int k = 0; k < j; ++k) {
      // Compute dot product r = A_k * A_j.
      float r = 0.0;
      ak = A + k;
      aj = A + j;
      for (int i = 0; i < m; ++i, ak += n, aj += n) r += *ak * *aj;

      // Update A_j -= r * A_k.
      ak = A + k;
      aj = A + j;
      for (int i = 0; i < m; ++i, ak += n, aj += n) *aj -= r * *ak;
    }
    
    // Normalize A_j.
    aj = A + j;
    float sum = 0.0;
    for (int i = 0; i < m; ++i, aj += n) sum += *aj * *aj;
    float scaler = 1.0/ sqrt(sum);
    aj = A + j;
    for (int i = 0; i < m; ++i, aj += n) *aj *= scaler;
  }
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);
}


#include <math.h>
#include <iostream>
#include <string>

#include "sling/base/init.h"
#include "sling/base/flags.h"
#include "sling/base/logging.h"
#include "sling/myelin/compiler.h"
#include "sling/myelin/compute.h"

DEFINE_string(model, "local/tdozat/tdozat-step26.flow", "");
DEFINE_bool(baseline, false, "");
DEFINE_bool(dump_data, false, "");

using namespace sling;
using namespace sling::myelin;

// Baseline implementation of float matrix multiplication.
void BaselineMatMatMul(const TensorData &A, const TensorData &B,
                       TensorData *C) {
  LOG(INFO) << "MatMul "
            << A.shape().ToString() << " "
            << B.shape().ToString() << " "
            << C->shape().ToString()<< " "
            << C->format().producer()->name();
  for (int i = 0; i < A.dim(0); ++i) {
    for (int j = 0; j < B.dim(1); ++j) {
      float sum = 0.0;
      for (int k = 0; k < A.dim(1); ++k) {
        sum += A.at<float>(i, k) * B.at<float>(k, j);
      }
      C->at<float>(i, j) = sum;
    }
  }
}

// Baseline implementation of float matrix multiplication. with B^T.
void BaselineMatMatMulBt(const TensorData &A, const TensorData &B,
                         TensorData *C) {
  for (int i = 0; i < A.dim(0); ++i) {
    for (int j = 0; j < B.dim(0); ++j) {
      float sum = 0.0;
      for (int k = 0; k < A.dim(1); ++k) {
        sum += A.at<float>(i, k) * B.at<float>(j, k);
      }
      C->at<float>(i, j) = sum;
    }
  }
}

bool CheckTranspose(Step *step, bool ta, bool tb) {
  if (step->GetAttr("transpose_a", false) != ta) return false;
  if (step->GetAttr("transpose_b", false) != tb) return false;
  if (step->GetAttr("transpose_c", false)) return false;
  return true;
}

int main(int argc, char *argv[]) {
  InitProgram(&argc, &argv);

  // Load model.
  Flow flow;
  flow.set_batch_size(1);
  CHECK(flow.Load(FLAGS_model));

  // Compile model.
  Compiler compiler;
  if (FLAGS_baseline) {
    compiler.library()->Register(
      "MatMul", "BaselineMatMatMul", BaselineMatMatMul)
      .Input(0, DT_FLOAT, 2)
      .Input(1, DT_FLOAT, 2)
      .Output(0, DT_FLOAT, 2)
      .Select([](Step *step) { return CheckTranspose(step, false, false); });
    compiler.library()->Register(
      "MatMul", "BaselineMatMatMulBt", BaselineMatMatMulBt)
      .Input(0, DT_FLOAT, 2)
      .Input(1, DT_FLOAT, 2)
      .Output(0, DT_FLOAT, 2)
      .Select([](Step *step) { return CheckTranspose(step, false, true); });
  }

  Network network;
  compiler.Compile(&flow, &network);

  // Test model.
  for (int cell_id = 2; cell_id < 19; ++cell_id) {
    LOG(INFO) <<"biaffine test mlp/biaffine: " << cell_id;
    string cell_number = std::to_string(cell_id);
    string cell_name = "mlps_" + cell_number;
    string input_name = "recur_nob_" + cell_number + ":0";
    string output_name = "Arcs_" + cell_number + "/Bilinear/MatMul_1:0";

    // Create the MLP/biaffine instance.
    Cell *mlps = network.GetCell(cell_name);
    Instance data(mlps);

    // Fill input.
    TensorData input = data[input_name];
    for (int i = 0; i < input.format().elements(); ++i) {
      input.nth<float>(i) = 0.37;
    }

    // Compute cell.
    data.Compute();
    if (FLAGS_dump_data) std::cout << data.ToString();

    // Check output.
    const float expected = -2.987;
    const float epsilon = 1e-2;
    TensorData output = data[output_name];
    for (int i = 0; i < output.format().elements(); ++i) {
      float v = output.nth<float>(i);
      CHECK(fabs(v - expected) < epsilon);
    }
  }

  return 0;
}

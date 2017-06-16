#include "myelin/tests/compare-kernels.h"

#include <math.h>
#include <random>

#include "base/flags.h"
#include "base/logging.h"
#include "file/file.h"
#include "myelin/compute.h"

DEFINE_bool(debug_base, false, "Debug base kernel");
DEFINE_bool(debug_test, false, "Debug test kernel");
DEFINE_bool(log_input_tensors, false, "Dump input tensors");
DEFINE_bool(log_output_tensors, false, "Dump output tensors");
DEFINE_string(test_code_output, "", "File for generated test code");
DEFINE_string(base_code_output, "", "File for generated base code");
DEFINE_bool(intrand, false, "Use integers for random number generation");
DEFINE_int32(minint, -64, "Minimum integer for random number generation");
DEFINE_int32(maxint, 64, "Maximum integer for random number generation");

namespace sling {
namespace myelin {

static const float kEpsilon = 1e-6;
static const float kMinimum = 1e-3;

static const int redzone_size = 128;
static char redzone[redzone_size] =
  "<- START *REDZONE* Don't overwrite this region of memory!"
  "Memory checked on deallocation. Achtung! *REDZONE* END ->";

// Debug runtime with memory checking.
class DebugRuntime : public Runtime {
 public:
  void AllocateInstance(Instance *instance) override {
    // Allocate space for redzone on both sides of instance buffer.
    int alignment = instance->alignment();
    CHECK(redzone_size % alignment == 0);
    int size = instance->size() + 2 * redzone_size;
    char *data;
    int rc = posix_memalign(reinterpret_cast<void **>(&data), alignment, size);
    CHECK_EQ(rc, 0) << "Cannot allocate memory, size: " << size
                  << " alignment: " << alignment;

    // Initialize redzones.
    memcpy(data, redzone, redzone_size);
    memset(data + redzone_size, 0, instance->size());
    memcpy(data + redzone_size + instance->size(), redzone, redzone_size);

    // Return data buffer between the redzones.
    instance->set_data(data + redzone_size);
  }

  void FreeInstance(Instance *instance) override {
    // Check redzones.
    char *front = instance->data() - redzone_size;
    char *back = instance->data() + instance->size();
    CHECK(memcmp(front, redzone, redzone_size) == 0) << "Data corruption";
    CHECK(memcmp(back, redzone, redzone_size) == 0) << "Data corruption";
    free(front);
  }

  void ClearInstance(Instance *instance) override {
    memset(instance->data(), 0, instance->size());
  }

  bool SupportsAsync() override { return false; }
  TaskFunc StartTaskFunc() override { return StartTask; }
  TaskFunc WaitTaskFunc() override { return WaitTask; }

  static void StartTask(Task *task) { task->func(task->arg); }
  static void WaitTask(Task *task) {}
};

static DebugRuntime debug_runtime;

class FloatPRNG {
 public:
  FloatPRNG() : unit_(0.0, 1.0) {}

  float Random(float scale, float bias) {
    float val = unit_(prng_) * scale + bias;
    if (FLAGS_intrand) {
      val = round(val);
    } else if (val >  0.0 && val < kMinimum) {
      val = 0;
    } else if (val <  0.0 && val > -kMinimum) {
      val = 0;
    }
    return val;
  }

 private:
  std::mt19937 prng_;
  std::uniform_real_distribution<float> unit_;
};

struct KernelCompiler {
  bool Compile(const Library &library,
               const Flow &flow,
               const string &op,
               const string &kernel,
               const string binfile,
               bool debug) {
    if (!library.Singleton(op, kernel, &singleton)) {
      LOG(ERROR) << "Unknown kernel: " << kernel;
      return false;
    }
    network.set_runtime(&debug_runtime);
    network.set_parameter_element_order(ANY_ORDER);
    if (debug) network.set_debug(true);
    if (!network.Compile(flow, singleton)) {
      LOG(ERROR) << "Error compiling kernel: " << kernel;
      return false;
    }
    func = network.GetCell("benchmark");
    if (func == nullptr) {
      LOG(ERROR) << "Benchmark function missing";
      return false;
    }
    if (!binfile.empty()) func->WriteCodeToFile(binfile);

    return true;
  }

  Library singleton;
  Network network;
  Cell *func = nullptr;
};

KernelComparator::KernelComparator(
  const Library &library,
  const string &operation_name,
  const string &test_kernel_name,
  const string &base_kernel_name)
    : library_(library),
      test_kernel_name_(test_kernel_name),
      base_kernel_name_(base_kernel_name) {
  Flow::Function *func = flow_.AddFunction("benchmark");
  op_ = flow_.AddOperation("test", operation_name);
  func->AddOperation(op_);
}

void FltKernelComparator::AddInput(const string &name,
                                   const Shape &shape,
                                   float low, float high) {
  Flow::Variable *input = flow_.AddVariable(name, DT_FLOAT, shape);
  op_->AddInput(input);
  inputs_.push_back(input);
  low_.push_back(low);
  high_.push_back(high);
}

void FltKernelComparator::AddOutput(const string &name,
                                    const Shape &shape,
                                    float tolerance) {
  Flow::Variable *output = flow_.AddVariable(name, DT_FLOAT, shape);
  op_->AddOutput(output);
  outputs_.push_back(output);
  tolerance_.push_back(tolerance);
}

bool FltKernelComparator::Check(int iterations) {
  VLOG(3) << "Compare " << op_->type << " kernel " << test_kernel_name_
          << " against " << base_kernel_name_;

  // Compile computation for base kernel.
  KernelCompiler base;
  if (!base.Compile(library_, flow_, op_->type, base_kernel_name_,
      FLAGS_base_code_output, FLAGS_debug_base)) {
    return false;
  }

  // Compile computation for base kernel.
  KernelCompiler test;
  if (!test.Compile(library_, flow_, op_->type, test_kernel_name_,
      FLAGS_test_code_output, FLAGS_debug_test)) {
    return false;
  }

  // Compare kernels on random sampled inputs.
  FloatPRNG prng;
  int num_errors = 0;
  int num_inexact = 0;
  float max_error = 0.0;
  double error = 0.0;
  int num_elements = 0;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    // Create data instances for base and test.
    Instance base_data(base.func);
    Instance test_data(test.func);

    // Fill inputs with random data.
    for (int i = 0; i < inputs_.size(); ++i) {
      Flow::Variable *var = inputs_[i];
      Tensor *b = base.func->GetParameter(var->name);
      Tensor *t = test.func->GetParameter(var->name);
      float bias = low_[i];
      float scale = high_[i] - low_[i];
      if (var->rank() == 1) {
        for (int r = 0; r < var->dim(0); ++r) {
          float val = prng.Random(scale, bias);
          *base_data.Get<float>(b, r) = val;
          *test_data.Get<float>(t, r) = val;
          if (FLAGS_log_input_tensors) {
            LOG(INFO) << var->name << "[" << r << "]=" << val;
          }
        }
      } else if (var->rank() == 2) {
        for (int r = 0; r < var->dim(0); ++r) {
          for (int c = 0; c < var->dim(1); ++c) {
            float val = prng.Random(scale, bias);
            *base_data.Get<float>(b, r, c) = val;
            *test_data.Get<float>(t, r, c) = val;
            if (FLAGS_log_input_tensors) {
              LOG(INFO) << var->name << "[" << r << "," << c << "]=" << val;
            }
          }
        }
      } else {
        LOG(ERROR) << var->rank() << "D tensor not supported";
        return false;
      }
    }

    // Run base and test computation.
    base_data.Compute();
    test_data.Compute();

    // Compare output from base and test.
    for (int i = 0; i < outputs_.size(); ++i) {
      Flow::Variable *var = outputs_[i];
      Tensor *b = base.func->GetParameter(var->name);
      Tensor *t = test.func->GetParameter(var->name);
      if (var->rank() == 1) {
        num_elements += var->dim(0);
        for (int r = 0; r < var->dim(0); ++r) {
          float base_result = *base_data.Get<float>(b, r);
          float test_result = *test_data.Get<float>(t, r);
          float delta = fabs(test_result - base_result);
          if (delta != 0.0) {
              float e = 0.0;
              if (fabs(base_result) > kEpsilon) e = delta / fabs(base_result);
              error += e;
              VLOG(9) << "Base and test difference for "
                      << var->name << "[" << r << "] "
                      << base_result << " vs. " << test_result
                      << " (delta " << delta << ", error " << e << ")";
            if (e > tolerance_[i]) {
              num_errors++;
            } else {
              num_inexact++;
            }
            if (e > max_error) max_error = e;
          }
          if (FLAGS_log_output_tensors) {
            LOG(INFO) << var->name << "[" << r << "]=" << test_result;
          }
        }
      } else if (var->rank() == 2) {
        num_elements += var->elements();
        for (int r = 0; r < var->dim(0); ++r) {
          for (int c = 0; c < var->dim(1); ++c) {
            float base_result = *base_data.Get<float>(b, r, c);
            float test_result = *test_data.Get<float>(t, r, c);
            float delta = fabs(test_result - base_result);
            if (delta != 0.0) {
              float e = 0.0;
              if (fabs(base_result) > kEpsilon) e = delta / fabs(base_result);
              error += e;
              VLOG(9) << "Base and test difference for "
                      << var->name << "[" << r << "," << c << "] "
                      << base_result << " vs. " << test_result
                      << " (delta " << delta << ", error " << e << ")";
              if (e > tolerance_[i]) {
                num_errors++;
              } else {
                num_inexact++;
              }
              if (e > max_error) max_error = e;
            }
            if (FLAGS_log_output_tensors) {
              LOG(INFO) << var->name << "[" << r << "," << c << "]="
                        << test_result;
            }
          }
        }
      } else {
        LOG(ERROR) << var->rank() << "D tensor not supported";
        return false;
      }
    }
  }

  if (max_error != 0 || error != 0.0 || num_inexact != 0) {
    double avg_error = error / num_elements;
    LOG(WARNING) << num_inexact << "/" << num_elements
                 << " inexact values in  comparison between "
                 << test_kernel_name_ << " and " << base_kernel_name_
                 << " (max. error: " << max_error << ", "
                 << "avg. error: " << avg_error << ")";
  }

  if (num_errors != 0) {
    LOG(ERROR) << num_errors << "/" << num_elements
              << " errors in comparison between " << test_kernel_name_
              << " and " << base_kernel_name_;
  }

  return num_errors == 0;
}

void IntKernelComparator::AddInput(const string &name,
                                   const Shape &shape,
                                   Type type) {
  Flow::Variable *input = flow_.AddVariable(name, type, shape);
  op_->AddInput(input);
  inputs_.push_back(input);
}

void IntKernelComparator::AddOutput(const string &name,
                                    const Shape &shape,
                                    Type type) {
  Flow::Variable *output = flow_.AddVariable(name, type, shape);
  op_->AddOutput(output);
  outputs_.push_back(output);
}

static int64 GetInt(Instance *data, Tensor *t, int r) {
  switch (t->type()) {
    case DT_INT8: return *data->Get<int8>(t, r);
    case DT_INT16: return *data->Get<int16>(t, r);
    case DT_INT32: return *data->Get<int32>(t, r);
    case DT_INT64: return *data->Get<int64>(t, r);
    default:
      LOG(FATAL) << "Unsupported type " << t->type();
  }
  return 0;
}

static int64 GetInt(Instance *data, Tensor *t, int r, int c) {
  switch (t->type()) {
    case DT_INT8: return *data->Get<int8>(t, r, c);
    case DT_INT16: return *data->Get<int16>(t, r, c);
    case DT_INT32: return *data->Get<int32>(t, r, c);
    case DT_INT64: return *data->Get<int64>(t, r, c);
    default:
      LOG(FATAL) << "Unsupported type " << t->type();
  }
  return 0;
}

static void SetInt(Instance *data, Tensor *t, int r, int64 value) {
  switch (t->type()) {
    case DT_INT8: *data->Get<int8>(t, r) = value; break;
    case DT_INT16: *data->Get<int16>(t, r) = value; break;
    case DT_INT32: *data->Get<int32>(t, r) = value; break;
    case DT_INT64: *data->Get<int64>(t, r) = value; break;
    default:
      LOG(FATAL) << "Unsupported type " << t->type();
  }
}

static void SetInt(Instance *data, Tensor *t, int r, int c, int64 value) {
  switch (t->type()) {
    case DT_INT8: *data->Get<int8>(t, r, c) = value; break;
    case DT_INT16: *data->Get<int16>(t, r, c) = value; break;
    case DT_INT32: *data->Get<int32>(t, r, c) = value; break;
    case DT_INT64: *data->Get<int64>(t, r, c) = value; break;
    default:
      LOG(FATAL) << "Unsupported type " << t->type();
  }
}

bool IntKernelComparator::Check(int iterations) {
  VLOG(3) << "Compare " << op_->type << " kernel " << test_kernel_name_
          << " against " << base_kernel_name_;

  // Compile computation for base kernel.
  KernelCompiler base;
  if (!base.Compile(library_, flow_, op_->type, base_kernel_name_,
      FLAGS_base_code_output, FLAGS_debug_base)) {
    return false;
  }

  // Compile computation for base kernel.
  KernelCompiler test;
  if (!test.Compile(library_, flow_, op_->type, test_kernel_name_,
      FLAGS_test_code_output, FLAGS_debug_test)) {
    return false;
  }

  // Compare kernels on random sampled inputs.
  std::mt19937 prng;
  std::uniform_int_distribution<int> unit(FLAGS_minint, FLAGS_maxint);
  int num_errors = 0;
  int num_elements = 0;
  for (int iteration = 0; iteration < iterations; ++iteration) {
    // Create data instances for base and test.
    Instance base_data(base.func);
    Instance test_data(test.func);

    // Fill inputs with random data.
    for (int i = 0; i < inputs_.size(); ++i) {
      Flow::Variable *var = inputs_[i];
      Tensor *b = base.func->GetParameter(var->name);
      Tensor *t = test.func->GetParameter(var->name);
      if (var->rank() == 1) {
        for (int r = 0; r < var->dim(0); ++r) {
          int64 val = unit(prng);
          SetInt(&base_data, b, r, val);
          SetInt(&test_data, t, r, val);
          if (FLAGS_log_input_tensors) {
            LOG(INFO) << var->name << "[" << r << "]=" << val;
          }
        }
      } else if (var->rank() == 2) {
        for (int r = 0; r < var->dim(0); ++r) {
          for (int c = 0; c < var->dim(1); ++c) {
            int64 val = unit(prng);
            SetInt(&base_data, b, r, c, val);
            SetInt(&test_data, t, r, c, val);
            if (FLAGS_log_input_tensors) {
              LOG(INFO) << var->name << "[" << r << "," << c << "]=" << val;
            }
          }
        }
      } else {
        LOG(ERROR) << var->rank() << "D tensor not supported";
        return false;
      }
    }

    // Run base and test computation.
    base_data.Compute();
    test_data.Compute();

    // Compare output from base and test.
    for (int i = 0; i < outputs_.size(); ++i) {
      Flow::Variable *var = outputs_[i];
      Tensor *b = base.func->GetParameter(var->name);
      Tensor *t = test.func->GetParameter(var->name);
      if (var->rank() == 1) {
        num_elements += var->dim(0);
        for (int r = 0; r < var->dim(0); ++r) {
          int64 base_result = GetInt(&base_data, b, r);
          int64 test_result = GetInt(&test_data, t, r);
          int64 delta = abs(test_result - base_result);
          if (delta != 0.0) {
              VLOG(9) << "Base and test difference for "
                      << var->name << "[" << r << "] "
                      << base_result << " vs. " << test_result
                      << " (delta " << delta << ")";
            num_errors++;
          }
          if (FLAGS_log_output_tensors) {
            LOG(INFO) << var->name << "[" << r << "]=" << test_result;
          }
        }
      } else if (var->rank() == 2) {
        num_elements += var->elements();
        for (int r = 0; r < var->dim(0); ++r) {
          for (int c = 0; c < var->dim(1); ++c) {
            int64 base_result = GetInt(&base_data, b, r, c);
            int64 test_result = GetInt(&test_data, t, r, c);
            int64 delta = abs(test_result - base_result);
            if (delta != 0.0) {
              VLOG(9) << "Base and test difference for "
                      << var->name << "[" << r << "," << c << "] "
                      << base_result << " vs. " << test_result
                      << " (delta " << delta << ")";
              num_errors++;
            }
            if (FLAGS_log_output_tensors) {
              LOG(INFO) << var->name << "[" << r << "," << c << "]="
                        << test_result;
            }
          }
        }
      } else {
        LOG(ERROR) << var->rank() << "D tensor not supported";
        return false;
      }
    }
  }

  if (num_errors != 0) {
    LOG(ERROR) << num_errors << "/" << num_elements
              << " errors in comparison between " << test_kernel_name_
              << " and " << base_kernel_name_;
  }

  return num_errors == 0;
}

}  // namespace myelin
}  // namespace sling


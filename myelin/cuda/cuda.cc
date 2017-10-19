#include "myelin/cuda/cuda.h"

#include <iomanip>
#include <mutex>
#include <sstream>

#include "base/logging.h"
#include "myelin/cuda/cuda-api.h"
#include "string/printf.h"

namespace sling {
namespace myelin {

// Flag to check that we only try to initialize the CUDA library once.
static std::once_flag cuda_initialized;

// Number of CUDA-enabled devices.
static int num_cuda_devices = 0;

// Initialize CUDA support. This function should only be called once.
void CUDA::Init() {
  // Load the CUDA driver API.
  if (!LoadCUDALibrary()) return;

  // Initialize CUDA driver library.
  CHECK_CUDA(cuInit(0));

  // Get the number of CUDA-enabled devices.
  CHECK_CUDA(cuDeviceGetCount(&num_cuda_devices));
}

bool CUDA::Supported() {
  std::call_once(cuda_initialized, []() { Init(); });
  return num_cuda_devices > 0;
}

int CUDA::Devices() {
  if (!Supported()) return 0;
  return num_cuda_devices;
}

CUDADevice::CUDADevice(int number) : number_(number) {
  // Check that CUDA is supported.
  CHECK(CUDA::Supported());

  // Check that device is valid.
  CHECK_LT(number, num_cuda_devices);

  // Get device handle.
  CHECK_CUDA(cuDeviceGet(&handle_, number));

  // Create context for device.
  CHECK_CUDA(cuCtxCreate(&context_, CU_CTX_SCHED_SPIN, handle_));

  // Get compute capabilities.
  int minor, major;
  CHECK_CUDA(cuDeviceComputeCapability(&major, &minor, handle_));
  capability_ = major * 10 + minor;
}

CUDADevice::~CUDADevice() {
  for (auto *m : modules_) delete m;
  CHECK_CUDA(cuCtxDetach(context_));
}

CUDAModule *CUDADevice::Compile(const char *ptx) {
  CUDAModule *module = new CUDAModule(ptx);
  modules_.push_back(module);
  return module;
}

int CUDADevice::CoresPerSM() const {
  switch (capability_) {
    case 20: return 32;   // Fermi Generation (SM 2.0) GF100 class
    case 21: return 48;   // Fermi Generation (SM 2.1) GF10x class
    case 30: return 192;  // Kepler Generation (SM 3.0) GK10x class
    case 32: return 192;  // Kepler Generation (SM 3.2) GK10x class
    case 35: return 192;  // Kepler Generation (SM 3.5) GK11x class
    case 37: return 192;  // Kepler Generation (SM 3.7) GK21x class
    case 50: return 128;  // Maxwell Generation (SM 5.0) GM10x class
    case 52: return 128;  // Maxwell Generation (SM 5.2) GM20x class
    case 53: return 128;  // Maxwell Generation (SM 5.3) GM20x class
    case 60: return 64;   // Pascal Generation (SM 6.0) GP100 class
    case 61: return 128;  // Pascal Generation (SM 6.1) GP10x class
    case 62: return 128;  // Pascal Generation (SM 6.2) GP10x class
    default: return 128;
  }
}

string CUDADevice::Name() const {
  // Get GPU device name.
  char name[256];
  CHECK_CUDA(cuDeviceGetName(name, sizeof(name), handle_));
  return name;
}

size_t CUDADevice::TotalMemory() const {
  // Get size of GPU global memory.
  size_t memory;
  CHECK_CUDA(cuDeviceTotalMem(&memory, handle_));
  return memory;
}

string CUDADevice::ToString() const {
  int version;
  CHECK_CUDA(cuDriverGetVersion(&version));
  string name = Name();
  size_t memory = TotalMemory();
  int64 bandwidth = memory_transfer_rate() * (bus_width() / 8);
  string str;
  StringAppendF(&str, "%s, SM %d.%d, %lu MB RAM, "
                "%d cores @ %lld MHz, "
                "%lld GB/s bandwidth (%d-bits @ %lld Mhz), "
                "%d KB L2 cache, "
                "CUDA v%d.%d",
                name.c_str(),
                capability_ / 10, capability_ % 10,
                memory >> 20,
                cores(),
                clock_rate() / 1000000,
                bandwidth / 1000000000,
                bus_width(),
                memory_transfer_rate() / 1000000,
                l2_cache_size() >> 10,
                version / 1000, version % 1000);
  return str;
}

CUDAModule::CUDAModule(const char *ptx) {
  const static int buffer_size = 1024;
  const static int num_options = 5;
  char log[buffer_size];
  char errors[buffer_size];
  CUjit_option option[num_options];
  void *value[num_options];

  option[0] = CU_JIT_INFO_LOG_BUFFER;
  value[0] = log;
  memset(log, 0, buffer_size);

  option[1] = CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES;
  value[1] = reinterpret_cast<void *>(buffer_size);

  option[2] = CU_JIT_ERROR_LOG_BUFFER;
  value[2] = errors;
  memset(errors, 0, buffer_size);

  option[3] = CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES;
  value[3] = reinterpret_cast<void *>(buffer_size);

  option[4] = CU_JIT_FALLBACK_STRATEGY;
  value[4] = reinterpret_cast<void *>(CU_PREFER_PTX);

  CUresult res = cuModuleLoadDataEx(&handle_, ptx, num_options, option, value);
  if (res != CUDA_SUCCESS) {
    LOG(ERROR) << "PTX compile error " << res << ": " << errors;
    const char *line = ptx;
    int lineno = 1;
    for (;;) {
      const char *end = strchr(line, '\n');
      if (end == nullptr) end = line + strlen(line);
      LOG(INFO) << lineno++ << ": " << string(line, end - line);
      if (*end == 0) break;
      line = end + 1;
      if (lineno > 100) break;
    }
    LOG(FATAL) << "Error compiling PTX code";
  }
  if (strlen(log) > 0) {
    LOG(INFO) << log;
  }
}

CUDAModule::~CUDAModule() {
  CHECK_CUDA(cuModuleUnload(handle_));
}

CUfunction CUDAModule::function(const char *name) {
  CUfunction func;
  CHECK_CUDA(cuModuleGetFunction(&func, handle_, name));
  return func;
}

CUDAFunction::CUDAFunction(const CUDAModule &module, const char *name) {
  CHECK_CUDA(cuModuleGetFunction(&handle_, module.handle(), name));
}

void PTXLiteral::Generate(string *code) const {
  code->append(arg_);
}

void PTXLabel::Generate(string *code) const {
  code->append(name_);
}

void PTXImm::Generate(string *code) const {
  code->append(std::to_string(number_));
}

void PTXFloat::Generate(string *code) const {
  std::ostringstream out;
  out.setf(std::ios_base::showpoint);
  out << std::setprecision(15) << number_;
  code->append(out.str());
}

void PTXReg::Generate(string *code) const {
  code->append(name_);
  if (index_ != -1) code->append(std::to_string(index_));
}

void PTXAddr::Generate(string *code) const {
  code->append("[");
  if (reg_.none()) {
    // Absolute address.
    code->append(std::to_string(static_cast<uint64>(disp_)));
  } else {
    // Register addressing with optional displacement.
    reg_.Generate(code);
    if (disp_ > 0) {
      code->append("+");
      code->append(std::to_string(disp_));
    } else if (disp_ < 0) {
      code->append("-");
      code->append(std::to_string(-disp_));
    }
  }
  code->append("]");
}

void PTXAssembler::Generate(string *ptx) {
  // Generate directives.
  ptx->clear();
  ptx->append(".version 5.0\n");
  ptx->append(".target sm_");
  ptx->append(std::to_string(target_));
  ptx->append("\n");
  ptx->append(".address_size 64\n");

  // Generate source file index.
  if (generate_line_info_) {
    for (int i = 0; i < source_files_.size(); ++i) {
      ptx->append(".file ");
      ptx->append(std::to_string(i));
      ptx->push_back(' ');
      ptx->push_back('"');
      ptx->append(source_files_[i]);
      ptx->push_back('"');
      ptx->push_back('\n');
    }
  }

  ptx->append(".visible .entry ");
  ptx->append(name_);
  ptx->append("(");
  bool first = true;
  for (auto &p : parameters_) {
    if (!first) ptx->append(", ");
    ptx->append(".param .");
    ptx->append(p.reg.type());
    ptx->append(" ");
    ptx->append(p.reg.name());
    if (p.reg.index() != -1) {
      ptx->append(std::to_string(p.reg.index()));
    }
    first = false;
  }
  ptx->append(") {\n");

  // Generate register declarations.
  for (auto &r : registers_) {
    if (r.source != -1 && r.line != -1) {
      ptx->append(".loc ");
      ptx->append(std::to_string(r.source));
      ptx->push_back(' ');
      ptx->append(std::to_string(r.line));
      ptx->append(" 0\n");
    }
    ptx->append(".reg .");
    ptx->append(r.reg.type());
    ptx->append(" ");
    ptx->append(r.reg.name());
    if (r.reg.index() != -1) {
      ptx->append(std::to_string(r.reg.index()));
    }
    ptx->append(";\n");
  }

  // Add code instructions.
  ptx->append(code_);
  ptx->append("}\n");
}

void PTXAssembler::EmitLoc(const char *source, int line) {
  int fileno = SourceIndex(source);
  if (fileno != -1 && line != -1) {
    code_.append(".loc ");
    code_.append(std::to_string(fileno));
    code_.push_back(' ');
    code_.append(std::to_string(line));
    code_.append(" 0\n");
  }
}

void PTXAssembler::EmitPredicate() {
  if (predicate_.name() == nullptr) return;
  code_.push_back('@');
  if (!condition_) code_.push_back('!');
  predicate_.Generate(&code_);
  EmitSpace();
}

void PTXAssembler::EmitInstruction(const PTXInstr &instr) {
  for (const char *p = instr.op(); *p; ++p) {
    code_.push_back(*p == '_' ? '.' : *p);
  }
  if (instr.type() != nullptr) {
    code_.push_back('.');
    code_.append(instr.type());
  }
  EmitSpace();
}

void PTXAssembler::EmitArg(const PTXArg &arg) {
  arg.Generate(&code_);
}

void PTXAssembler::EmitLabel(const char *name) {
  code_.append(name);
  code_.append(":\n");
}

void PTXAssembler::EmitLineEnd() {
  code_.push_back(';');
  code_.push_back('\n');
}

void PTXAssembler::EmitSpace() {
  code_.push_back(' ');
}

void PTXAssembler::EmitComma() {
  code_.push_back(',');
}

int PTXAssembler::SourceIndex(const char *source) {
  if (source == nullptr) return -1;
  if (!generate_line_info_) return -1;
  for (int i = 0; i < source_files_.size(); ++i) {
    if (source == source_files_[i]) return i;
  }
  source_files_.push_back(source);
  return source_files_.size() - 1;
}

}  // namespace myelin
}  // namespace sling


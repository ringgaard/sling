#ifndef MYELIN_CUDA_CUDA_H_
#define MYELIN_CUDA_CUDA_H_

#include <iostream>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/types.h"
#include "myelin/cuda/cuda-api.h"

namespace sling {
namespace myelin {

class CUDAModule;

// Check that CUDA call is successful.
#define CHECK_CUDA(op) CHECK_EQ((op), CUDA_SUCCESS)

// CUDA driver interface.
class CUDA {
 public:
  // Check if CUDA is supported on computer and it has a GPU.
  static bool Supported();

  // Return the number of CUDA-enabled GPUs.
  static int Devices();

 private:
  // Initialize CUDA. This function should only be called once.
  static void Init();
};

// CUDA device.
class CUDADevice {
 public:
  // Initialize CUDA device.
  CUDADevice(int number);
  ~CUDADevice();

  // Return device number.
  int number() const { return number_; }

  // Return handle for device.
  CUdevice handle() const { return handle_; }

  // Return context for device.
  CUcontext context() const { return context_; }

  // Compile PTX code and return module. The module is owned by the device
  // object and is destroyed together with the device object.
  CUDAModule *Compile(const char *ptx);

  // Return compute capability for device.
  int capability() const { return capability_; }

  // Get device attributes.
  int GetAttribute(CUdevice_attribute attr) const {
    int value;
    CHECK_CUDA(cuDeviceGetAttribute(&value, attr, handle_));
    return value;
  }

  // Return Number of multiprocessors on the device.
  int multiprocessors() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT);
  }

  // Return GPU clock rate in Hz.
  int64 clock_rate() const {
    return 1000LL * GetAttribute(CU_DEVICE_ATTRIBUTE_CLOCK_RATE);
  }

  // Return GPU memory transfer rate in Hz.
  int64 memory_transfer_rate() const {
    return 1000LL * GetAttribute(CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE);
  }

  // Return global memory bus width in bits.
  int bus_width() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH);
  }

  // Return L2 cache size.
  int l2_cache_size() const {
    return GetAttribute(CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE);
  }

  // Return number of cores per processor.
  int CoresPerSM() const;

  // Return number of cores.
  int cores() const { return multiprocessors() * CoresPerSM(); }

  // Return device name.
  string Name() const;

  // Return total amount of global memory on device.
  size_t TotalMemory() const;

  // Return device information as text.
  string ToString() const;

 public:
  // Device number.
  int number_;

  // CUDA device handle.
  CUdevice handle_;

  // Context for device.
  CUcontext context_;

  // Compute capabilities.
  int capability_;

  // List of modules owned by device.
  std::vector<CUDAModule *> modules_;
};

// CUDA module.
class CUDAModule {
 public:
  // Compile and initialize PTX module.
  CUDAModule(const char *ptx);
  ~CUDAModule();

  // Return module handle.
  CUmodule handle() const { return handle_; }

  // Get function handle.
  CUfunction function(const char *name);

 private:
  // CUDA module handle.
  CUmodule handle_;
};

// CUDA function.
class CUDAFunction {
 public:
  // Initialize CUDA kernel function.
  CUDAFunction(CUfunction handle) : handle_(handle) {}
  CUDAFunction(const CUDAModule &module, const char *name);

  // Return function handle.
  CUfunction handle() const { return handle_; }

  // Get function attributes.
  int GetAttribute(CUfunction_attribute attr) const {
    int value;
    CHECK_CUDA(cuFuncGetAttribute(&value, attr, handle_));
    return value;
  }

  // Return the maximum number of threads per block, beyond which a launch of
  // the function would fail. This number depends on both the function and the
  // device on which the function is currently loaded.
  int max_threads_per_block() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
  }

  // Return the size in bytes of statically-allocated shared memory per block
  // required by this function. This does not include dynamically-allocated
  // shared memory requested by the user at runtime.
  int shared_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES);
  }

  // Return the size in bytes of user-allocated constant memory required by this
  // function.
  int const_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES);
  }

  // Return the size in bytes of local memory used by each thread of this
  // function.
  int local_size() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES);
  }

  // Return the number of registers used by each thread of this function.
  int num_regs() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_NUM_REGS);
  }

  // Return the PTX virtual architecture version for which the function was
  // compiled. This value is the major PTX version * 10 + the minor PTX version.
  int ptx_version() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_PTX_VERSION);
  }

  // Return the binary architecture version for which the function was compiled.
  // This value is the major binary version * 10 + the minor binary version.
  int binary_version() const {
    return GetAttribute(CU_FUNC_ATTRIBUTE_BINARY_VERSION);
  }

 private:
  // CUDA function handle.
  CUfunction handle_;
};

// PTX assembler instruction argument.
class PTXArg {
 public:
  ~PTXArg() = default;
  virtual void Generate(string *code) const = 0;
};

// PTX literal argument.
class PTXLiteral : public PTXArg {
 public:
  PTXLiteral(const char *arg) : arg_(arg) {}

  void Generate(string *code) const override;

 private:
  const char *arg_;
};

// PTX immediate argument.
class PTXImm : public PTXArg {
 public:
  PTXImm(int number) : number_(number) {}

  void Generate(string *code) const override;

 private:
  int number_;
};

// PTX register argument.
class PTXReg : public PTXArg {
 public:
  PTXReg(const char *type, const char *name)
      : type_(type), name_(name), index_(-1) {}
  PTXReg(const char *type, const char *name, int index)
      : type_(type), name_(name), index_(index) {}

  void Generate(string *code) const override;

  const char *type() const { return type_; }
  const char *name() const { return name_; }
  int index() const { return index_; }

 private:
  const char *type_;  // register type
  const char *name_;  // register name
  int index_;         // index for register arrays
};

// PTX address indirection argument.
class PTXAddr : public PTXArg {
 public:
  PTXAddr(const PTXReg &reg) : reg_(reg), ofs_(0) {}
  PTXAddr(const PTXReg &reg, int ofs) : reg_(reg), ofs_(ofs) {}

  void Generate(string *code) const override;

 private:
  const PTXReg &reg_;
  int ofs_;
};

// PTX assemlber for generating code for CUDA kernels.
class PTXAssembler {
 public:
  // Initialize PTX assembler for generating code for function.
  PTXAssembler(const string &name) : name_(name) {}

  // Generate PTX code for function.
  void Generate(string *ptx);

  // Declare register.
  PTXReg reg(const char *type, const char *name) {
    registers_.emplace_back(type, name);
    return registers_.back();
  }

  PTXReg reg(const char *type, const char *name, int index) {
    registers_.emplace_back(type, name, index);
    return registers_.back();
  }

  // Declare parameter.
  PTXReg param(const char *type, const char *name) {
    parameters_.emplace_back(type, name);
    return parameters_.back();
  }

  // Emit instruction with no arguments.
  void emit(const char *instr) {
    EmitInstruction(instr);
    EmitLineEnd();
  }

  // Emit instruction with one argument.
  void emit(const char *instr, const PTXArg &arg1) {
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitLineEnd();
  }

  // Emit instruction with two arguments.
  void emit(const char *instr, const PTXArg &arg1, const PTXArg &arg2) {
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitLineEnd();
  }

  // Emit instruction with three arguments.
  void emit(const char *instr, const PTXArg &arg1, const PTXArg &arg2,
            const PTXArg &arg3) {
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitComma();
    EmitArg(arg3);
    EmitLineEnd();
  }

  // Emit instruction with four arguments.
  void emit(const char *instr, const PTXArg &arg1, const PTXArg &arg2,
            const PTXArg &arg3, const PTXArg &arg4) {
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitComma();
    EmitArg(arg3);
    EmitComma();
    EmitArg(arg4);
    EmitLineEnd();
  }

  // Emit predicated instruction with no arguments.
  void emit(const PTXReg &pred, const char *instr) {
    EmitPredicate(pred);
    EmitInstruction(instr);
    EmitLineEnd();
  }

  // Emit predicated instruction with one argument.
  void emit(const PTXReg &pred, const char *instr, const PTXArg &arg1) {
    EmitPredicate(pred);
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitLineEnd();
  }

  // Emit predicated instruction with two arguments.
  void emit(const PTXReg &pred, const char *instr, const PTXArg &arg1,
            const PTXArg &arg2) {
    EmitPredicate(pred);
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitLineEnd();
  }

  // Emit predicated instruction with three arguments.
  void emit(const PTXReg &pred, const char *instr, const PTXArg &arg1,
            const PTXArg &arg2, const PTXArg &arg3) {
    EmitPredicate(pred);
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitComma();
    EmitArg(arg3);
    EmitLineEnd();
  }

  // Emit predicated instruction with four arguments.
  void emit(const PTXReg &pred, const char *instr, const PTXArg &arg1,
            const PTXArg &arg2, const PTXArg &arg3, const PTXArg &arg4) {
    EmitPredicate(pred);
    EmitInstruction(instr);
    EmitArg(arg1);
    EmitComma();
    EmitArg(arg2);
    EmitComma();
    EmitArg(arg3);
    EmitComma();
    EmitArg(arg4);
    EmitLineEnd();
  }

  // Declare label.
  void label(const char *name) {
    EmitLabel(name);
  }

  // CUDA SM target architecture.
  int target() const { return target_; }
  void set_target(int target) { target_ = target; }

 private:
  // Emit predicate.
  void EmitPredicate(const PTXReg &pred);

  // Emit instruction name. Underscores are replaced by periods.
  void EmitInstruction(const char *instr);

  // Emit instruction argument.
  void EmitArg(const PTXArg &arg);

  // Emit label declaration.
  void EmitLabel(const char *name);

  // Emit line termination with semicolon.
  void EmitLineEnd();

  // Emit a space character.
  void EmitSpace();

  // Emit a comma.
  void EmitComma();

  // Function name.
  string name_;

  // Target architecture.
  int target_ = 21;

  // Function parameters.
  std::vector<PTXReg> parameters_;

  // Declared registers.
  std::vector<PTXReg> registers_;

  // PTX code instruction buffer.
  string code_;
};

// Utility macros for emitting PTX code.
#define ptx_decl(type, name) PTXReg name = ptx->reg(#type, #name)
#define ptx_param(type, name) PTXReg name = ptx->param(#type, #name)
#define ptx_emit(instr, ...) ptx->emit(#instr, __VA_ARGS__)
#define ptx_pemit(pred, instr, ...) ptx->emit(pred, #instr, __VA_ARGS__)
#define ptx_label(name) ptx->label(#name)
#define ptx_ret() ptx->emit("ret")

}  // namespace myelin
}  // namespace sling

#endif  // MYELIN_CUDA_CUDA_H_


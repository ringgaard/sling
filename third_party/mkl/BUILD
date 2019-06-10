# Intel Math Kernel Library

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
  name = "mkl",
  hdrs = [
    "include/mkl.h",
    "include/mkl_version.h",
    "include/mkl_types.h",
    "include/mkl_blas.h",
    "include/mkl_trans.h",
    "include/mkl_cblas.h",
    "include/mkl_spblas.h",
    "include/mkl_lapack.h",
    "include/mkl_lapacke.h",
    "include/mkl_pardiso.h",
    "include/mkl_dss.h",
    "include/mkl_sparse_handle.h",
    "include/mkl_rci.h",
    "include/mkl_service.h",
    "include/mkl_vml.h",
    "include/mkl_vml_defines.h",
    "include/mkl_vml_types.h",
    "include/mkl_vml_functions.h",
    "include/mkl_vsl.h",
    "include/mkl_vsl_defines.h",
    "include/mkl_vsl_functions.h",
    "include/mkl_vsl_types.h",
    "include/mkl_df.h",
    "include/mkl_df_defines.h",
    "include/mkl_df_functions.h",
    "include/mkl_df_types.h",
    "include/mkl_dfti.h",
    "include/mkl_trig_transforms.h",
    "include/mkl_poisson.h",
    "include/mkl_solvers_ee.h",
    "include/mkl_direct_call.h",
    "include/mkl_dnn.h",
    "include/mkl_dnn_types.h",
    "include/mkl_compact.h",
  ],
  copts = ["-Iinclude"],
  # See: https://software.intel.com/en-us/articles/intel-mkl-link-line-advisor
  #linkopts = ["-L/opt/intel/mkl/lib/intel64 -Wl,--no-as-needed -lmkl_intel_ilp64 -lmkl_sequential -lmkl_core -lpthread -lm -ldl"],
  linkopts = ["-L/opt/intel/mkl/lib/intel64 -Wl,--no-as-needed -lmkl_intel_ilp64 -lmkl_gnu_thread -lmkl_core -lgomp -lpthread -lm -ldl"],
  #linkopts = ["-L/opt/intel/mkl/lib/intel64 -Wl,--no-as-needed -lmkl_intel_ilp64 -lmkl_tbb_thread -lmkl_core -ltbb -lstdc++ -lpthread -lm -ldl"],
)


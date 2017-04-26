package(default_visibility = ["//visibility:public"])

cc_library(
  name = "elf-writer",
  srcs = ["elf-writer.cc"],
  hdrs = ["elf-writer.h"],
  copts = [
    "-Wno-sign-compare",
  ],
)

cc_binary(
  name = "embed-data",
  srcs = ["embed-data.cc"],
  deps = [
    ":elf-writer",
  ],
)


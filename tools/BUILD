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

cc_binary(
  name = "shell",
  srcs = ["shell.cc"],
  deps = [
    "//base",
    "//file:posix",
    "//frame",
    "//frame:json",
    "//schema:feature-structure",
    "//schema:schemata",
    "//string",
  ],
)


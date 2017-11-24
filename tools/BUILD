package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "embed-data",
  srcs = ["embed-data.cc"],
  deps = [
    "//util:elf-writer",
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


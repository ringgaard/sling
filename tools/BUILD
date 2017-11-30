package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "embed-data",
  srcs = ["embed-data.cc"],
  deps = [
    "//sling/util:elf-writer",
  ],
)

cc_binary(
  name = "shell",
  srcs = ["shell.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/frame",
    "//sling/frame:json",
    "//sling/schema:feature-structure",
    "//sling/schema:schemata",
    "//sling/string",
  ],
)


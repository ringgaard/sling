package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "embed-data",
  srcs = ["embed-data.cc"],
  tags = ["linux"],
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

cc_binary(
  name = "codex",
  srcs = ["codex.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:recordio",
    "//sling/file:posix",
    "//sling/frame",
    "//sling/stream:memory",
    "//sling/string:printf",
    "//sling/util:fingerprint",
  ],
)


package(default_visibility = ["//visibility:public"])

cc_library(
  name = "feature-structure",
  srcs = ["feature-structure.cc"],
  hdrs = ["feature-structure.h"],
  deps = [
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
  ],
)

cc_library(
  name = "schemata",
  srcs = ["schemata.cc"],
  hdrs = ["schemata.h"],
  deps = [
    ":feature-structure",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
  ],
)


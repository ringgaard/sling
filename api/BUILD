package(default_visibility = ["//visibility:public"])

cc_library(
  name = "hello",
  srcs = ["hello.cc"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "frames",
  srcs = ["frames.cc"],
  hdrs = ["frames.h"],
  deps = [
    "//base",
    "//frame:store",
  ],
)

cc_binary(
  name = "sling.so",
  srcs = ["api.cc"],
  deps = [
    ":frames",
    ":hello",
    "//base:libinit",
    "//file:posix",
  ],
  linkshared = 1,
  linkstatic = 1,
)


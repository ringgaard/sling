package(default_visibility = ["//visibility:public"])

cc_library(
  name = "string",
  deps = [
    ":charset",
    ":ctype",
    ":numbers",
    ":printf",
    ":split",
    ":strcat",
    ":strip",
    ":text",
  ],
)

cc_library(
  name = "charset",
  hdrs = ["charset.h"],
  srcs = ["charset.cc"],
  deps = [
    ":text",
    "//sling/base",
  ],
)

cc_library(
  name = "ctype",
  hdrs = ["ctype.h"],
  srcs = ["ctype.cc"],
  copts = [
    "-Wno-narrowing",
  ]
)

cc_library(
  name = "numbers",
  hdrs = ["numbers.h"],
  srcs = ["numbers.cc"],
  deps = [
    ":ctype",
    "//sling/base",
  ],
)

cc_library(
  name = "printf",
  hdrs = ["printf.h"],
  srcs = ["printf.cc"],
  deps = [
    "//sling/base",
  ],
)

cc_library(
  name = "split",
  hdrs = ["split.h"],
  srcs = [
    "split.cc",
  ],
  deps = [
    ":charset",
    ":text",
    ":strip",
    "//sling/base",
  ],
)

cc_library(
  name = "strcat",
  hdrs = ["strcat.h"],
  srcs = ["strcat.cc"],
  deps = [
    ":ctype",
    ":numbers",
    ":text",
    "//sling/base",
  ],
)

cc_library(
  name = "strip",
  hdrs = ["strip.h"],
  srcs = ["strip.cc"],
  deps = [
    ":ctype",
    ":text",
  ],
)

cc_library(
  name = "text",
  hdrs = ["text.h"],
  srcs = ["text.cc"],
  deps = [
    ":ctype",
    "//sling/base",
    "//sling/util:city",
  ],
)

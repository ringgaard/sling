package(default_visibility = ["//visibility:public"])

cc_library(
  name = "bits",
  hdrs = ["bits.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "varint",
  hdrs = ["varint.h"],
  srcs = ["varint.cc"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "snappy",
  srcs = [
    "snappy.cc",
    "snappy-sinksource.cc",
  ],
  hdrs = [
    "snappy.h",
    "snappy-sinksource.h",
  ],
  deps = [
    ":bits",
    ":endian",
    ":varint",
    "//base",
  ],
  copts = [
    "-Wno-sign-compare",
  ],
)

cc_library(
  name = "endian",
  hdrs = ["endian.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "hash",
  srcs = ["hash.cc"],
  hdrs = ["hash.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "city",
  srcs = ["city.cc"],
  hdrs = ["city.h"],
)

cc_library(
  name = "fingerprint",
  srcs = ["fingerprint.cc"],
  hdrs = ["fingerprint.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "unicode",
  hdrs = [
    "unicode.h",
  ],
  srcs = [
    "unicode.cc",
    "unicodetab.cc",
  ],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "thread",
  srcs = ["thread.cc"],
  hdrs = [
    "thread.h",
  ],
  deps = [
    "//base",
  ],
  copts = [
    "-pthread",
  ],
)

cc_library(
  name = "threadpool",
  srcs = ["threadpool.cc"],
  hdrs = ["threadpool.h"],
  deps = [
    "//base",
    ":thread",
  ],
  copts = [
    "-pthread",
  ],
)

cc_library(
  name = "mutex",
  hdrs = ["mutex.h"],
  copts = [
    "-pthread",
  ],
)


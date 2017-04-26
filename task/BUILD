package(default_visibility = ["//visibility:public"])

cc_library(
  name = "message",
  srcs = ["message.cc"],
  hdrs = ["message.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "environment",
  hdrs = ["environment.h"],
  deps = [
    "//base",
  ],
)

cc_library(
  name = "task",
  srcs = ["task.cc"],
  hdrs = ["task.h"],
  deps = [
    ":environment",
    ":message",
    "//base",
    "//base:registry",
    "//string:numbers",
    "//string:printf",
  ],
)

cc_library(
  name = "container",
  srcs = ["container.cc"],
  hdrs = ["container.h"],
  deps = [
    ":environment",
    ":task",
    "//base",
    "//file",
    "//string:numbers",
    "//util:mutex",
    "//util:threadpool",
  ],
)

cc_library(
  name = "process",
  srcs = ["process.cc"],
  hdrs = ["process.h"],
  deps = [
    ":task",
    "//util:thread",
  ],
)

cc_library(
  name = "workers",
  srcs = ["workers.cc"],
  deps = [
    ":task",
    "//util:threadpool",
  ],
  alwayslink = 1,
)

cc_library(
  name = "text-file-reader",
  srcs = ["text-file-reader.cc"],
  deps = [
    ":process",
    ":task",
    "//stream:file-input",
  ],
  alwayslink = 1,
)

cc_library(
  name = "text-file-writer",
  srcs = ["text-file-writer.cc"],
  deps = [
    ":task",
    "//stream:file",
    "//stream:output",
    "//util:mutex",
  ],
  alwayslink = 1,
)

cc_library(
  name = "text-map-reader",
  srcs = ["text-map-reader.cc"],
  deps = [
    ":process",
    ":task",
    "//stream:file-input",
  ],
  alwayslink = 1,
)

cc_library(
  name = "text-map-writer",
  srcs = ["text-map-writer.cc"],
  deps = [
    ":task",
    "//stream:file",
    "//stream:output",
    "//util:mutex",
  ],
  alwayslink = 1,
)

cc_library(
  name = "record-file-reader",
  srcs = ["record-file-reader.cc"],
  deps = [
    ":process",
    ":task",
    "//base",
    "//file:recordio",
  ],
  alwayslink = 1,
)

cc_library(
  name = "record-file-writer",
  srcs = ["record-file-writer.cc"],
  deps = [
    ":task",
    "//base",
    "//file:recordio",
    "//util:mutex",
  ],
  alwayslink = 1,
)

cc_library(
  name = "message-printer",
  srcs = ["message-printer.cc"],
  deps = [
    ":task",
    "//base",
  ],
  alwayslink = 1,
)

cc_library(
  name = "sharder",
  srcs = ["sharder.cc"],
  deps = [
    ":task",
    "//base",
    "//util:fingerprint",
  ],
  alwayslink = 1,
)

cc_library(
  name = "mapper",
  srcs = ["mapper.cc"],
  hdrs = ["mapper.h"],
  deps = [
    ":task",
    "//base",
  ],
)

cc_library(
  name = "reducer",
  srcs = ["reducer.cc"],
  hdrs = ["reducer.h"],
  deps = [
    ":task",
    "//base",
    "//util:mutex",
  ],
)

cc_library(
  name = "identity",
  srcs = ["identity.cc"],
  deps = [
    ":task",
  ],
  alwayslink = 1,
)

cc_library(
  name = "sorter",
  srcs = ["sorter.cc"],
  deps = [
    ":task",
    "//base",
    "//file:recordio",
    "//string:printf",
    "//util:mutex",
  ],
  alwayslink = 1,
)

cc_library(
  name = "null-sink",
  srcs = ["null-sink.cc"],
  deps = [
    ":task",
  ],
  alwayslink = 1,
)

cc_library(
  name = "frames",
  srcs = ["frames.cc"],
  hdrs = ["frames.h"],
  deps = [
    ":task",
    "//base",
    "//frame",
    "//stream:file",
    "//stream:memory",
  ],
)

cc_library(
  name = "frame-store-reader",
  srcs = ["frame-store-reader.cc"],
  deps = [
    ":frames",
    ":process",
    ":task",
    "//base",
    "//frame",
    "//stream:file",
  ],
  alwayslink = 1,
)

cc_library(
  name = "frame-store-builder",
  srcs = ["frame-store-builder.cc"],
  deps = [
    ":frames",
    ":task",
    "//base",
    "//frame",
    "//stream:file",
    "//util:mutex",
  ],
  alwayslink = 1,
)

cc_library(
  name = "rekey",
  srcs = ["rekey.cc"],
  deps = [
    ":task",
    ":frames",
    "//base",
    "//frame",
  ],
  alwayslink = 1,
)

cc_library(
  name = "accumulator",
  srcs = ["accumulator.cc"],
  hdrs = ["accumulator.h"],
  deps = [
    ":reducer",
    ":task",
    "//base",
    "//string:numbers",
    "//string:text",
    "//util:fingerprint",
    "//util:mutex",
  ],
  alwayslink = 1,
)


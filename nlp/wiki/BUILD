package(default_visibility = ["//visibility:public"])

#load("/tools/proto", "proto_library")

#proto_library(
#  name = "wiki-proto",
#  src = "wiki.proto",
#  generate_cc = 1,
#)

cc_library(
  name = "wiki",
  srcs = ["wiki.cc"],
  hdrs = ["wiki.h"],
  deps = [
    "//base",
    "//util:unicode",
  ],
)

cc_library(
  name = "wikipedia-map",
  srcs = ["wikipedia-map.cc"],
  hdrs = ["wikipedia-map.h"],
  deps = [
    ":wiki",
    "//frame",
    "//string:text",
  ],
)

cc_library(
  name = "wiki-parser",
  srcs = ["wiki-parser.cc"],
  hdrs = ["wiki-parser.h"],
  deps = [
    "//base",
    "//string:ctype",
    "//string:strcat",
    "//string:text",
  ],
)

cc_library(
  name = "wikidata-importer",
  srcs = ["wikidata-importer.cc"],
  deps = [
    ":wiki",
    "//frame",
    "//stream:input",
    "//stream:output",
    "//string:text",
    "//string:numbers",
    "//task",
    "//task:frames",
  ],
  alwayslink = 1,
)

cc_library(
  name = "wikipedia-importer",
  srcs = ["wikipedia-importer.cc"],
  deps = [
    ":wiki",
    "//base",
    "//file",
    "//frame",
    "//stream:file-input",
    "//string:numbers",
    "//string:printf",
    "//task",
    "//task:frames",
    "//task:process",
    "//web:xml-parser",
  ],
  alwayslink = 1,
)

cc_library(
  name = "wikipedia-profiles",
  srcs = ["wikipedia-profiles.cc"],
  deps = [
    ":wiki-parser",
    ":wikipedia-map",
    "//base",
    "//frame",
    "//nlp/document:text-tokenizer",
    "//nlp/document:tokens",
    "//task",
    "//task:frames",
    "//task:reducer",
    "//string:strcat",
    "//string:text",
  ],
  alwayslink = 1,
)

cc_library(
  name = "profile-aliases",
  srcs = ["profile-aliases.cc"],
  deps = [
    ":wiki",
    "//base",
    "//file:textmap",
    "//frame:object",
    "//nlp/document:phrase-tokenizer",
    "//task",
    "//task:frames",
    "//task:reducer",
    "//util:unicode",
  ],
  alwayslink = 1,
)

cc_library(
  name = "name-table-builder",
  srcs = ["name-table-builder.cc"],
  deps = [
    "//base",
    "//file:repository",
    "//frame:object",
    "//task",
    "//task:frames",
    "//util:mutex",
    "//util:unicode",
  ],
  alwayslink = 1,
)

cc_library(
  name = "name-table",
  srcs = ["name-table.cc"],
  hdrs = ["name-table.h"],
  deps = [
    "//base",
    "//file:repository",
    "//string:text",
    "//util:unicode",
  ],
)

cc_binary(
  name = "parse-wiki-text",
  srcs = ["parse-wiki-text.cc"],
  deps = [
    ":wiki-parser",
    "//base",
    "//file",
    "//file:posix",
    "//string",
  ],
)


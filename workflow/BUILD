package(default_visibility = ["//visibility:public"])

cc_library(
  name = "common",
  srcs = ["common.cc"],
  hdrs = ["common.h"],
  deps = [
    "//stream:file-input",
    "//string:strcat",
    "//string:strip",
    "//string:text",
    "//task",
    "//task:container",
  ],
)

cc_binary(
  name = "wikidata",
  srcs = ["wikidata.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:wikidata-importer",
    "//task:container",
    "//task:record-file-writer",
    "//task:sharder",
    "//task:text-file-reader",
    "//task:workers",
  ],
)

cc_binary(
  name = "toxic-aliases",
  srcs = ["toxic-aliases.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:wiki",
    "//task:accumulator",
    "//task:container",
    "//task:frames",
    "//task:record-file-reader",
    "//task:sharder",
    "//task:sorter",
    "//task:text-map-writer",
    "//string:numbers",
  ],
)

cc_binary(
  name = "wikimap",
  srcs = ["wikimap.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:wikidata-importer",
    "//task:container",
    "//task:frame-store-builder",
    "//task:record-file-reader",
  ],
)

cc_binary(
  name = "wikipedia",
  srcs = ["wikipedia.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:wikipedia-importer",
    "//task:container",
    "//task:frame-store-builder",
    "//task:sharder",
    "//task:record-file-writer",
  ],
)

cc_binary(
  name = "wikiprofile",
  srcs = ["wikiprofile.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:wikipedia-profiles",
    "//task:container",
    "//task:record-file-reader",
    "//task:frames",
    "//task:record-file-writer",
    "//task:sharder",
    "//task:sorter",
    "//task:text-map-writer",
  ],
)

cc_binary(
  name = "repository",
  srcs = ["repository.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//task:container",
    "//task:frame-store-builder",
    "//task:frame-store-reader",
    "//task:frames",
    "//task:record-file-reader",
  ],
)

cc_binary(
  name = "aliases",
  srcs = ["aliases.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:profile-aliases",
    "//task:container",
    "//task:record-file-reader",
    "//task:record-file-writer",
    "//task:sharder",
    "//task:sorter",
  ],
)

cc_binary(
  name = "names",
  srcs = ["names.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//nlp/wiki:name-table-builder",
    "//task:container",
    "//task:record-file-reader",
  ],
)

cc_binary(
  name = "webharvest",
  srcs = ["webharvest.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//stream",
    "//stream:file-input",
    "//stream:input",
    "//stream:memory",
    "//string:ctype",
    "//string:printf",
    "//string:split",
    "//string:strip",
    "//task:accumulator",
    "//task:container",
    "//task:sharder",
    "//task:sorter",
    "//task:workers",
    "//task:text-map-writer",
    "//util:unicode",
    "//web:html-parser",
    "//web:rfc822-headers",
    "//web:url",
    "//web:warc-reader",
  ],
)

cc_binary(
  name = "wikifuse",
  srcs = ["wikifuse.cc"],
  deps = [
    ":common",
    "//base",
    "//file:posix",
    "//task:container",
    "//task:record-file-writer",
    "//task:record-file-reader",
    "//task:frames",
    "//task:task",
    "//task:sharder",
    "//task:sorter",
  ],
)


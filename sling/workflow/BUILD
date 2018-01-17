package(default_visibility = ["//visibility:public"])

cc_library(
  name = "common",
  srcs = ["common.cc"],
  hdrs = ["common.h"],
  deps = [
    "//sling/stream:file-input",
    "//sling/string:strcat",
    "//sling/string:strip",
    "//sling/string:text",
    "//sling/task",
    "//sling/task:job",
  ],
)

cc_binary(
  name = "wikidata",
  srcs = ["wikidata.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:wikidata-importer",
    "//sling/task:job",
    "//sling/task:record-file-writer",
    "//sling/task:sharder",
    "//sling/task:text-file-reader",
    "//sling/task:workers",
  ],
)

cc_binary(
  name = "toxic-aliases",
  srcs = ["toxic-aliases.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:wiki",
    "//sling/task:accumulator",
    "//sling/task:frames",
    "//sling/task:job",
    "//sling/task:record-file-reader",
    "//sling/task:sharder",
    "//sling/task:sorter",
    "//sling/task:text-map-writer",
    "//sling/string:numbers",
  ],
)

cc_binary(
  name = "wikimap",
  srcs = ["wikimap.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:wikidata-importer",
    "//sling/task:frame-store-writer",
    "//sling/task:job",
    "//sling/task:record-file-reader",
  ],
)

cc_binary(
  name = "wikipedia",
  srcs = ["wikipedia.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:wikipedia-importer",
    "//sling/task:frame-store-writer",
    "//sling/task:job",
    "//sling/task:sharder",
    "//sling/task:record-file-writer",
  ],
)

cc_binary(
  name = "wikiprofile",
  srcs = ["wikiprofile.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:wikipedia-profiles",
    "//sling/task:job",
    "//sling/task:frames",
    "//sling/task:record-file-reader",
    "//sling/task:record-file-writer",
    "//sling/task:sharder",
    "//sling/task:sorter",
    "//sling/task:text-map-writer",
  ],
)

cc_binary(
  name = "repository",
  srcs = ["repository.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/task:frame-store-reader",
    "//sling/task:frame-store-writer",
    "//sling/task:frames",
    "//sling/task:job",
    "//sling/task:record-file-reader",
  ],
)

cc_binary(
  name = "aliases",
  srcs = ["aliases.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:profile-aliases",
    "//sling/task:job",
    "//sling/task:record-file-reader",
    "//sling/task:record-file-writer",
    "//sling/task:sharder",
    "//sling/task:sorter",
  ],
)

cc_binary(
  name = "names",
  srcs = ["names.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/nlp/wiki:name-table-builder",
    "//sling/task:job",
    "//sling/task:record-file-reader",
  ],
)

cc_binary(
  name = "webharvest",
  srcs = ["webharvest.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/stream",
    "//sling/stream:file-input",
    "//sling/stream:input",
    "//sling/stream:memory",
    "//sling/string:ctype",
    "//sling/string:printf",
    "//sling/string:split",
    "//sling/string:strip",
    "//sling/task:accumulator",
    "//sling/task:job",
    "//sling/task:sharder",
    "//sling/task:sorter",
    "//sling/task:text-map-writer",
    "//sling/task:workers",
    "//sling/util:unicode",
    "//sling/web:html-parser",
    "//sling/web:rfc822-headers",
    "//sling/web:url",
    "//sling/web:warc-reader",
  ],
)

cc_binary(
  name = "wikifuse",
  srcs = ["wikifuse.cc"],
  deps = [
    ":common",
    "//sling/base",
    "//sling/file:posix",
    "//sling/task:frames",
    "//sling/task:job",
    "//sling/task:record-file-writer",
    "//sling/task:record-file-reader",
    "//sling/task:sharder",
    "//sling/task:sorter",
    "//sling/task:task",
  ],
)


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

cc_binary(
  name = "codex",
  srcs = ["codex.cc"],
  deps = [
    "//sling/base",
    "//sling/db:dbclient",
    "//sling/file",
    "//sling/file:recordio",
    "//sling/file:posix",
    "//sling/frame",
    "//sling/nlp/document",
    "//sling/nlp/document:lex",
    "//sling/stream:memory",
    "//sling/string:printf",
    "//sling/util:fingerprint",
  ],
)

cc_binary(
  name = "index",
  srcs = ["index.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:recordio",
    "//sling/file:posix",
  ],
)

cc_binary(
  name = "snaps",
  srcs = ["snaps.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/frame:serialization",
    "//sling/frame:snapshot",
    "//sling/frame:store",
  ],
)

cc_binary(
  name = "dbkeys",
  srcs = ["dbkeys.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/file:textmap",
    "//sling/db:dbclient",
  ],
)

cc_binary(
  name = "recfix",
  srcs = ["recfix.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/file:recordio",
  ],
)

cc_binary(
  name = "templgen",
  srcs = ["templgen.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/file:posix",
    "//sling/nlp/wiki:wiki-parser",
    "//sling/util:unicode",
  ],
)

cc_binary(
  name = "archive-scanner",
  srcs = [
    "archive-scanner.cc",
    "epsonds.cc",
    "epsonds.h",
  ],
  deps = [
    "//app",
    "//sling/base",
    "//sling/file",
    "//sling/file:embed",
    "//sling/file:posix",
    "//sling/net:http-server",
    "//sling/net:static-content",
    "//sling/net:web-sockets",
    "//sling/string:printf",
    "//sling/string:text",
    "//sling/util:json",
    "//sling/util:threadpool",
  ],
)


package(default_visibility = ["//visibility:public"])

cc_binary(
  name = "collabd",
  srcs = ["collabd.cc"],
  deps = [
    "//sling/base",
    "//sling/file:posix",
    "//sling/frame:store",
    "//sling/frame:decoder",
    "//sling/frame:encoder",
    "//sling/frame:reader",
    "//sling/frame:wire",
    "//sling/db:dbclient",
    "//sling/net:http-server",
    "//sling/net:web-sockets",
    "//sling/stream:input",
    "//sling/stream:file",
    "//sling/stream:memory",
    "//sling/string:strcat",
    "//sling/util:mutex",
    "//sling/util:queue",
    "//sling/util:thread",
    "//sling/util:unicode",
  ],
)


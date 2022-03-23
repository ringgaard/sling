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
    "//sling/net:http-server",
    "//sling/net:web-sockets",
    "//sling/stream:input",
    "//sling/stream:file",
    "//sling/stream:memory",
    "//sling/util:mutex",
    "//sling/util:thread",
  ],
)


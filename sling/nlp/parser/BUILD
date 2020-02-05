package(default_visibility = ["//visibility:public"])

cc_library(
  name = "parser-action",
  srcs = ["parser-action.cc"],
  hdrs = ["parser-action.h"],
  deps = [
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/string:strcat",
    "//sling/util:fingerprint",
  ],
)

cc_library(
  name = "parser-state",
  srcs = ["parser-state.cc"],
  hdrs = ["parser-state.h"],
  deps = [
    ":parser-action",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/nlp/document",
  ],
)

cc_library(
  name = "action-table",
  srcs = ["action-table.cc"],
  hdrs = ["action-table.h"],
  deps = [
    ":parser-action",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/frame:store",
  ],
)

cc_library(
  name = "roles",
  srcs = ["roles.cc"],
  hdrs = ["roles.h"],
  deps = [
    ":action-table",
    ":parser-state",
    "//sling/frame:object",
  ],
)

cc_library(
  name = "parser-features",
  srcs = ["parser-features.cc"],
  hdrs = ["parser-features.h"],
  deps = [
    ":parser-action",
    ":parser-state",
    ":roles",
    "//sling/base",
    "//sling/myelin:compute",
  ],
)

cc_library(
  name = "delegate",
  srcs = ["delegate.cc"],
  hdrs = ["delegate.h"],
  deps = [
    ":parser-action",
    "//sling/base",
    "//sling/base:registry",
    "//sling/frame:object",
    "//sling/myelin:compute",
  ],
)

cc_library(
  name = "parser-codec",
  srcs = ["parser-codec.cc"],
  hdrs = ["parser-codec.h"],
  deps = [
    "//sling/base",
    "//sling/base:registry",
    "//sling/frame:object",
    "//sling/myelin:compute",
    "//sling/myelin:flow",
    "//sling/nlp/document",
    "//sling/task",
    "//sling/util:vocabulary",
  ],
)

cc_library(
  name = "parser",
  srcs = ["parser.cc"],
  hdrs = ["parser.h"],
  deps = [
    ":delegate",
    ":parser-codec",
    ":parser-features",
    ":parser-state",
    ":roles",
    "//sling/base",
    "//sling/base:registry",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/myelin:compiler",
    "//sling/myelin:profile",
    "//sling/nlp/document",
  ],
)

cc_library(
  name = "parser-annotator",
  srcs = ["parser-annotator.cc"],
  deps = [
    ":parser",
    ":multiclass-delegate",
    "//sling/nlp/document:annotator",
  ],
  alwayslink = 1,
)

cc_library(
  name = "frame-evaluation",
  hdrs = ["frame-evaluation.h"],
  srcs = ["frame-evaluation.cc"],
  deps = [
    "//sling/base",
    "//sling/file",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/nlp/document:document",
    "//sling/nlp/document:document-corpus",
    "//sling/string:strcat",
    "//sling/string:printf",
  ],
)

cc_library(
  name = "transition-generator",
  srcs = ["transition-generator.cc"],
  hdrs = ["transition-generator.h"],
  deps = [
    ":parser-action",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/nlp/document:document",
  ],
)

cc_library(
  name = "lexrnn-encoder",
  srcs = ["lexrnn-encoder.cc"],
  deps = [
    ":parser-codec",
    "//sling/myelin:rnn",
    "//sling/nlp/document:lexical-features",
  ],
  alwayslink = 1,
)

cc_library(
  name = "transition-decoder",
  srcs = ["transition-decoder.cc"],
  hdrs = ["transition-decoder.h"],
  deps = [
    ":parser-action",
    ":parser-codec",
    ":parser-features",
    ":roles",
    "//sling/myelin:builder",
    "//sling/myelin:gradient",
  ],
)

cc_library(
  name = "parser-trainer",
  srcs = ["parser-trainer.cc"],
  hdrs = ["parser-trainer.h"],
  deps = [
    ":frame-evaluation",
    ":parser-action",
    ":parser-codec",
    ":parser-features",
    ":roles",
    "//sling/base",
    "//sling/file:textmap",
    "//sling/frame:store",
    "//sling/frame:serialization",
    "//sling/myelin:builder",
    "//sling/myelin:compiler",
    "//sling/myelin:gradient",
    "//sling/myelin:learning",
    "//sling/nlp/document:document",
    "//sling/nlp/document:document-corpus",
    "//sling/task:learner",
    "//sling/util:mutex",
  ],
)

cc_library(
  name = "multiclass-learner",
  srcs = ["multiclass-learner.cc"],
  hdrs = ["multiclass-learner.h"],
  deps = [
    ":action-table",
    ":parser-trainer",
    "//sling/base",
    "//sling/myelin:flow",
    "//sling/myelin:compute",
    "//sling/myelin:learning",
  ],
)

cc_library(
  name = "multiclass-delegate",
  srcs = ["multiclass-delegate.cc"],
  hdrs = ["multiclass-delegate.h"],
  deps = [
    ":action-table",
    ":delegate",
    "//sling/base",
    "//sling/myelin:flow",
    "//sling/myelin:compute",
    "//sling/myelin:gradient",
    "//sling/myelin:learning",
  ],
  alwayslink = 1,
)

cc_library(
  name = "caspar-trainer",
  srcs = ["caspar-trainer.cc"],
  deps = [
    ":parser-trainer",
    ":multiclass-learner",
    ":transition-generator",
  ],
  alwayslink = 1,
)

cc_library(
  name = "knolex-trainer",
  srcs = ["knolex-trainer.cc"],
  deps = [
    ":parser-trainer",
    ":multiclass-learner",
    ":transition-generator",
    "//sling/nlp/kb:facts",
  ],
  alwayslink = 1,
)


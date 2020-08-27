package(default_visibility = ["//visibility:public"])

# Frame semantic parser.

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
    ":parser-codec",
    "//sling/base",
    "//sling/frame:serialization",
    "//sling/frame:store",
    "//sling/myelin:compiler",
    "//sling/nlp/document",
  ],
)

cc_library(
  name = "parser-annotator",
  srcs = ["parser-annotator.cc"],
  deps = [
    ":parser",
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
  name = "parser-trainer",
  srcs = ["parser-trainer.cc"],
  deps = [
    ":components",
    ":frame-evaluation",
    ":parser-codec",
    "//sling/base",
    "//sling/file:textmap",
    "//sling/frame:store",
    "//sling/frame:serialization",
    "//sling/myelin:compiler",
    "//sling/myelin:learning",
    "//sling/nlp/document:document",
    "//sling/nlp/document:annotator",
    "//sling/nlp/document:document-corpus",
    "//sling/task:learner",
    "//sling/util:mutex",
  ],
  alwayslink = 1,
)

# Encoders.

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
  name = "subrnn-encoder",
  srcs = ["subrnn-encoder.cc"],
  deps = [
    ":parser-codec",
    "//sling/file:textmap",
    "//sling/myelin:builder",
    "//sling/myelin:gradient",
    "//sling/myelin:rnn",
    "//sling/nlp/document:subword-tokenizer",
    "//sling/nlp/document:wordpiece-builder",
    "//sling/util:unicode",
  ],
  alwayslink = 1,
)

# Decoders.

cc_library(
  name = "bio-decoder",
  srcs = ["bio-decoder.cc"],
  deps = [
    ":parser-codec",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/myelin:builder",
    "//sling/myelin:crf",
    "//sling/myelin:gradient",
    "//sling/myelin:learning",
    "//sling/nlp/kb:facts",
  ],
  alwayslink = 1,
)

cc_library(
  name = "biaffine-decoder",
  srcs = ["biaffine-decoder.cc"],
  deps = [
    ":parser-codec",
    "//sling/base",
    "//sling/frame:object",
    "//sling/frame:store",
    "//sling/myelin:builder",
    "//sling/myelin:gradient",
    "//sling/myelin:learning",
    "//sling/nlp/kb:facts",
  ],
  alwayslink = 1,
)

# Transition-based parsing.

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
  name = "transition-decoder",
  srcs = ["transition-decoder.cc"],
  hdrs = ["transition-decoder.h"],
  deps = [
    ":delegate",
    ":parser-action",
    ":parser-codec",
    ":parser-features",
    ":roles",
    ":transition-generator",
    "//sling/myelin:builder",
    "//sling/myelin:gradient",
  ],
  alwayslink = 1,
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
  name = "caspar-decoder",
  srcs = ["caspar-decoder.cc"],
  deps = [
    ":transition-decoder",
    ":multiclass-delegate",
    ":transition-generator",
    "//sling/nlp/document:document-corpus",
  ],
  alwayslink = 1,
)

cc_library(
  name = "knolex-decoder",
  srcs = ["knolex-decoder.cc"],
  deps = [
    ":transition-decoder",
    ":multiclass-delegate",
    ":transition-generator",
    "//sling/nlp/kb:facts",
  ],
  alwayslink = 1,
)

# Parser components.

cc_library(
  name = "components",
  deps = [
    # Annotators.
    ":parser-annotator",

    # Encoders.
    ":lexrnn-encoder",
    ":subrnn-encoder",

    # Decoders.
    ":transition-decoder",
    ":caspar-decoder",
    ":knolex-decoder",
    ":bio-decoder",
    ":biaffine-decoder",

    # Delegates.
    ":multiclass-delegate",
  ],
)

cc_binary(
  name = "parse",
  srcs = ["parse.cc"],
  deps = [
    "//sling/base",
    "//sling/base:clock",
    "//sling/file:posix",
    "//sling/frame:object",
    "//sling/frame:serialization",
    "//sling/myelin:profile",
    "//sling/nlp/document",
    "//sling/nlp/document:document-corpus",
    "//sling/nlp/document:document-tokenizer",
    "//sling/nlp/document:lex",
    "//sling/nlp/parser",
    "//sling/nlp/parser:components",
    "//sling/nlp/parser:frame-evaluation",
    "//sling/string:printf",
  ],
)


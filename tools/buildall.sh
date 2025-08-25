#!/bin/bash

PYVER=$(python3 -c 'import sys; v=sys.version_info; ver=v.major * 100 + v.minor; print(36 if ver < 309 else 312);')

bazel build -c opt --cxxopt=-DPYVER=${PYVER} $* \
  sling/base:* \
  sling/db:* \
  sling/file:* \
  sling/frame:* \
  sling/myelin:* \
  sling/myelin/kernel:* \
  sling/myelin/generator:* \
  sling/myelin/tests:* \
  sling/myelin/cuda:* \
  sling/net:* \
  sling/nlp/document:* \
  sling/nlp/embedding:* \
  sling/nlp/kb:* \
  sling/nlp/silver:* \
  sling/nlp/parser:* \
  sling/nlp/parser/tools:* \
  sling/nlp/wiki:* \
  sling/nlp/search:* \
  sling/org:* \
  sling/pyapi:* \
  sling/nlp/web:* \
  sling/nlp/wiki:* \
  sling/schema:* \
  sling/stream:* \
  sling/string:* \
  sling/task:* \
  sling/util:* \
  sling/web:* \
  tools:* \
  case:* \

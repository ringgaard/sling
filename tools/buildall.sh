#!/bin/sh

bazel build -c opt $* \
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
  sling/pyapi:* \
  sling/nlp/web:* \
  sling/nlp/wiki:* \
  sling/schema:* \
  sling/stream:* \
  sling/string:* \
  sling/task:* \
  sling/util:* \
  sling/web:* \
  sling/workflow:* \
  tools:* \
  case:* \


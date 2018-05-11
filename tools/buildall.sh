#!/bin/sh

bazel build -c opt $* \
  sling/base:* \
  sling/file:* \
  sling/frame:* \
  sling/http:* \
  sling/myelin:* \
  sling/myelin/kernel:* \
  sling/myelin/generator:* \
  sling/myelin/tests:* \
  sling/myelin/cuda:* \
  sling/nlp/document:* \
  sling/nlp/embedding:* \
  sling/nlp/kb:* \
  sling/nlp/parser:* \
  sling/nlp/parser/tools:* \
  sling/nlp/parser/trainer:* \
  sling/nlp/kb:* \
  sling/nlp/wiki:* \
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


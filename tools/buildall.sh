#!/bin/sh

bazel build -c opt $* \
  sling/base:* \
  sling/db:* \
  sling/file:* \
  sling/frame:* \
  sling/myelin:* \
  sling/myelin/kernel:* \
  sling/myelin/generator:* \
  sling/myelin/cuda:* \
  sling/net:* \
  sling/nlp/document:* \
  sling/nlp/embedding:* \
  sling/nlp/kb:* \
  sling/nlp/silver:* \
  sling/nlp/parser:* \
  sling/nlp/wiki:* \
  sling/pyapi:* \
  sling/nlp/wiki:* \
  sling/stream:* \
  sling/string:* \
  sling/task:* \
  sling/util:* \
  sling/web:* \
  tools:* \


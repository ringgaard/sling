#!/bin/sh

bazel build -c opt \
  base:* \
  file:* \
  frame:* \
  http:* \
  myelin:* \
  nlp/document:* \
  nlp/kb:* \
  nlp/parser:* \
  nlp/web:* \
  nlp/wiki:* \
  schema:* \
  stream:* \
  string:* \
  task:* \
  tools:* \
  util:* \
  web:* \
  workflow:* \


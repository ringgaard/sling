#!/bin/sh

bazel build -c opt \
  base:* \
  file:* \
  frame:* \
  http:* \
  myelin:* \
  myelin/kernel:* \
  myelin/generator:* \
  myelin/tests:* \
  nlp/document:* \
  nlp/kb:* \
  nlp/parser:* \
  nlp/parser/tools:* \
  nlp/parser/trainer:* \
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


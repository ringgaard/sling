#!/bin/bash

SPEC='{
  annotator: "parser"
  annotator: "mention-name"
  inputs: {
    parser: {
      file: "local/data/corpora/caspar/parser.flow"
      format: "flow"
    }
  }
  parameters: {
    language: "en"
  }
}'

bazel-bin/sling/nlp/document/analyzer --spec "${SPEC}" $@


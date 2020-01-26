#!/bin/bash

SPEC='{
  annotator: "parser"
  annotator: "mention-name"
  inputs: {
    parser: {
      ;file: "local/data/e/caspar/caspar.flow"
      file: "local/data/e/knolex/knolex-en-1.flow"
      format: "flow"
    }
    commons: {
      file: "local/data/e/wiki/kb.sling"
      format: "store/frame"
    }
  }
  parameters: {
    language: "en"
  }
}'

bazel-bin/sling/nlp/document/analyzer --spec "${SPEC}" $@


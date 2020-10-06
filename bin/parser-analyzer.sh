#!/bin/bash

LANGUAGE=${LANGUAGE:-en}
PORT=${PORT:-8080}

SPEC='{
  annotator: "parser"
  annotator: "prune-nominals"
  ;annotator: "mention-name"

  inputs: {
    parser: {
      ;file: "data/e/caspar/caspar.flow"
      file: "data/e/silver/LANG/knolex.flow"
      ;file: "data/e/silver/LANG/bio.flow"
      format: "flow"
    }
    commons: {
      ;file: "data/e/wiki/kb.sling"
      file: "data/dev/types.sling"
      format: "store/frame"
    }
  }
  parameters: {
    language: "LANG"
  }
}'

bazel-bin/sling/nlp/document/analyzer \
  --spec "${SPEC//LANG/$LANGUAGE}" \
  --port $PORT $@ \
  $@


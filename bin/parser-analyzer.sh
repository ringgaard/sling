#!/bin/bash

LANGUAGE=${LANGUAGE:-en}
PORT=${PORT:-8080}

SPEC='{
  annotator: "parser"
  annotator: "prune-nominals"
  ;annotator: "mention-name"

  inputs: {
    parser: {
      ;file: "local/data/e/caspar/caspar.flow"
      file: "local/data/e/silver/LANG/knolex.flow"
      ;file: "local/data/e/silver/LANG/bio.flow"
      format: "flow"
    }
    commons: {
      ;file: "local/data/e/wiki/kb.sling"
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


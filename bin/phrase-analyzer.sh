#!/bin/bash

LANGUAGE=${LANGUAGE:-en}
PORT=${PORT:-8080}

SPEC='{
  annotator: "phrase-structure"
  annotator: "mention-name"

  inputs: {
    commons: {
      file: "local/data/e/wiki/kb.sling"
      format: "store/frame"
    }
    aliases: {
      file: "local/data/e/wiki/LANG/phrase-table.repo"
      format: "repository"
    }
  }
  parameters: {
    language: "LANG"
  }
}'

bazel-bin/sling/nlp/document/analyzer \
  --kb \
  --names local/data/e/wiki/$LANGUAGE/name-table.repo \
  --spec "${SPEC//LANG/$LANGUAGE}" \
  --port $PORT


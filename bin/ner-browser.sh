#!/bin/bash

LANGUAGE=${LANGUAGE:-en}
PORT=${PORT:-8080}

SPEC='{
  annotator: "ner"
  annotator: "phrase-structure"
  annotator: "mention-name"

  inputs: {
    commons: {
      file: "local/data/e/ner/kb.sling"
      format: "store/frame"
    }
    aliases: {
      file: "local/data/e/wiki/LANG/phrase-table.repo"
      format: "repository"
    }
    dictionary: {
      file: "local/data/e/ner/LANG/idf.repo"
      format: "repository"
    }
  }
  parameters: {
    language: "LANG"
    resolve: true
  }
}'

bazel-bin/sling/nlp/document/corpus-browser \
  --kb \
  --names local/data/e/wiki/$LANGUAGE/name-table.repo \
  --spec "${SPEC//LANG/$LANGUAGE}" \
  --port $PORT \
  local/data/e/wiki/$LANGUAGE/documents@10.rec


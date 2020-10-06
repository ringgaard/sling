#!/bin/bash

LANGUAGE=${LANGUAGE:-en}
PORT=${PORT:-8080}

SPEC='{
  annotator: "mentions"
  annotator: "anaphora"
  ;annotator: "phrase-structure"
  annotator: "relations"
  annotator: "types"
  ;annotator: "clear-references"
  annotator: "mention-name"

  inputs: {
    commons: {
      file: "data/e/wiki/kb.sling"
      format: "store/frame"
    }
    commons: {
      file: "data/wiki/LANG/silver.sling"
      format: "store/frame"
    }
    aliases: {
      file: "data/e/wiki/LANG/phrase-table.repo"
      format: "repository"
    }
    dictionary: {
      file: "data/e/silver/LANG/idf.repo"
      format: "repository"
    }
    phrases: {
      file: "data/wiki/LANG/phrases.txt"
      format: "text"
    }
  }
  parameters: {
    language: "LANG"
    resolve: true
    definite_reference: false
    initial_reference: false
  }
}'

bazel-bin/sling/nlp/document/corpus-browser \
  --kb \
  --names data/e/wiki/$LANGUAGE/name-table.repo \
  --spec "${SPEC//LANG/$LANGUAGE}" \
  --port $PORT $@ \
  data/e/wiki/$LANGUAGE/documents@10.rec


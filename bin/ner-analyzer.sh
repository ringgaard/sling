#!/bin/bash

LANG=en

SPEC='{
  annotator: "ner"
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
    resolve: 1
  }
}'

bazel-bin/sling/nlp/document/analyzer \
  --kb \
  --names local/data/e/wiki/$LANG/name-table.repo \
  --spec "${SPEC//LANG/$LANG}" \
  --port 8081


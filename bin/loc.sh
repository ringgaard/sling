#!/bin/bash

exists() {
  [ -e "$1" ]
}

loc() {
  path=$1

  cc=0
  if exists $path/*.cc; then
    cc=$(cat $path/*.cc | wc -l)
  fi

  h=0
  if exists $path/*.h; then
    h=$(cat $path/*.h | wc -l)
  fi

  py=0
  if exists $path/*.py; then
    py=$(cat $path/*.py | wc -l)
  fi

  js=0
  if exists $path/*.js; then
    js=$(cat $path/*.js | wc -l)
  fi

  echo -e "${path}\t${cc}\t${h}\t${py}\t${js}"
}

echo -e "path\tcc\th\tpy\tjs"

loc "sling/base"
loc "sling/util"

loc "sling/db"
loc "sling/net"
loc "sling/string"
loc "sling/file"
loc "sling/stream"
loc "sling/frame"

loc "app/lib"

loc "python"
loc "python/myelin"
loc "python/nlp"
loc "python/task"
loc "python/crawl"
loc "python/media"
loc "python/dataset"

loc "sling/pyapi"
loc "sling/web"

loc "sling/nlp/document"
loc "sling/nlp/document/app"
loc "sling/nlp/embedding"
loc "sling/nlp/kb"
loc "sling/nlp/kb/app"
loc "sling/nlp/parser"
loc "sling/nlp/silver"
loc "sling/nlp/web"
loc "sling/nlp/wiki"

loc "sling/task"
loc "sling/task/app"

loc "sling/myelin"
loc "sling/myelin/cuda"
loc "sling/myelin/generator"
loc "sling/myelin/kernel"
loc "third_party/jit"


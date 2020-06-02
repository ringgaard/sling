#!/bin/bash

run() {
  while true
  do
    echo "***" $(date) "Start database server"
    bazel-bin/sling/db/dbserver \
      --dbdir /archive/4/db \
      --auto_mount \
      --recover \
      --flushlog
    echo "***" $(date) "Database terminated"
    sleep 30
  done
}

echo "Starting database server"
run >> /tmp/db.log 2>&1 &


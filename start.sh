#!/bin/bash

if [ -d "local/logs" ]; then
  LOGFILE=local/logs/$(date -I)-$$.log
else
  LOGFILE=/tmp/sling-$(date -I)-$$.log
fi

echo Running run.py $* in the background logging to $LOGFILE
python python/run.py $* &> $LOGFILE &


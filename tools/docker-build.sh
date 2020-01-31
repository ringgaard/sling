#!/bin/bash

IMAGE=gcr.io/google.com/sling-198709/pysling

tools/buildall.sh
python3 tools/build-wheel.py
docker build -t $IMAGE .
docker push $IMAGE


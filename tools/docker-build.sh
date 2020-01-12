#!/bin/bash

tools/buildall.sh
python3 tools/build-wheel.py
docker build -t pysling .


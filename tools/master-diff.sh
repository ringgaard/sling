#!/bin/bash

#git fetch origin master
git diff --name-only origin/master -- | sort > /tmp/masterdiff.txt
sort data/dev/devfiles.txt > /tmp/devfiles.txt
comm -2 -3 /tmp/masterdiff.txt /tmp/devfiles.txt


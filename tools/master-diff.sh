#!/bin/bash

#git fetch upstream master
git diff --name-status upstream/master..dev | cut -f2 | sort > /tmp/masterdiff.txt
sort data/dev/devfiles.txt > /tmp/devfiles.txt
comm -2 -3 /tmp/masterdiff.txt /tmp/devfiles.txt


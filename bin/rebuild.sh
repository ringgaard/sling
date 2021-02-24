#!/bin/bash

# Update to newest version of code in git repo and rebuild.
echo "Refresh from git repo" $(date -I -d yesterday)
git pull 2>&1
tools/buildall.sh 2>&1


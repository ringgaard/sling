#!/bin/bash

# Update to newest version of code in git repo and rebuild.
echo "Refresh from git repo" $(date -I -d yesterday)
git pull origin dev 2>&1
tools/buildall.sh 2>&1


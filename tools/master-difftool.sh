#!/bin/bash

git difftool upstream/master..dev $(tools/master-diff.sh)


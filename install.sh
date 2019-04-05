#!/bin/bash

# Exit on errors.
set -e

echo "========================================================================="
echo "Installing SLING development environment"
echo "========================================================================="
echo

echo "=== Install dependencies"
sudo apt-get install pkg-config zip g++ zlib1g-dev unzip python2.7 python2.7-dev

echo "=== Install Bazel build system"
wget -P /tmp https://github.com/bazelbuild/bazel/releases/download/0.13.0/bazel-0.13.0-installer-linux-x86_64.sh
chmod +x /tmp/bazel-0.13.0-installer-linux-x86_64.sh
sudo /tmp/bazel-0.13.0-installer-linux-x86_64.sh

echo "=== Build SLING"
tools/buildall.sh

echo "=== Install SLING Python API"
sudo ln -s $(realpath python) /usr/lib/python2.7/dist-packages/sling

echo "=== SLING install SUCCESS"


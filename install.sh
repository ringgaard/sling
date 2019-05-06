#!/bin/bash

# Exit on errors.
set -e

echo "========================================================================="
echo "Installing SLING development environment"
echo "========================================================================="

# Install packages.
echo
echo "=== Install SLING dependencies"
PKGS="pkg-config zip g++ zlib1g-dev unzip python2.7 python2.7-dev python-pip"
sudo apt-get install ${PKGS}

# Install bazel.
BAZELVER=0.13.0
BAZELSH=bazel-${BAZELVER}-installer-linux-x86_64.sh
BAZELURL=https://github.com/bazelbuild/bazel/releases/download/${BAZELVER}/${BAZELSH}
if ! which bazel > /dev/null; then
  echo
  echo "=== Install Bazel build system"
  wget -P ${BAZELURL}
  chmod +x /tmp/${BAZELSH}
  sudo /tmp/${BAZELSH}
  rm /tmp/${BAZELSH}
fi

# Build SLING.
echo
echo "=== Build SLING"
tools/buildall.sh

# Install SLING Python API.
echo
echo "=== Install SLING Python API"
SLINGPKG=/usr/lib/python3/dist-packages/sling
if ! [ -x ${SLINGPKG} ]; then
  echo "Adding link for SLING Python package"
  sudo ln -s $(realpath python) ${SLINGPKG}
else
  echo "SLING Python package already installed"
fi

# Done.
echo
echo "=== SLING is now installed."


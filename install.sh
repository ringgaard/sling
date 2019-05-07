#!/bin/bash

# Exit on errors.
set -e

echo "========================================================================="
echo "Installing SLING development environment"
echo "========================================================================="

# Install packages.
echo
echo "=== Install SLING dependencies"
PKGS="pkg-config zip g++ zlib1g-dev unzip python3.6 python3.6-dev python3-pip"
sudo apt-get install ${PKGS}

# If your system does not have python 3.6 in the repo you can add it manually:
# sudo add-apt-repository ppa:jonathonf/python-3.6
# sudo apt update

# Install bazel.
BAZELVER=0.13.0
BAZELSH=bazel-${BAZELVER}-installer-linux-x86_64.sh
BAZELREPO=https://github.com/bazelbuild/bazel
BAZELURL=${BAZELREPO}/releases/download/${BAZELVER}/${BAZELSH}
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

if [ -L /usr/lib/python2.7/dist-packages/sling ]; then
  echo "Removing deprecated SLING Python 2.7 package"
  sudo rm /usr/lib/python2.7/dist-packages/sling
fi

# Done.
echo
echo "=== SLING is now installed."


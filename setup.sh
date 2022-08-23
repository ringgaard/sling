#!/bin/bash

# Exit on errors.
set -e

echo "========================================================================="
echo "Set up SLING development environment"
echo "========================================================================="

# Install packages.
echo
echo "=== Install SLING dependencies"
PYVER=3.6
PYPKGS="python${PYVER} python${PYVER}-dev python3-pip"
CLIBS="lbzip2 libcurl4-openssl-dev"
PKGS="g++ ${CLIBS} ${PYPKGS} unzip"
sudo apt install ${PKGS}

# Install bazel.
BAZELVER=1.0.0
BAZELSH=bazel-${BAZELVER}-installer-linux-x86_64.sh
BAZELREPO=https://github.com/bazelbuild/bazel
BAZELURL=${BAZELREPO}/releases/download/${BAZELVER}/${BAZELSH}

if [[ $UPGRADE_BAZEL = "1" ]]; then
  echo "=== Forcing reinstall of Bazel"
  sudo rm $(which bazel)
fi

if ! which bazel > /dev/null; then
  echo
  echo "=== Install Bazel build system"
  wget -P /tmp ${BAZELURL}
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
echo "=== Set up SLING Python API"
SLINGPKG=/usr/lib/python3/dist-packages/sling

PIP="sudo -H pip3 --disable-pip-version-check"

if [[ $(${PIP} freeze | grep "sling==") ]]; then
  echo "Removing existing SLING pip package"
  ${PIP} uninstall sling
fi

if [[ -x "${SLINGPKG}" ]]; then
  echo "SLING Python package already installed"
else
  echo "Adding link for SLING Python package"
  sudo ln -s $(realpath python) ${SLINGPKG}
fi

# Install sling command.
CMD="/usr/local/bin/sling"
if [ ! -f  "$CMD" ]; then
  echo "Adding sling command to $CMD"
  SCRIPT="#!$(which python3)\nimport sling.run\nsling.run.main()"
  echo -e $SCRIPT | sudo tee $CMD > /dev/null
  sudo chmod +x $CMD
fi

# Extra Python packages:
#   requests (http download)
#   numpy (myelin tests)
#   tweepy (twitter)
#   praw (reddit)
#   urllib3 (proxy)
#   pillow (photo dedup)

# Done.
echo
echo "=== SLING is now set up."


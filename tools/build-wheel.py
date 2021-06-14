# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Create a Python Wheel for installing SLING API."""

# The Python wheel produced by this script can be installed with the following
# command:
#
#   sudo -H pip install /wheel/sling-2.0.0-py3-none-linux_x86_64.whl
#
# If you are developing the SLING system, it is convenient to just add a
# link to the SLING repository directly from the Python package directory
# instead:
#
#   sudo ln -s $(realpath python) /usr/lib/python3/dist-packages/sling

import os
import sys
import hashlib
import base64
import zipfile
import distutils.util;
import sling

def sha256_checksum(filename, block_size=65536):
  """ Compute SHA256 digest for file."""
  sha256 = hashlib.sha256()
  with open(filename, 'rb') as f:
    for block in iter(lambda: f.read(block_size), b''):
      sha256.update(block)
  return base64.urlsafe_b64encode(sha256.digest()).rstrip(b'=')

def sha256_content_checksum(data):
  """ Compute SHA256 digest for data content."""
  sha256 = hashlib.sha256()
  sha256.update(data)
  return base64.urlsafe_b64encode(sha256.digest()).rstrip(b'=')

# Python version.
pyversion = str(sys.version_info.major)
abi = "none"

# Wheel package information.
platform = distutils.util.get_platform().replace("-", "_")
tag = "py" + pyversion + "-" + abi + "-" + platform
package = "sling"
version = sling.VERSION
dist_dir = package + "-" + version + ".dist-info"
purelib_dir = package + "-" + version + ".data/purelib"
scripts_dir = package + "-" + version + ".data/scripts"
record_filename = dist_dir + "/RECORD"

wheel_dir = "data/e/dist"
wheel_basename = package + "-" + version + "-" + tag + ".whl"
wheel_filename = wheel_dir + "/" + wheel_basename

# Files to distribute in wheel.

files = {
  'bazel-bin/sling/pyapi/pysling.so': '$PURELIB$/sling/pysling.so',
  'python/script': '$SCRIPTS$/sling',
  'bin/codex': '$SCRIPTS$/codex',
}

pyfiles = [
  '__init__.py',
  '__main__.py',
  'run.py',
  'flags.py',
  'log.py',
  'util.py',

  'myelin/__init__.py',
  'myelin/builder.py',
  'myelin/flow.py',
  'myelin/nn.py',
  'myelin/tf.py',

  'nlp/__init__.py',
  'nlp/document.py',
  'nlp/embedding.py',
  'nlp/parser.py',
  'nlp/silver.py',

  'task/__init__.py',
  'task/alias.py',
  'task/corpora.py',
  'task/data.py',
  'task/download.py',
  'task/kb.py',
  'task/wikidata.py',
  'task/wikipedia.py',
  'task/workflow.py',

  'media/__init__.py',
  'media/wikimedia.py',
  'media/twitterprofiles.py',
]

for f in pyfiles:
  files['python/' + f] = '$PURELIB$/sling/' + f

# Create new wheel zip archive.
os.makedirs(wheel_dir, exist_ok=True)
wheel = zipfile.ZipFile(wheel_filename, "w")
record = ""

# Write wheel metadata.
wheel_metadata_filename = dist_dir + "/WHEEL"
wheel_metadata = """Wheel-Version: 1.0
Root-Is-Purelib: false
Tag: $TAG$""".replace("$TAG$", tag).encode()

record += wheel_metadata_filename + ",sha256=" + \
          sha256_content_checksum(wheel_metadata).decode() + "," + \
          str(len(wheel_metadata)) + "\n"
wheel.writestr(wheel_metadata_filename, wheel_metadata)

# Write package metadata.
package_metadata_filename = dist_dir + "/METADATA"
package_metadata = """Metadata-Version: 2.0
Name: sling
Version: $VERSION$
Summary: SLING frame semantic parsing framework
Home-page: https://github.com/ringgaard/sling
Author: Michael Ringgaard
Author-email: michael@ringgaard.com
License: Apache 2.0
Download-URL: https://github.com/ringgaard/sling/releases
Platform: UNKNOWN
Classifier: Programming Language :: Python
Classifier: Programming Language :: Python :: $PYVERSION$

SLING frame semantic parsing framework
"""
package_metadata = package_metadata.replace("$VERSION$", version)
package_metadata = package_metadata.replace("$PYVERSION$", pyversion)
package_metadata = package_metadata.encode()

record += package_metadata_filename + ",sha256=" + \
          sha256_content_checksum(package_metadata).decode() + "," + \
          str(len(package_metadata)) + "\n"
wheel.writestr(package_metadata_filename, package_metadata)

# Copy all files to wheel ZIP archive.
for source in files:
  # Get source and destination file names.
  destination = files[source]
  destination = destination.replace("$INFO$", dist_dir)
  destination = destination.replace("$PURELIB$", purelib_dir)
  destination = destination.replace("$SCRIPTS$", scripts_dir)
  print("Install", source, "as", destination)

  # Write entry to RECORD file.
  size = os.path.getsize(source)
  checksum = sha256_checksum(source.encode()).decode()
  record += destination + ",sha256=" + checksum + "," + str(size)  + "\n"

  # Add file to wheel zip archive.
  wheel.write(source, destination)

# Add RECORD file to wheel.
print("Add", record_filename)
wheel.writestr(record_filename, record)

# Done.
wheel.close()
print("Wheel written to", wheel_filename)


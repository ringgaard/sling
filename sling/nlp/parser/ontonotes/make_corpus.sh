#/bin/bash
#
# Copyright 2018 Google Inc.
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

# Script for downloading CoNLL OntoNotes 5 data and converting it to a SLING
# corpus that can be used for training and testing a SLING parser.
#
# The LDC2013T19 OntoNotes 5 corpus is needed for the conversion. This is
# licensed by LDC and you need an LDC license to get the corpus:
#   https://catalog.ldc.upenn.edu/LDC2013T19
#
# LDC2013T19.tar.gz is assumed to be in local/data/corpora/ontonotes.
#
# The OntoNotes SLING corpus will end up in local/data/corpora/caspar.

set -e

pushd local/data/corpora/ontonotes

echo "Check that OntoNotes 5 corpus is present"
if [ -f "LDC2013T19.tar.gz" ] ; then
  echo "OntoNotes 5 corpus present"
else
  echo "OntoNotes 5 corpus not found"
  echo "OntoNotes 5 can be obtained from LDC if you have a LDC license"
  echo "See: https://catalog.ldc.upenn.edu/LDC2013T19"
  exit 1
fi

echo "Unpack OntoNotes 5"
tar -xvf LDC2013T19.tar.gz

echo "Download and unpack the CoNLL formated OntoNotes 5 data"
wget https://github.com/ontonotes/conll-formatted-ontonotes-5.0/archive/v12.tar.gz
tar -xvf v12.tar.gz --strip-components=1

wget -O train.ids http://ontonotes.cemantix.org/download/english-ontonotes-5.0-train-document-ids.txt
wget -O dev.ids http://ontonotes.cemantix.org/download/english-ontonotes-5.0-development-document-ids.txt
wget -O test.ids http://ontonotes.cemantix.org/download/english-ontonotes-5.0-test-document-ids.txt

wget http://ontonotes.cemantix.org/download/conll-formatted-ontonotes-5.0-scripts.tar.gz
tar -xvf conll-formatted-ontonotes-5.0-scripts.tar.gz

echo "Generate CoNLL files"
./conll-formatted-ontonotes-5.0/scripts/skeleton2conll.sh -D ontonotes-release-5.0/data/files/data/ conll-formatted-ontonotes-5.0/

popd

echo "Convert CoNLL files to SLING"

mkdir -p local/data/corpora/caspar
python sling/nlp/parser/ontonotes/ontonotesv5_to_sling.py --input=local/data/corpora/ontonotes/conll-formatted-ontonotes-5.0/data/train/data/english/annotations/ --allowed_ids=local/data/corpora/ontonotes/train.ids  --output=local/data/corpora/caspar/train.rec
python sling/nlp/parser/ontonotes/ontonotesv5_to_sling.py --input=local/data/corpora/ontonotes/conll-formatted-ontonotes-5.0/data/development/data/english/annotations/ --allowed_ids=local/data/corpora/ontonotes/dev.ids  --output=local/data/corpora/caspar/dev.rec
python sling/nlp/parser/ontonotes/ontonotesv5_to_sling.py --input=local/data/corpora/ontonotes/conll-formatted-ontonotes-5.0/data/test/data/english/annotations/ --allowed_ids=local/data/corpora/ontonotes/test.ids  --output=local/data/corpora/caspar/test.rec

echo "Done."

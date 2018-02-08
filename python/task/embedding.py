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

"""Workflow builder for embedding processing"""

from workflow import *
from wiki import WikiWorkflow
import sling.flags as flags
import corpora

class EmbeddingWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf
    self.wiki = WikiWorkflow(wf=wf)

  #---------------------------------------------------------------------------
  # Word embeddings
  #---------------------------------------------------------------------------

  def vocabulary(self, language=None):
    """Resource for word embedding vocabulary. This is a text map with
    (normalized) words and counts.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("word-vocabulary.map",
                            dir=corpora.wikidir(language),
                            format="textmap/word")

  def extract_vocabulary(self, documents=None, output=None, language=None):
    if language == None: language = flags.arg.language
    if documents == None: documents = self.wiki.wikipedia_documents(language)
    if output == None: output = self.vocabulary(language)

    with self.wf.namespace(language + "-vocabulary"):
      return self.wf.mapreduce(documents, output,
                               format="message/word:count",
                               mapper="embedding-vocabulary-mapper",
                               reducer="embedding-vocabulary-reducer")


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

  def word_embeddings(self, language=None):
    """Resource for word embeddings in word2vec embedding format."""
    if language == None: language = flags.arg.language
    return self.wf.resource("word-embeddings.vec",
                            dir=corpora.wikidir(language),
                            format="embeddings")

  def extract_vocabulary(self, documents=None, output=None, language=None):
    if language == None: language = flags.arg.language
    if documents == None: documents = self.wiki.wikipedia_documents(language)
    if output == None: output = self.vocabulary(language)

    with self.wf.namespace(language + "-vocabulary"):
      return self.wf.mapreduce(documents, output,
                               format="message/word:count",
                               mapper="word-embeddings-vocabulary-mapper",
                               reducer="word-embeddings-vocabulary-reducer")

  def train_word_embeddings(self, documents=None, vocabulary=None, output=None,
                            language=None):
    if language == None: language = flags.arg.language
    if documents == None: documents = self.wiki.wikipedia_documents(language)
    if vocabulary == None: vocabulary = self.vocabulary(language)
    if output == None: output = self.word_embeddings(language)

    with self.wf.namespace(language + "-word-embeddings"):
      trainer = self.wf.task("word-embeddings-trainer")
      trainer.add_params({
        "iterations" : 5,
        "negative": 5,
        "window": 5,
        "learning_rate": 0.025,
        "min_learning_rate": 0.0001,
        "embedding_dims": 32,
        "subsampling": 1e-3,
      })
      trainer.attach_input("documents", documents)
      trainer.attach_input("vocabulary", vocabulary)
      trainer.attach_output("output", output)
      return output

  #---------------------------------------------------------------------------
  # Fact and category embeddings
  #---------------------------------------------------------------------------

  def fact_dir(self):
    return flags.arg.workdir + "/fact"

  def fact_lexicon(self):
    """Resource for fact vocabulary (text map with fact paths and counts."""
    return self.wf.resource("facts.map",
                            dir=self.fact_dir(),
                            format="textmap/fact")
  def category_lexicon(self):
    """Resource for category vocabulary (text map with categories and counts."""
    return self.wf.resource("categories.map",
                            dir=self.fact_dir(),
                            format="textmap/category")

  def extract_fact_lexicon(self):
    kb = self.wiki.knowledge_base()
    factmap = self.fact_lexicon()
    catmap = self.category_lexicon()
    with self.wf.namespace("fact-embeddings"):
      trainer = self.wf.task("fact-lexicon-extractor")
      trainer.attach_input("kb", kb)
      trainer.attach_output("factmap", factmap)
      trainer.attach_output("catmap", catmap)
      return factmap, catmap

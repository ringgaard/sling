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

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task.workflow import *

class EmbeddingWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

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
    if documents == None: documents = self.data.wikipedia_documents(language)
    if output == None: output = self.vocabulary(language)

    with self.wf.namespace(language + "-vocabulary"):
      return self.wf.mapreduce(documents, output,
                               format="message/word:count",
                               mapper="word-vocabulary-mapper",
                               reducer="word-vocabulary-reducer",
                               params={"normalization": "d"})

  def train_word_embeddings(self, documents=None, vocabulary=None, output=None,
                            language=None):
    """Train word embeddings."""
    if language == None: language = flags.arg.language
    if documents == None: documents = self.data.wikipedia_documents(language)
    if vocabulary == None: vocabulary = self.vocabulary(language)
    if output == None: output = self.word_embeddings(language)

    with self.wf.namespace(language + "-word-embeddings"):
      trainer = self.wf.task("word-embeddings-trainer")
      trainer.add_params({
        "iterations" : 1,
        "negative": 300,
        "window": 5,
        "learning_rate": 0.025,
        "min_learning_rate": 0.0001,
        "embedding_dims": 32,
        "subsampling": 1e-3,
        "normalization": "d",
      })
      trainer.attach_input("documents", documents)
      trainer.attach_input("vocabulary", vocabulary)
      trainer.attach_output("output", output)
      return output

  #---------------------------------------------------------------------------
  # Fact and category embeddings
  #---------------------------------------------------------------------------

  def factdir(self):
    return corpora.workdir("fact")

  def fact_lexicon(self):
    """Resource for fact vocabulary (text map with fact paths and counts."""
    return self.wf.resource("facts.map",
                            dir=self.factdir(),
                            format="textmap/fact")
  def category_lexicon(self):
    """Resource for category vocabulary (text map with categories and counts."""
    return self.wf.resource("categories.map",
                            dir=self.factdir(),
                            format="textmap/category")

  def facts(self):
    """Resource for resolved facts."""
    return self.wf.resource("facts.rec",
                            dir=self.factdir(),
                            format="records/fact")

  def fact_embeddings(self):
    """Resource for fact embeddings in word2vec embedding format."""
    return self.wf.resource("fact-embeddings.vec",
                            dir=self.factdir(),
                            format="embeddings")

  def category_embeddings(self):
    """Resource for category embeddings in word2vec embedding format."""
    return self.wf.resource("category-embeddings.vec",
                            dir=self.factdir(),
                            format="embeddings")

  def extract_fact_lexicon(self):
    """Build fact and category lexicons."""
    kb = self.data.knowledge_base()
    factmap = self.fact_lexicon()
    catmap = self.category_lexicon()
    with self.wf.namespace("fact-embeddings"):
      trainer = self.wf.task("fact-lexicon-extractor")
      trainer.attach_input("kb", kb)
      trainer.attach_output("factmap", factmap)
      trainer.attach_output("catmap", catmap)
      return factmap, catmap

  def extract_facts(self):
    """Extract facts for items in the knowledge base."""
    kb = self.data.knowledge_base()
    factmap = self.fact_lexicon()
    catmap = self.category_lexicon()
    output = self.facts()
    with self.wf.namespace("fact-embeddings"):
      extractor = self.wf.task("fact-extractor")
      extractor.attach_input("kb", kb)
      extractor.attach_input("factmap", factmap)
      extractor.attach_input("catmap", catmap)
      facts = self.wf.channel(extractor, format="message/frame")
      return self.wf.write(facts, output, name="fact-writer")

  def train_fact_embeddings(self):
    """Train fact and category embeddings."""
    facts = self.facts()
    factmap = self.fact_lexicon()
    catmap = self.category_lexicon()
    fact_embeddings = self.fact_embeddings()
    category_embeddings = self.category_embeddings()
    with self.wf.namespace("fact-embeddings"):
      trainer = self.wf.task("fact-embeddings-trainer")
      trainer.add_params({
        "batch_size": 256,
        "batches_per_update": 32,
        "embedding_dims": 256,
        "normalize": False,
        "epochs" : 100000,
        "report_interval": 250,
        "learning_rate": 1.0,
        "learning_rate_decay": 0.95,
        "rampup": 120,
        "clipping": 1,
        "optimizer": "sgd",
      })
      self.wf.connect(self.wf.read(facts, name="fact-reader"), trainer)
      trainer.attach_input("factmap", factmap)
      trainer.attach_input("catmap", catmap)
      trainer.attach_output("factvecs", fact_embeddings)
      trainer.attach_output("catvecs", category_embeddings)
    return fact_embeddings, category_embeddings

  #---------------------------------------------------------------------------
  # Fact plausibility model
  #---------------------------------------------------------------------------

  def fact_plausibility_model(self):
    """Resource for fact plausibility model."""
    return self.wf.resource("plausibility.flow",
                            dir=self.factdir(),
                            format="flow")

  def train_fact_plausibility(self):
    """Train fact plausibility model."""
    facts = self.facts()
    factmap = self.fact_lexicon()
    model = self.fact_plausibility_model()
    with self.wf.namespace("fact-plausibility"):
      trainer = self.wf.task("fact-plausibility-trainer")
      trainer.add_params({
        "batch_size": 4,
        "batches_per_update": 256,
        "min_facts": 4,
        "embedding_dims": 128,
        "epochs" : 250000,
        "report_interval": 1000,
        "checkpoint_interval": 50000,
        "learning_rate": 1.0,
        "min_learning_rate": 0.001,
        "learning_rate_decay": 0.95,
        "clipping": 1,
        "optimizer": "sgd",
        "rampup": 5 * 60,
      })
      self.wf.connect(self.wf.read(facts, name="fact-reader"), trainer)
      trainer.attach_input("factmap", factmap)
      trainer.attach_output("model", model)
    return model

# Commands.

def extract_vocabulary():
  # Extract vocabulary for word embeddings.
  for language in flags.arg.languages:
    log.info("Extract " + language + " vocabulary")
    wf = EmbeddingWorkflow(language + "-vocabulary")
    wf.extract_vocabulary(language=language)
    run(wf.wf)

def train_word_embeddings():
  # Train word embeddings.
  for language in flags.arg.languages:
    log.info("Train " + language + " word embeddings")
    wf = EmbeddingWorkflow(language + "-word-embeddings")
    wf.train_word_embeddings(language=language)
    run(wf.wf)

def extract_fact_lexicon():
  # Extract vocabulary for fact and category embeddings.
  log.info("Extract fact and category lexicons")
  wf = EmbeddingWorkflow("fact-lexicon")
  wf.extract_fact_lexicon()
  run(wf.wf)

def extract_facts():
  # Extract facts from knowledge base.
  log.info("Extract facts from knowledge base")
  wf = EmbeddingWorkflow("fact-extraction")
  wf.extract_facts()
  run(wf.wf)

def train_fact_embeddings():
  # Train fact and category embeddings.
  log.info("Train fact and category embeddings")
  wf = EmbeddingWorkflow("fact-embeddings")
  wf.train_fact_embeddings()
  run(wf.wf)

def train_fact_plausibility():
  # Train fact plausibility model.
  log.info("Train fact plausibility model")
  wf = EmbeddingWorkflow("plausibility")
  wf.train_fact_plausibility()
  run(wf.wf)


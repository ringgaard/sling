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

"""Workflow for silver-labeling of Wikipedia articles"""

import os
import sling.flags as flags
import sling.task.corpora as corpora
from sling.task import *
from sling.task.wiki import WikiWorkflow

flags.define("--decoder",
             help="parser decoder type",
             default="knolex")

flags.define("--simple_types",
             help="use simple commons store with basic types",
             default=False,
             action="store_true")

flags.define("--subwords",
             help="use subword tokenization",
             default=False,
             action="store_true")

class SilverWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf
    self.wiki = WikiWorkflow(wf=wf)

  def workdir(self, language=None):
    if language == None:
      return flags.arg.workdir + "/silver"
    else:
      return flags.arg.workdir + "/silver/" + language

  #---------------------------------------------------------------------------
  # IDF table
  #---------------------------------------------------------------------------

  def idftable(self, language=None):
    """Resource for IDF table."""
    if language == None: language = flags.arg.language
    return self.wf.resource("idf.repo",
                            dir=self.workdir(language),
                            format="repository")

  def build_idf(self, language=None):
    # Build IDF table from Wikipedia.
    if language == None: language = flags.arg.language
    documents = self.wiki.wikipedia_documents(language)

    with self.wf.namespace(language + "-idf"):
      # Collect words.
      wordcounts = self.wf.shuffle(
        self.wf.map(documents, "vocabulary-mapper", format="message/count",
                    params={
                      "min_document_length": 200,
                      "only_lowercase": True
                    })
      )

      # Build IDF table.
      builder = self.wf.task("idf-table-builder", params={"threshold": 30})
      self.wf.connect(wordcounts, builder)
      builder.attach_output("repository", self.idftable(language))

  #---------------------------------------------------------------------------
  # Silver-labeled training and evaluation documents
  #---------------------------------------------------------------------------

  def training_documents(self, language=None):
    """Resource for silver-labeled training documents."""
    if language == None: language = flags.arg.language
    return self.wf.resource("train@10.rec",
                            dir=self.workdir(language),
                            format="records/document")

  def evaluation_documents(self, language=None):
    """Resource for silver-labeled evaluation documents."""
    if language == None: language = flags.arg.language
    return self.wf.resource("eval.rec",
                            dir=self.workdir(language),
                            format="records/document")

  def silver_annotation(self, docs=None, language=None):
    if language == None: language = flags.arg.language
    if docs == None: docs = self.wiki.wikipedia_documents(language)
    train_docs = self.training_documents(language)
    eval_docs = self.evaluation_documents(language)
    phrases = corpora.repository("data/wiki/" + language) + "/phrases.txt"

    with self.wf.namespace(language + "-silver"):
      # Map document through silver annotation pipeline and split corpus.
      mapper = self.wf.task("corpus-split", "labeler")

      mapper.add_annotator("mentions")
      mapper.add_annotator("anaphora")
      #mapper.add_annotator("phrase-structure")
      mapper.add_annotator("relations")
      mapper.add_annotator("types")
      mapper.add_annotator("clear-references")

      mapper.add_param("resolve", True)
      mapper.add_param("language", language)
      mapper.add_param("initial_reference", False)
      mapper.add_param("definite_reference", False)
      mapper.add_param("split_ratio", 5000)

      mapper.attach_input("commons", self.wiki.knowledge_base())
      mapper.attach_input("aliases", self.wiki.phrase_table(language))
      mapper.attach_input("dictionary", self.idftable(language))

      self.wf.connect(self.wf.read(docs), mapper, name="docs")
      train_channel = self.wf.channel(mapper, name="train",
                                      format="message/document")
      eval_channel = self.wf.channel(mapper, name="eval",
                                     format="message/document")

      # Write shuffled training documents.
      train_shards = length_of(train_docs)
      train_shuffled = self.wf.shuffle(train_channel,
                                       shards=train_shards,
                                       bufsize=256 * 1024 * 1024)
      self.wf.write(train_shuffled, train_docs, name="train")

      # Write evaluation documents.
      self.wf.write(eval_channel, eval_docs, name="eval")

    return train_docs, eval_docs

  #---------------------------------------------------------------------------
  # Vocabulary
  #---------------------------------------------------------------------------

  def vocabulary(self, language=None):
    """Resource for word vocabulary. This is a text map with (normalized) words
    and counts.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("vocabulary.map",
                            dir=self.workdir(language),
                            format="textmap/word")

  def subwords(self, language=None):
    """Resource for subword vocabulary. This is a text map with (normalized)
    subwords and counts.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("subwords.map",
                            dir=self.workdir(language),
                            format="textmap/subword")

  def extract_vocabulary(self, documents=None, output=None, language=None):
    if language == None: language = flags.arg.language
    if documents == None: documents = self.training_documents(language)
    if output == None: output = self.vocabulary(language)

    with self.wf.namespace(language + "-vocabulary"):
      # Extract words from documents.
      words = self.wf.shuffle(
        self.wf.map(documents, "word-vocabulary-mapper",
                    format="message/word:count",
                    params={
                      "normalization": "l",
                      "skip_section_titles": True,
                    }))

      # Build vocabulary from words in documents.
      vocab = self.wf.reduce(words, output, "word-vocabulary-reducer")
      vocab.add_param("min_freq", 100)
      vocab.add_param("max_words", 100000)

      # Also produce subword vocabulary if requested
      if flags.arg.subwords:
        vocab.add_param("max_subwords", 50000)
        subwords = self.wf.channel(vocab, name="subwords",
                                   format="message/word")
        self.wf.write(subwords, self.subwords(language))

    return output

  #---------------------------------------------------------------------------
  # Parser training
  #---------------------------------------------------------------------------

  def parser_model(self, arch, language=None):
    """Resource for parser model."""
    if language == None: language = flags.arg.language
    return self.wf.resource(arch + ".flow",
                            dir=self.workdir(language),
                            format="flow")

  def train_parser(self, language=None):
    if language == None: language = flags.arg.language
    with self.wf.namespace(language + "-parser"):
      # Parser trainer task.
      params = {
        "normalization": "l",

        "rnn_type": 1,
        "rnn_dim": 128,
        "rnn_highways": True,
        "rnn_layers": 1,
        "rnn_bidir": True,
        "dropout": 0.2,

        "skip_section_titles": True,
        "learning_rate": 0.5,
        "learning_rate_decay": 0.9,
        "clipping": 1,
        "local_clipping": True,
        "optimizer": "sgd",
        "batch_size": 64,
        "warmup": 20 * 60,
        "rampup": 5 * 60,
        "report_interval": 100,
        "learning_rate_cliff": 9000,
        "epochs": 10000,
        "checkpoint_interval": 1000,
      }

      if flags.arg.subwords:
        params["encoder"] = "subrnn"
        params["subword_dim"] = 64
      else:
        params["encoder"] = "lexrnn"
        params["word_dim"] = 64

      if flags.arg.decoder == "knolex":
        params["decoder"] = "knolex"
        params["link_dim_token"] = 64
        params["ff_l2reg"] = 0.0001
      elif flags.arg.decoder == "bio":
        params["decoder"] = "bio"
        params["ff_dims"] = [128]
      elif flags.arg.decoder == "crf":
        params["decoder"] = "bio"
        params["crf"] = True
        params["ff_dims"] = [128]
      elif flags.arg.decoder == "biaffine":
        params["decoder"] = "biaffine"
        params["ff_dims"] = [64]
        params["ff_dropout"] = 0.2
      else:
        params["decoder"] = flags.arg.decoder

      trainer = self.wf.task("parser-trainer", params=params)

      # Inputs.
      if flags.arg.simple_types:
        kb = self.wf.resource("data/dev/types.sling", format="store/frame")
      else:
        kb = self.wiki.knowledge_base()

      trainer.attach_input("commons", kb)
      trainer.attach_input("training_corpus",
                           self.training_documents(language))
      trainer.attach_input("evaluation_corpus",
                           self.evaluation_documents(language))
      trainer.attach_input("vocabulary", self.vocabulary(language))
      if flags.arg.subwords:
        trainer.attach_input("subwords", self.subwords(language))

      # Parser model.
      model = self.parser_model(flags.arg.decoder, language)
      trainer.attach_output("model", model)

    return model

# Commands.

def build_idf():
  # Extract IDF table.
  wf = SilverWorkflow("idf-table")
  for language in flags.arg.languages:
    log.info("Build " + language + " IDF table")
    wf.build_idf(language=language)
  run(wf.wf)

def silver_annotation():
  # Run silver-labeling of Wikipedia documents.
  for language in flags.arg.languages:
    log.info("Silver-label " + language + " wikipedia")
    wf = SilverWorkflow(language + "-silver")
    wf.silver_annotation(language=language)
    run(wf.wf)

def extract_parser_vocabulary():
  # Extract vocabulary for parser.
  for language in flags.arg.languages:
    log.info("Extract " + language + " parser vocabulary")
    wf = SilverWorkflow(language + "-parser-vocabulary")
    wf.extract_vocabulary(language=language)
    run(wf.wf)

def train_parser():
  # Train parser on silver data.
  for language in flags.arg.languages:
    log.info("Train " + language + " parser")
    wf = SilverWorkflow(language + "-parser")
    wf.train_parser(language=language)
    run(wf.wf)


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
      mapper.add_annotator("types")
      mapper.add_annotator("clear-references")

      mapper.add_param("resolve", True)
      mapper.add_param("language", language)
      mapper.add_param("detailed", False)
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
      train_shuffled = self.wf.shuffle(train_channel, shards=train_shards)
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

  def extract_vocabulary(self, documents=None, output=None, language=None):
    if language == None: language = flags.arg.language
    if documents == None: documents = self.training_documents(language)
    if output == None: output = self.vocabulary(language)

    with self.wf.namespace(language + "-vocabulary"):
      return self.wf.mapreduce(documents, output,
                               format="message/word:count",
                               mapper="word-vocabulary-mapper",
                               reducer="word-vocabulary-reducer",
                               params={
                                 "normalization": "d",
                                 "min_freq": 100,
                                 "max_words": 50000,
                                 "skip_section_titles": True,
                               })


# Copyright 2021 Ringgaard Research ApS
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

"""Datasets shared across workflows."""

import sling.flags as flags
import sling.task.corpora as corpora
from sling.task import *

flags.define("--extra_items",
             help="additional items with info",
             default=None,
             metavar="RECFILES")

class Datasets:
  def __init__(self, wf):
    self.wf = wf

  #---------------------------------------------------------------------------
  # Repository
  #---------------------------------------------------------------------------

  def catalog_defs(self):
    """Resource for global catalog definitions."""
    return self.wf.resource("catalog.sling",
                            dir=corpora.repository("data/nlp/schemas"),
                            format="store/frame")

  def meta_schema_defs(self):
    """Resource for meta schema definitions."""
    return self.wf.resource("meta-schema.sling",
                            dir=corpora.repository("data/nlp/schemas"),
                            format="store/frame")

  def document_schema_defs(self):
    """Resource for document schema definitions."""
    return self.wf.resource("document-schema.sling",
                            dir=corpora.repository("data/nlp/schemas"),
                            format="store/frame")

  def language_defs(self):
    """Resource for language definitions. This defines the /lang/<lang>
    symbols and has meta information for each language."""
    return self.wf.resource("languages.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def calendar_defs(self):
    """Resource for calendar definitions."""
    return self.wf.resource("calendar.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def country_defs(self):
    """Resource for country definitions."""
    return self.wf.resource("countries.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def unit_defs(self):
    """Resource for calendar definitions."""
    return self.wf.resource("units.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def wikidata_defs(self):
    """Resource for Wikidata schema definitions."""
    return self.wf.resource("wikidata.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def wikipedia_defs(self):
    """Resource for Wikipedia schema definitions."""
    return self.wf.resource("wikipedia.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def schema_defs(self):
    """Resources for schemas included in knowledge base."""
    return self.wf.bundle(
      self.catalog_defs(),
      self.meta_schema_defs(),
      self.document_schema_defs(),
      self.language_defs(),
      self.calendar_defs(),
      self.country_defs(),
      self.unit_defs(),
      self.wikidata_defs(),
      self.wikipedia_defs())

  def custom_properties(self):
    """Resource for custom SLING knowledge base properties."""
    return self.wf.resource("custom-properties.sling",
                            dir=corpora.repository("data/nlp/schemas"),
                            format="store/frame")

  #---------------------------------------------------------------------------
  # Wikidata
  #---------------------------------------------------------------------------

  def wikidata_items(self):
    """Resource for Wikidata items. This is a set of record files where each
    Wikidata item is represented as a frame:
      <qid>: {
        =<qid>
        :/w/item
        name: "..."
        description: "..."
        alias: {
          name: "..."
          lang: /lang/<lang>
          sources: ...
        }
        ...
        /w/wikipedia: {
          /lang/<lang>: "<wikipedia article title>"
          ...
       }
       ... properties
      }

      <qid>: Wikidata item id (Q<item number>, e.g. Q35)
      <pid>: Wikidata property id (P<property number>, e.g. P31)
    """
    return self.wf.resource("wikidata-items@10.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikidata_redirects(self):
    """Resource for Wikidata redirects. This is a set of record files where each
    Wikidata redirect is represented as a frame:
      <qid>: {
        =<qid>
        +<redirect>
      }
    """
    return self.wf.resource("wikidata-redirects.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikidata_properties(self):
    """Resource for Wikidata properties. This is a record file where each
    Wikidata property is represented as a frame.
      <pid>: {
        =<pid>
        :/w/property
        name: "..."
        description: "..."
        /w/datatype: ...
        ... properties ...
      }
    """
    return self.wf.resource("properties.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def fanin(self):
    """Resource for item fan-in, i.e. the number of times an item is a target
    in a relation."""
    return self.wf.resource("fanin.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  #---------------------------------------------------------------------------
  # Wikipedia
  #---------------------------------------------------------------------------

  def wikipedia_documents(self, language=None):
    """Resource for parsed Wikipedia documents. This is a set of record files
    with one record per article, where the text has been extracted from the
    wiki markup and tokenized. The documents also contain additional
    structured information (e.g. categories and infoboxes) and mentions for
    links to other Wikipedia pages:
      <title>: {
        :/wp/page
        /wp/page/pageid: ...
        /wp/page/title: "..."
        lang: /lang/<lang>
        /wp/page/text: "<Wikipedia page in wiki markup format>"
        /wp/page/qid: <qid>
        :document
        url: "http://<lang>.wikipedia.org/wiki/<name>"
        title: "..."
        text: "<clear text extracted from wiki markup>"
        tokens: [...]
        mention: {
          :/wp/link
          begin: ...
          length: ...
          evokes: <qid>
        }
        ...
        /wp/page/category: <qid>
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("documents@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/document")

  def wikipedia_summaries(self, language=None):
    """Resource for Wikipedia document summaries."""
    return self.wf.resource("summaries.rec",
                            dir=corpora.wikidir(language),
                            format="records/document")

  def wikipedia_aliases(self, language=None):
    """Resource for wikipedia aliases. The aliases are extracted from the
    Wikipedia pages from anchors, redirects, disambiguation pages etc. This is
    a set of record files with a SLING frame record for each item:
      <qid>: {
        alias: {+"<alias>"@/lang/xx
          sources: ...
          count: ...
        }
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("aliases@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/alias")

  def wikipedia_items(self):
    """Resource for item data from Wikipedias."""
    return self.wf.resource("wikipedia-items.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikipedia_members(self):
    """Resource for members of categories."""
    return self.wf.resource("wikipedia-members.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikilinks(self):
    """Resource for link graph."""
    return self.wf.resource("links@10.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def popularity(self):
    """Resource for item popularity."""
    return self.wf.resource("popularity.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  #---------------------------------------------------------------------------
  # Knowledge base
  #---------------------------------------------------------------------------

  def properties(self):
    """Resources for knowledge base properties."""
    return self.wf.bundle(
      self.wikidata_properties(),
      self.custom_properties())

  def standard_item_sources(self):
    items = self.wf.bundle(
              self.properties(),
              self.wikidata_items(),
              self.wikilinks(),
              self.popularity(),
              self.fanin(),
              self.wikipedia_items(),
              self.wikipedia_members())

    if flags.arg.extra_items:
      extra = self.wf.resource(flags.arg.extra_items, format="records/frame")
      items = self.wf.bundle(items, extra)

    return items

  def items(self):
    """Resource for reconciled items. This is a set of record files where each
    item is represented as a frame.
    """
    return self.wf.resource("items@10.rec",
                            dir=corpora.kbdir(),
                            format="records/frame")

  def knowledge_base(self):
    """Resource for knowledge base. This is a SLING frame store with frames for
    each Wikidata item and property plus additional schema information.
    """
    return self.wf.resource("kb.sling",
                            dir=corpora.kbdir(),
                            format="store/frame")

  #---------------------------------------------------------------------------
  # Aliases
  #---------------------------------------------------------------------------

  def name_table(self, language=None):
    """Resource for item name table. This is a repository with all the names
    and the items they are aliases for."""
    if language == None: language = flags.arg.language
    return self.wf.resource("name-table.repo",
                            dir=corpora.kbdir(language),
                            format="repository")

  def phrase_table(self, language=None):
    """Resource for item name phrase table. This is a repository with phrase
    fingerprints of the item names."""
    if language == None: language = flags.arg.language
    return self.wf.resource("phrase-table.repo",
                            dir=corpora.kbdir(language),
                            format="repository")


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

"""Workflow for importing and processing Wikidata."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

class WikipediaWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  #---------------------------------------------------------------------------
  # Wikipedia import
  #---------------------------------------------------------------------------

  def wikipedia_dump(self, language=None):
    """Resource for wikipedia dump. This can be downloaded from wikimedia.org
    and contrains a full dump of Wikipedia in a particular language. This is
    in XML format with the articles in Wiki markup format."""
    if language == None: language = flags.arg.language
    return self.wf.resource(corpora.wikipedia_dump(language),
                            format="xml/wikipage")

  def wikipedia_articles(self, language=None):
    """Resource for wikipedia articles. This is a set of record files where each
    Wikipedia article is encoded as a SLING document.
      <wid>: {
        =<wid>
        :/wp/page
        /wp/page/pageid: ...
        /wp/page/title: "..."
        lang: /lang/<lang>
        /wp/page/text: "<Wikipedia page in Wiki markup format>"
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("articles@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/frame")

  def wikipedia_categories(self, language=None):
    """Resource for wikipedia categories. This is a set of record files where
    each Wikipedia article is encoded as a SLING document.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("categories@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/frame")

  def wikipedia_redirects(self, language=None):
    """Resource for wikidata redirects. This is encoded as a SLING frame store
    where each redirect is a SLING frame.
      {
        =<wid for redirect page>
        :/wp/redirect
        /wp/redirect/pageid: ...
        /wp/redirect/title: "..."
        /wp/redirect/link: <wid for target page>
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("redirects.sling",
                            dir=corpora.wikidir(language),
                            format="store/frame")

  def wikipedia_import(self, dump=None, language=None):
    """Task for importing Wikipedia dump as SLING articles and redirects."""
    if language == None: language = flags.arg.language
    if dump == None: dump = self.wikipedia_dump(language)
    with self.wf.namespace(language + "-wikipedia"):
      # Import Wikipedia dump and convert to SLING format.
      task = self.wf.task("wikipedia-importer")
      task.attach_input("input", dump)
      articles = self.wf.channel(task, name="articles",
                                 format="message/frame")
      categories = self.wf.channel(task, name="categories",
                                   format="message/frame")
      redirects = self.wf.channel(task, name="redirects",
                                  format="message/frame")

      # Write articles.
      articles_output = self.wikipedia_articles(language)
      self.wf.write(articles, articles_output, name="article-writer")

      # Write categories.
      categories_output = self.wikipedia_categories(language)
      self.wf.write(categories, categories_output, name="category-writer")

      # Write redirects.
      redirects_output = self.wikipedia_redirects(language)
      self.wf.write(redirects, redirects_output, name="redirect-writer")

      return articles_output, categories_output, redirects_output

  #---------------------------------------------------------------------------
  # Wikipedia mapping
  #---------------------------------------------------------------------------

  def wikipedia_mapping(self, language=None):
    """Resource for Wikipedia to Wikidata mapping. This is a SLING frame store
    with one frame per Wikipedia article with information for mapping it to
    Wikidata.
      {
        =<wid>
        /w/item/qid: <qid>
        /w/item/kind: /w/item/kind/...
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("mapping.sling",
                            dir=corpora.wikidir(language),
                            format="store/frame")

  def map_wikipedia(self, language=None, name=None):
    """Build mapping from Wikipedia IDs (<wid>) to Wikidata IDs (<qid>).
    Returns file with frame store for mapping."""
    if language == None: language = flags.arg.language
    wikidata_items = self.data.wikidata_items()

    wiki_mapping = self.wf.map(wikidata_items, "wikipedia-mapping",
                               params={"language": language},
                               name=name)
    output = self.wikipedia_mapping(language)
    self.wf.write(wiki_mapping, output, name="mapping-writer")
    return output

  #---------------------------------------------------------------------------
  # Wikipedia parsing
  #---------------------------------------------------------------------------

  def template_defs(self, language=None):
    """Resource for Wikipedia template definitions."""
    if language == None: language = flags.arg.language
    return self.wf.resource("templates.sling",
                            dir=corpora.repository("data/wiki/" + language),
                            format="store/frame")

  def wikipedia_category_documents(self, language=None):
    """Resource for parsed Wikipedia category documents.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("category-documents@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/document")

  def parse_wikipedia(self, language=None):
    """Parse Wikipedia articles and build alias table."""
    if language == None: language = flags.arg.language
    with self.wf.namespace(language + "-wikipedia-parsing"):
      # Parse Wikipedia articles to SLING documents.
      articles = self.wikipedia_articles(language)
      categories = self.wikipedia_categories(language)
      redirects = self.wikipedia_redirects(language)
      commons = [
        self.data.language_defs(),
        self.data.unit_defs(),
        self.data.calendar_defs(),
        self.data.country_defs(),
        self.template_defs(language),
      ]
      wikimap = self.wikipedia_mapping(language)

      parser = self.wf.task("wikipedia-document-builder", "wikipedia-documents")
      parser.add_param("language", language)
      parser.add_param("skip_tables", True)
      self.wf.connect(self.wf.read(articles, name="article-reader"), parser)
      self.wf.connect(self.wf.read(categories, name="category-reader"), parser)
      parser.attach_input("commons", commons)
      parser.attach_input("wikimap", wikimap)
      parser.attach_input("redirects", redirects)
      documents = self.wf.channel(parser, format="message/document")
      aliases = self.wf.channel(parser, "aliases", format="message/qid:alias")
      catdocs = self.wf.channel(parser, "categories",
                                format="message/qid:alias")

      # Write Wikipedia documents.
      document_output = self.data.wikipedia_documents(language)
      self.wf.write(documents, document_output, name="document-writer",
                    params={"indexed": True})

      # Write Wikipedia category documents.
      category_document_output = self.wikipedia_category_documents(language)
      self.wf.write(catdocs, category_document_output, name="document-writer",
                    params={"indexed": True})

      # Collect aliases.
      alias_output = self.data.wikipedia_aliases(language)
      self.wf.reduce(self.wf.shuffle(aliases, len(alias_output)),
                     alias_output,
                     "wikipedia-alias-reducer",
                     params={'language': language})

    return document_output, category_document_output, alias_output

  #---------------------------------------------------------------------------
  # Wikipedia items
  #---------------------------------------------------------------------------

  def merge_categories(self, languages=None):
    """Merge Wikipedia categories for all languages."""
    if languages == None: languages = flags.arg.languages

    with self.wf.namespace("wikipedia-categories"):
      documents = []
      for language in languages:
        documents.extend(self.data.wikipedia_documents(language))
        documents.extend(self.wikipedia_category_documents(language))
      return self.wf.mapreduce(input=documents,
                               output=self.data.wikipedia_items(),
                               mapper="category-item-extractor",
                               reducer="category-item-merger",
                               format="message/frame")

  def invert_categories(self, languages=None):
    """Invert category membership."""
    if languages == None: languages = flags.arg.languages

    with self.wf.namespace("wikipedia-members"):
      return self.wf.mapreduce(input=self.data.wikipedia_items(),
                               output=self.data.wikipedia_members(),
                               mapper="category-inverter",
                               reducer="category-member-merger",
                               format="message/string",
                               params={"threshold": 100000})

  #---------------------------------------------------------------------------
  # Wikipedia link graph
  #---------------------------------------------------------------------------

  def extract_links(self):
    """Build link graph over all Wikipedias and compute item popularity."""
    documents = []
    for l in flags.arg.languages:
      documents.extend(self.data.wikipedia_documents(l))

    # Extract links from documents.
    mapper = self.wf.task("wikipedia-link-extractor")
    self.wf.connect(self.wf.read(documents), mapper)
    links = self.wf.channel(mapper, format="message/frame",
                            name="output")
    targets = self.wf.channel(mapper, format="message/int",
                              name="target")

    # Reduce links.
    with self.wf.namespace("links"):
      wikilinks = self.data.wikilinks()
      self.wf.reduce(self.wf.shuffle(links, shards=length_of(wikilinks)),
                     wikilinks, "wikipedia-link-merger")

    # Reduce link targets.
    with self.wf.namespace("targets"):
      popularity = self.data.popularity()
      self.wf.reduce(self.wf.shuffle(targets, shards=length_of(popularity)),
                     popularity, "item-popularity-reducer")

    return wikilinks, popularity

def import_wikipedia():
  wf = WikipediaWorkflow("wikipedia-import")
  for language in flags.arg.languages:
    log.info("Import " + language + " wikipedia")
    wf.wikipedia_import(language=language)
  run(wf.wf)

def map_wikipedia():
  wf = WikipediaWorkflow("wikipedia-mapping")
  for language in flags.arg.languages:
    log.info("Map " + language + " wikipedia")
    wf.map_wikipedia(language=language)
  run(wf.wf)

def parse_wikipedia():
  wf = WikipediaWorkflow("wikipedia-parsing")
  for language in flags.arg.languages:
    log.info("Parse " + language + " wikipedia")
    wf.parse_wikipedia(language=language)
  run(wf.wf)

def merge_categories():
  log.info("Merge wikipedia categories")
  wf = WikipediaWorkflow("category-merging")
  wf.merge_categories()
  run(wf.wf)

def invert_categories():
  log.info("Invert wikipedia categories")
  wf = WikipediaWorkflow("category-inversion")
  wf.invert_categories()
  run(wf.wf)

def extract_wikilinks():
  log.info("Extract link graph")
  wf = WikipediaWorkflow("link-graph")
  wf.extract_links()
  run(wf.wf)


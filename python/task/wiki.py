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

"""Workflow for Wikidata and Wikipedia processing"""

import sling.flags as flags
from sling.task import *
import sling.task.corpora as corpora

flags.define("--index",
             help="index wiki data sets",
             default=False,
             action='store_true')

flags.define("--only_primary_language",
             help="only use wikidata labels from primary language",
             default=False,
             action='store_true')

flags.define("--only_known_languages",
             help="only use wikidata labels from known languages",
             default=False,
             action='store_true')

flags.define("--skip_wikipedia_mapping",
             help="skip wikipedia mapping step",
             default=False,
             action='store_true')

flags.define("--extra_items",
             help="additional items with info",
             default=None,
             metavar="RECFILES")

flags.define("--lbzip2",
             help="use lbzip2 for parallel decompression",
             default=False,
             action='store_true')

class WikiWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf

  #---------------------------------------------------------------------------
  # Wikidata
  #---------------------------------------------------------------------------

  def wikidata_dump(self):
    """Resource for Wikidata dump. This can be downloaded from wikimedia.org
    and contains a full dump of Wikidata in JSON format."""
    return self.wf.resource(corpora.wikidata_dump(), format="text/json")

  def wikidata_items(self):
    """Resource for Wikidata items. This is a set of record files where each
    WikiData item is represented as a frame:
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
          /lang/<lang>: <wid>
          ...
       }
       ... properties
      }

      <qid>: Wikidata item id (Q<item number>, e.g. Q35)
      <pid>: Wikidata property id (P<property number>, e.g. P31)
      <wid>: Wikipedia page id (/wp/<lang>/<pageid>, /wp/en/76972)
    """
    return self.wf.resource("wikidata-items@10.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikidata_redirects(self):
    """Resource for Wikidata redirects. This is a set of record files where each
    WikiData redirect is represented as a frame:
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

  def wikidata_import(self, input, latest=False, name=None):
    """Task for converting Wikidata JSON to SLING items and properties."""
    task = self.wf.task("wikidata-importer", name=name)
    task.add_param("primary_language", flags.arg.language)
    task.add_param("only_primary_language", flags.arg.only_primary_language)
    task.add_param("only_known_languages", flags.arg.only_known_languages)
    self.wf.connect(input, task)
    items = self.wf.channel(task, name="items", format="message/frame")
    properties = self.wf.channel(task, name="properties",
                                 format="message/frame")
    if latest:
      task.attach_output("latest", self.wikidata_latest())
    return items, properties

  def wikidata_input(self, dump):
    if flags.arg.lbzip2:
      input = self.wf.pipe("lbzip2 -d -c " + dump.name,
                           name="wiki-decompress",
                           format="text/json")
    else:
      input = self.wf.read(dump)
    return self.wf.parallel(input, threads=10, queue=1000)

  def wikidata(self, dump=None):
    """Import Wikidata dump to frame format. It takes a Wikidata dump in JSON
    format as input and converts each item and property to a SLING frame.
    Returns the item and property output files."""
    if dump == None: dump = self.wikidata_dump()
    with self.wf.namespace("wikidata"):
      input = self.wikidata_input(dump)
      items, properties = self.wikidata_import(input)
      items_output = self.wikidata_items()
      self.wf.write(items, items_output, name="item-writer")
      properties_output = self.wikidata_properties()
      self.wf.write(properties, properties_output, name="property-writer")
      return items_output, properties_output

  def wikidatadb(self):
    """Resource for Wikidata database."""
    return self.wf.resource(corpora.wikidatadb(), format="db/frames")

  def wikidata_latest(self):
    """Resource for latest Wikidata update. This contains the the QID and
    revision of the latest update."""
    return self.wf.resource("latest", dir=corpora.wikidir(), format="text")

  def wikidata_load(self, dump=None):
    """Load Wikidata dump into a database. It takes a Wikidata dump in JSON
    format as input and converts each item and property to a SLING frame."""
    if dump == None: dump = self.wikidata_dump()
    with self.wf.namespace("wikidata"):
      input = self.wikidata_input(dump)
      items, properties = self.wikidata_import(input, latest=True)
      db = self.wikidatadb()
      self.wf.write([items, properties], db, name="db-writer")
      return db

  def wikidata_snapshot(self):
    """Read snapshot from Wikidata database and output items, properties, and
    redirects."""
    with self.wf.namespace("wikisnap"):
      # Read frames from database.
      snapshot = self.wf.read(self.wikidatadb(), name="db-reader")
      input = self.wf.parallel(snapshot, threads=3, queue=1000)

      # Split snapshot into items, properties, and redirects.
      task = self.wf.task("wikidata-splitter")
      self.wf.connect(input, task)
      items = self.wf.channel(task, name="items",
                              format="message/frame")
      properties = self.wf.channel(task, name="properties",
                                   format="message/frame")
      redirects = self.wf.channel(task, name="redirects",
                                  format="message/frame")

      # Write items, properties, and redirects.
      self.wf.write(items, self.wikidata_items(),
                    name="item-writer")
      self.wf.write(properties, self.wikidata_properties(),
                    name="property-writer")
      self.wf.write(redirects, self.wikidata_redirects(),
                    name="redirect-writer")

  def fanin(self):
    """Resource for item fan-in, i.e. the number of times an item is a target
    in a relation."""
    return self.wf.resource("fanin.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def compute_fanin(self):
    """Compute item fan-in, i.e. the number of times an item is the target in
    a relation."""
    return self.wf.mapreduce(input=self.wikidata_items(),
                             output=self.fanin(),
                             mapper="fact-target-extractor",
                             reducer="item-fanin-reducer",
                             format="message/int")

  #---------------------------------------------------------------------------
  # Wikipedia
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

  def wikipedia_mapping(self, language=None):
    """Resource for wikipedia to wikidata mapping. This is a SLING frame store
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

  def wikipedia_documents(self, language=None):
    """Resource for parsed Wikipedia documents. This is a set of record files
    with one record per article, where the text has been extracted from the
    wiki markup and tokenized. The documents also contains additional
    structured information (e.g. categories) and mentions for links to other
    Wikipedia pages:
      <wid>: {
        =<wid>
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

  def wikipedia_category_documents(self, language=None):
    """Resource for parsed Wikipedia category documents.
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("category-documents@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/document")

  def wikipedia_aliases(self, language=None):
    """Resource for wikipedia aliases. The aliases are extracted from the
    Wiipedia pages from anchors, redirects, disambiguation pages etc. This is
    a set of record files with a SLING frame record for each item:
      <qid>: {
        alias: {
          name: "<alias>"
          lang: /lang/<lang>
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

  def language_defs(self):
    """Resource for language definitions. This defines the /lang/<lang>
    symbols and has meta information for each language."""
    return self.wf.resource("languages.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def template_defs(self, language=None):
    """Resource for template definitions."""
    if language == None: language = flags.arg.language
    return self.wf.resource("templates.sling",
                            dir=corpora.repository("data/wiki/" + language),
                            format="store/frame")

  def wikipedia_import(self, input, name=None):
    """Task for converting Wikipedia dump to SLING articles and redirects.
    Returns article, categories, and redirect channels."""
    task = self.wf.task("wikipedia-importer", name=name)
    task.attach_input("input", input)
    articles = self.wf.channel(task, name="articles", format="message/frame")
    categories = self.wf.channel(task, name="categories",
                                 format="message/frame")
    redirects = self.wf.channel(task, name="redirects", format="message/frame")
    return articles, categories, redirects

  def wikipedia(self, dump=None, language=None):
    """Convert Wikipedia dump to SLING articles and store them in a set of
    record files. Returns output resources for articles and redirects."""
    if language == None: language = flags.arg.language
    if dump == None: dump = self.wikipedia_dump(language)
    with self.wf.namespace(language + "-wikipedia"):
      # Import Wikipedia dump and convert to SLING format.
      articles, categories, redirects = self.wikipedia_import(dump)

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

  def wikimap(self, wikidata_items=None, language=None, name=None):
    """Task for building mapping from Wikipedia IDs (<wid>) to Wikidata
    IDs (<qid>). Returns file with frame store for mapping."""
    if language == None: language = flags.arg.language
    if wikidata_items == None: wikidata_items = self.wikidata_items()

    wiki_mapping = self.wf.map(wikidata_items, "wikipedia-mapping",
                               params={"language": language},
                               name=name)
    output = self.wikipedia_mapping(language)
    self.wf.write(wiki_mapping, output, name="mapping-writer")
    return output

  def parse_wikipedia_articles(self,
                               articles=None,
                               categories=None,
                               redirects=None,
                               commons=None,
                               wikimap=None,
                               language=None):
    """Task for parsing Wikipedia articles to SLING documents and aliases.
    Returns channels for documents and aliases."""
    if language == None: language = flags.arg.language
    if articles == None: articles = self.wikipedia_articles(language)
    if categories == None: categories = self.wikipedia_categories(language)
    if redirects == None: redirects = self.wikipedia_redirects(language)
    if commons == None:
      commons = [
        self.language_defs(),
        self.template_defs(language),
        self.unit_defs(),
        self.calendar_defs(),
        self.country_defs(),
      ]
    if wikimap == None: wikimap = self.wikipedia_mapping(language)

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
    catdocs = self.wf.channel(parser, "categories", format="message/qid:alias")
    return documents, aliases, catdocs

  def parse_wikipedia(self, language=None):
    """Parse Wikipedia articles and build alias table."""
    if language == None: language = flags.arg.language
    with self.wf.namespace(language + "-wikipedia"):
      with self.wf.namespace("mapping"):
        # Build mapping from Wikipedia IDs to Wikidata IDs.
        if not flags.arg.skip_wikipedia_mapping:
          self.wikimap(language=language)

      with self.wf.namespace("parsing"):
        # Parse Wikipedia articles to SLING documents.
        documents, aliases, catdocs = \
          self.parse_wikipedia_articles(language=language)

        # Write Wikipedia documents.
        document_output = self.wikipedia_documents(language)
        self.wf.write(documents, document_output, name="document-writer",
                      params={"indexed": flags.arg.index})

        # Write Wikipedia category documents.
        category_document_output = self.wikipedia_category_documents(language)
        self.wf.write(catdocs, category_document_output, name="document-writer",
                      params={"indexed": flags.arg.index})

      with self.wf.namespace("aliases"):
        # Collect aliases.
        alias_output = self.wikipedia_aliases(language)
        self.wf.reduce(self.wf.shuffle(aliases, len(alias_output)),
                       alias_output,
                       "wikipedia-alias-reducer",
                       params={'language': language})

    return document_output, alias_output

  #---------------------------------------------------------------------------
  # Wikipedia items
  #---------------------------------------------------------------------------

  def wikipedia_items(self):
    """Resource for item data from Wikipedia . This merges the item categories
    from all Wikipedias.
    """
    return self.wf.resource("wikipedia-items.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def wikipedia_members(self):
    """Resource for members of categories.
    """
    return self.wf.resource("wikipedia-members.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def merge_wikipedia_categories(self, languages=None):
    """Merge Wikipedia categories for all languages."""
    if languages == None: languages = flags.arg.languages

    with self.wf.namespace("wikipedia-categories"):
      documents = []
      for language in languages:
        documents.extend(self.wikipedia_documents(language))
        documents.extend(self.wikipedia_category_documents(language))
      return self.wf.mapreduce(input=documents,
                               output=self.wikipedia_items(),
                               mapper="category-item-extractor",
                               reducer="category-item-merger",
                               format="message/frame")

  def invert_wikipedia_categories(self, languages=None):
    """Invert category membership."""
    if languages == None: languages = flags.arg.languages

    with self.wf.namespace("wikipedia-members"):
      return self.wf.mapreduce(input=self.wikipedia_items(),
                               output=self.wikipedia_members(),
                               mapper="category-inverter",
                               reducer="category-member-merger",
                               format="message/string",
                               params={"threshold": 100000})

  #---------------------------------------------------------------------------
  # Wikipedia link graph
  #---------------------------------------------------------------------------

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

  def extract_links(self):
    """Build link graph over all Wikipedias and compute item popularity."""
    documents = []
    for l in flags.arg.languages:
      documents.extend(self.wikipedia_documents(l))

    # Extract links from documents.
    mapper = self.wf.task("wikipedia-link-extractor")
    self.wf.connect(self.wf.read(documents), mapper)
    links = self.wf.channel(mapper, format="message/frame",
                            name="output")
    targets = self.wf.channel(mapper, format="message/int",
                              name="target")

    # Reduce links.
    with self.wf.namespace("links"):
      wikilinks = self.wikilinks()
      self.wf.reduce(self.wf.shuffle(links, shards=length_of(wikilinks)),
                     wikilinks, "wikipedia-link-merger")

    # Reduce link targets.
    with self.wf.namespace("targets"):
      popularity = self.popularity()
      self.wf.reduce(self.wf.shuffle(targets, shards=length_of(popularity)),
                     popularity, "item-popularity-reducer")

    return wikilinks, popularity

  #---------------------------------------------------------------------------
  # Knowledge base reconcilation
  #---------------------------------------------------------------------------

  def xref_properties(self):
    """Resource for properties tracked for cross-references."""
    return self.wf.resource("xrefs.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def xrefs(self):
    """Resource for store with cross-reference items."""
    return self.wf.resource("xrefs.sling",
                            dir=corpora.wikidir(),
                            format="store/frame")

  def wikimedia(self):
    """Resource for media files extracted from Wikipedia."""
    return self.wf.resource("*wiki-media.sling",
                            dir=flags.arg.workdir + "/media",
                            format="text/frame")

  def fused_items(self):
    """Resource for merged items. This is a set of record files where each
    item is represented as a frame.
    """
    return self.wf.resource("items@10.rec",
                            dir=corpora.wikidir(),
                            format="records/frame")

  def collect_xrefs(self, items=None):
    """Collect and cluster item identifiers."""
    if items == None:
      items = [self.wikidata_redirects()] + self.wikidata_items();

    with self.wf.namespace("xrefs"):
      builder = self.wf.task("xref-builder")
      self.wf.connect(self.wf.read(items), builder)
      builder.attach_input("config", self.xref_properties())
      xrefs = self.xrefs()
      builder.attach_output("output", xrefs)
    return xrefs

  def standard_item_sources(self, items=None):
    if items == None:
      items = self.wf.bundle(
                self.wikidata_items(),
                self.wikilinks(),
                self.popularity(),
                self.fanin(),
                self.wikipedia_items(),
                self.wikipedia_members(),
                self.wikimedia())

    if flags.arg.extra_items:
      extra = self.wf.resource(flags.arg.extra_items, format="records/frame")
      items = self.wf.bundle(items, extra)

    return items

  def reconcile_items(self, items=None, output=None):
    """Reconcile items."""
    items = self.standard_item_sources(items)
    if output == None: output = self.fused_items()

    with self.wf.namespace("reconciled-items"):
      return self.wf.mapreduce(input=items,
                               output=output,
                               mapper="item-reconciler",
                               reducer="item-merger",
                               format="message/frame",
                               params={
                                 "indexed": flags.arg.index
                               },
                               auxin={
                                 "commons": self.xrefs(),
                               })

  def fuse_items(self, items=None, extras=None, output=None):
    """Fuse items."""
    items = self.standard_item_sources(items)
    if extras != None:
      if isinstance(extras, list):
        items.extend(extras)
      else:
        items.append(extras)

    if output == None: output = self.fused_items()

    with self.wf.namespace("fused-items"):
      return self.wf.mapreduce(input=items,
                               output=output,
                               mapper="item-reconciler",
                               reducer="item-merger",
                               format="message/frame",
                               params={"indexed": flags.arg.index})

  #---------------------------------------------------------------------------
  # Knowledge base
  #---------------------------------------------------------------------------

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

  def knowledge_base(self):
    """Resource for knowledge base. This is a SLING frame store with frames for
    each Wikidata item and property plus additional schema information.
    """
    return self.wf.resource("kb.sling",
                            dir=corpora.wikidir(),
                            format="store/frame")

  def schema_defs(self):
    """Resources for schemas included in knowledge base."""
    return self.wf.bundle(
      self.language_defs(),
      self.calendar_defs(),
      self.country_defs(),
      self.unit_defs(),
      self.wikidata_defs(),
      self.wikipedia_defs())

  def build_knowledge_base(self,
                           items=None,
                           properties=None,
                           schemas=None):
    """Task for building knowledge base store with items, properties, and
    schemas."""
    if items == None: items = self.fused_items()
    if properties == None: properties = self.wikidata_properties()
    if schemas == None: schemas = self.schema_defs()

    with self.wf.namespace("wikidata"):
      # Prune information from Wikidata items.
      pruned_items = self.wf.map(items, "wikidata-pruner",
        params={"prune_aliases": True,
                "prune_wiki_links": True,
                "prune_category_members": True})

      # Collect property catalog.
      property_catalog = self.wf.map(properties, "wikidata-property-collector")

      # Collect frames into knowledge base store.
      parts = self.wf.collect(pruned_items, property_catalog, schemas)
      return self.wf.write(parts, self.knowledge_base(),
                           params={"snapshot": True})

  #---------------------------------------------------------------------------
  # Item names
  #---------------------------------------------------------------------------

  def item_names(self, language=None):
    """Resource for item names in language. This is a set of record files with
    one SLING frame per item.
      <qid>: {
        alias: {
          name: "<alias>"
          lang: /lang/<lang>
          sources: ...
          count: ...
          form: ...
        }
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.wf.resource("names@10.rec",
                            dir=corpora.wikidir(language),
                            format="records/alias")

  def alias_corrections(self):
    """Resource for alias corrections."""
    return self.wf.resource("aliases.sling",
                            dir=corpora.repository("data/wiki"),
                            format="store/frame")

  def extract_names(self, aliases=None, language=None):
    "Task for selecting language-dependent names for items."""
    if language == None: language = flags.arg.language

    if aliases == None:
      # Get language-dependent aliases from Wikidata and Wikpedia.
      wikidata_aliases = self.wf.map(self.fused_items(),
                                     "alias-extractor",
                                     params={
                                       "language": language,
                                       "skip_aux": True,
                                     },
                                     format="message/alias",
                                     name="wikidata-alias-extractor")
      wikipedia_aliases = self.wf.read(self.wikipedia_aliases(language),
                                       name="wikipedia-alias-reader")
      aliases = wikipedia_aliases + [wikidata_aliases]

    # Merge alias sources.
    names = self.item_names(language)
    merged_aliases = self.wf.shuffle(aliases, len(names))

    # Filter and select aliases.
    selector = self.wf.reduce(merged_aliases, names, "alias-reducer",
                              params={
                                "language": language,
                                "anchor_threshold": 30,
                                "min_prefix": 1,
                                "max_edit_distance": {
                                  "fi": 5, "pl": 5
                                }.get(language, 3)
                              })
    selector.attach_input("commons", self.alias_corrections())
    return names

  #---------------------------------------------------------------------------
  # Name table
  #---------------------------------------------------------------------------

  def name_table(self, language=None):
    """Resource for item name table. This is a repository with all the names
    and the items they are aliases for."""
    if language == None: language = flags.arg.language
    return self.wf.resource("name-table.repo",
                            dir=corpora.wikidir(language),
                            format="repository")

  def build_name_table(self, names=None, language=None):
    """Build name table for all items."""
    if language == None: language = flags.arg.language
    if names == None: names = self.item_names(language)

    with self.wf.namespace("name-table"):
      builder = self.wf.task("name-table-builder")
      builder.add_param("language", language)
      self.wf.connect(self.wf.read(names, name="name-reader"), builder)
      repo = self.name_table(language)
      builder.attach_output("repository", repo)
    return repo

  #---------------------------------------------------------------------------
  # Phrase table
  #---------------------------------------------------------------------------

  def phrase_table(self, language=None):
    """Resource for item name phrase table. This is a repository with phrase
    fingerprints of the item names."""
    if language == None: language = flags.arg.language
    return self.wf.resource("phrase-table.repo",
                            dir=corpora.wikidir(language),
                            format="repository")

  def build_phrase_table(self, names=None, language=None):
    """Build phrase table for all items."""
    if language == None: language = flags.arg.language
    if names == None: names = self.item_names(language)

    with self.wf.namespace("phrase-table"):
      builder = self.wf.task("phrase-table-builder")
      builder.add_param("language", language)
      builder.add_param("transfer_aliases", True)
      self.wf.connect(self.wf.read(names, name="name-reader"), builder)
      kb = self.knowledge_base()
      repo = self.phrase_table(language)
      builder.attach_input("commons", kb)
      builder.attach_output("repository", repo)
    return repo

# Commands.

wikidata_import = False
wikipedia_import = False

def build_wiki():
  pass

def import_wikidata():
  # Trigger import of wikidata dump.
  global wikidata_import
  wikidata_import = True

def import_wikipedia():
  # Trigger import of wikipedia dump.
  global wikipedia_import
  wikipedia_import = True

def import_wiki():
  wf = WikiWorkflow("wiki-import")

  # Import wikidata.
  if wikidata_import:
    log.info("Import wikidata")
    wf.wikidata()

  # Import wikipedia(s).
  if wikipedia_import:
    for language in flags.arg.languages:
      log.info("Import " + language + " wikipedia")
      wf.wikipedia(language=language)

  run(wf.wf)

def load_wikidata():
  # Load Wikidata into database.
  wf = WikiWorkflow("wikidata-load")
  log.info("Load wikidata")
  wf.wikidata_load()
  run(wf.wf)

def snapshot_wikidata():
  # Make snapshot from Wikidata database.
  wf = WikiWorkflow("wikidata-snapshot")
  log.info("Snapshot wikidata")
  wf.wikidata_snapshot()
  run(wf.wf)

def parse_wikipedia():
  # Convert wikipedia pages to SLING documents.
  for language in flags.arg.languages:
    log.info("Parse " + language + " wikipedia")
    wf = WikiWorkflow(language + "-wikipedia-parsing")
    wf.parse_wikipedia(language=language)
    run(wf.wf)

def merge_categories():
  # Merge categories from wikipedias.
  log.info("Merge wikipedia categories")
  wf = WikiWorkflow("category-merging")
  wf.merge_wikipedia_categories()
  run(wf.wf)

def invert_categories():
  # Invert categories.
  log.info("Invert categories")
  wf = WikiWorkflow("category-inversion")
  wf.invert_wikipedia_categories()
  run(wf.wf)

def extract_wikilinks():
  # Extract link graph.
  log.info("Extract link graph")
  wf = WikiWorkflow("link-graph")
  wf.extract_links()
  run(wf.wf)

def compute_fanin():
  # Compute item fan-in.
  log.info("Compute item fan-in")
  wf = WikiWorkflow("fanin")
  wf.compute_fanin()
  run(wf.wf)

def collect_xrefs():
  # Collect cross-references.
  wf = WikiWorkflow("xref-builder")
  log.info("Build cross references")
  wf.collect_xrefs()
  run(wf.wf)

def reconcile_items():
  # Reconcile items.
  log.info("Reconcile items")
  wf = WikiWorkflow("reconcile-items")
  wf.reconcile_items()
  run(wf.wf)

def fuse_items():
  # Fuse items.
  log.info("Fuse items")
  wf = WikiWorkflow("fuse-items")
  wf.fuse_items()
  run(wf.wf)

def build_kb():
  # Build knowledge base repository.
  log.info("Build knowledge base repository")
  wf = WikiWorkflow("knowledge-base")
  wf.build_knowledge_base()
  run(wf.wf)

def extract_names():
  # Extract item names from wikidata and wikipedia.
  for language in flags.arg.languages:
    log.info("Extract " + language + " names")
    wf = WikiWorkflow(language + "-name-extraction")
    wf.extract_names(language=language)
    run(wf.wf)

def build_nametab():
  # Build name table.
  for language in flags.arg.languages:
    log.info("Build " + language + " name table")
    wf = WikiWorkflow(language + "-name-table")
    wf.build_name_table(language=language)
    run(wf.wf)

def build_phrasetab():
  # Build phrase table.
  for language in flags.arg.languages:
    log.info("Build " + language + " phrase table")
    wf = WikiWorkflow(language + "-phrase-table")
    wf.build_phrase_table(language=language)
    run(wf.wf)


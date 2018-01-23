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

"""Workflow builder"""

from task import *
import corpora
import sling.flags as flags

class Workflow(Job):
  def __init__(self):
    super(Workflow, self).__init__()

  #---------------------------------------------------------------------------
  # Wikidata
  #---------------------------------------------------------------------------

  def wikidata_dump(self):
    """Resource for wikidata dump. This can be downloaded from wikimedia.org
    and contains a full dump of WikiData in JSON format."""
    return self.resource(corpora.wikidata_dump(), format="text/json")

  def wikidata_items(self):
    """Resource for wikidata items. This is a set of record files where each
    WikiData item is represented as a frame:
      <qid>: {
        =<qid>
        :/w/item
        name: "..."
        description: "..."
        /s/profile/alias: {
          name: "..."
          lang: /lang/<lang>
          /s/alias/sources: ...
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
    return self.resource("items@10.rec",
                         dir=corpora.wikidir(),
                         format="records/frame")

  def wikidata_properties(self):
    """Resource for wikidata properties. This is a record file where each
    WikiData property is represented as a frame.
      <pid>: {
        =<pid>
        :/w/property
        name: "..."
        description: "..."
        /w/datatype: ...
        ... properties ...
      }
    """
    return self.resource("properties.rec",
                         dir=corpora.wikidir(),
                         format="records/frame")

  def wikidata_import(self, input, name=None):
    """Task for converting WikiData JSON to SLING items and properties."""
    task = self.task("wikidata-importer", name=name)
    task.add_param("primary_language", flags.arg.language)
    self.connect(input, task)
    items = self.channel(task, name="items", format="message/frame")
    properties = self.channel(task, name="properties", format="message/frame")
    return items, properties

  def wikidata(self, dump=None):
    """Import Wikidata dump to frame format. It takes a WikiData dump in JSON
    format as inpput and converts each item and property to a SLING frame.
    Returns the item and property output files."""
    if dump == None: dump = self.wikidata_dump()
    with self.namespace("wikidata"):
      input = self.parallel(self.read(dump), threads=5)
      items, properties = self.wikidata_import(input)
      items_output = self.wikidata_items()
      self.write(items, items_output, name="item-writer")
      properties_output = self.wikidata_properties()
      self.write(properties, properties_output, name="property-writer")
      return items_output, properties_output

  #---------------------------------------------------------------------------
  # Wikipedia
  #---------------------------------------------------------------------------

  def wikipedia_dump(self, language=None):
    """Resource for wikipedia dump. This can be downloaded from wikimedia.org
    and contrains a full dump of Wikipedia in a particular language. This is
    in XML format with the articles in Wiki markup format."""
    if language == None: language = flags.arg.language
    return self.resource(corpora.wikipedia_dump(language),
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
    return self.resource("articles@10.rec",
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
    return self.resource("redirects.sling",
                         dir=corpora.wikidir(language),
                         format="store/frame")

  def wikipedia_mapping(self, language=None):
    """Resource for wikipedia to wikidata mapping. This is a SLING frame store
    with one frame per Wikipedia article with infomation for mapping it to
    WikiData.
      {
        =<wid>
        /w/item/qid: <qid>
        /w/item/kind: /w/item/kind/...
      }
    """
    if language == None: language = flags.arg.language
    return self.resource("mapping.sling",
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
        :/s/document
        /s/document/url: "http://<lang>.wikipedia.org/wiki/<name>"
        /s/document/title: "..."
        /s/document/text: "<clear text extracted from wiki markup>"
        /s/document/tokens: [...]
        /s/document/mention: {
          :/wp/link
          /s/phrase/begin: ...
          /s/phrase/length: ...
          name: "..."
          /s/phrase/evokes: <qid>
        }
        ...
        /wp/page/category: <qid>
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.resource("documents@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/document")

  def wikipedia_aliases(self, language=None):
    """Resource for wikipedia aliases. The aliases are extracted from the
    Wiipedia pages from anchors, redirects, disambiguation pages etc. This is
    a set of record files with a SLING frame record for each item:
      <qid>: {
        /s/profile/alias: {
          name: "<alias>"
          lang: /lang/<lang>
          /s/alias/sources: ...
          /s/alias/count: ...
        }
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.resource("aliases@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/alias")

  def language_defs(self):
    """Resource for language definitions. This defines the /lang/<lang>
    symbols and has meta information for each language."""
    return self.resource("languages.sling",
                         dir=corpora.repository("data/wiki"),
                         format="store/frame")

  def wikipedia_import(self, input, name=None):
    """Task for converting Wikipedia dump to SLING articles and redirects.
    Returns article and redirect channels."""
    task = self.task("wikipedia-importer", name=name)
    task.attach_input("input", input)
    articles = self.channel(task, name="articles", format="message/frame")
    redirects = self.channel(task, name="redirects", format="message/frame")
    return articles, redirects

  def wikipedia(self, dump=None, language=None):
    """Convert Wikipedia dump to SLING articles and store them in a set of
    record files. Returns output resources for articles and redirects."""
    if language == None: language = flags.arg.language
    if dump == None: dump = self.wikipedia_dump(language)
    with self.namespace(language + "-wikipedia"):
      # Import Wikipedia dump and convert to SLING format.
      articles, redirects = self.wikipedia_import(dump)

      # Write articles.
      articles_output = self.wikipedia_articles(language)
      self.write(articles, articles_output, name="article-writer")

      # Write redirects.
      redirects_output = self.wikipedia_redirects(language)
      self.write(redirects, redirects_output, name="redirect-writer")

      return articles_output, redirects_output

  def wikimap(self, wikidata_items=None, language=None, name=None):
    """Task for building mapping from Wikipedia IDs (<wid>) to Wikidata
    IDs (<qid>). Returns file with frame store for mapping."""
    if language == None: language = flags.arg.language
    if wikidata_items == None: wikidata_items = self.wikidata_items()

    wiki_mapping = self.map(wikidata_items, "wikipedia-mapping",
                            params={"language": language},
                            name=name)
    output = self.wikipedia_mapping(language)
    self.write(wiki_mapping, output, name="mapping-writer")
    return output

  def parse_wikipedia_articles(self,
                               articles=None,
                               redirects=None,
                               commons=None,
                               wikimap=None,
                               language=None):
    """Task for parsing Wikipedia articles to SLING documents and aliases.
    Returns channels for documents and aliases."""
    if language == None: language = flags.arg.language
    if articles == None: articles = self.wikipedia_articles(language)
    if redirects == None: redirects = self.wikipedia_redirects(language)
    if commons == None: commons = self.language_defs()
    if wikimap == None: wikimap = self.wikipedia_mapping(language)

    parser = self.task("wikipedia-profile-builder", "wikipedia-documents")
    self.connect(self.read(articles, name="article-reader"), parser)
    parser.attach_input("commons", commons)
    parser.attach_input("wikimap", wikimap)
    parser.attach_input("redirects", redirects)
    documents = self.channel(parser, format="message/document")
    aliases = self.channel(parser, "aliases", format="message/qid:alias")
    return documents, aliases

  def parse_wikipedia(self, language=None):
    """Parse Wikipedia articles and build alias table."""
    if language == None: language = flags.arg.language
    with self.namespace(language + "-wikipedia"):
      # Build mapping from Wikipedia IDs to Wikidata IDs.
      self.wikimap(language=language)

      # Parse Wikipedia articles to SLING documents.
      documents, aliases = self.parse_wikipedia_articles(language=language)

      # Write Wikipedia documents.
      document_output = self.wikipedia_documents(language)
      self.write(documents, document_output, name="document-writer")

      # Collect aliases.
      alias_output = self.wikipedia_aliases(language)
      self.reduce(self.shuffle(aliases, len(alias_output)),
                  alias_output,
                  "wikipedia-alias-reducer",
                  params={'language': language})
    return document_output, alias_output

  #---------------------------------------------------------------------------
  # Item names
  #---------------------------------------------------------------------------

  def item_names(self, language=None):
    """Resource for item names in language. This is a set of record files with
    one SLING frame per item.
      <qid>: {
        /s/profile/alias: {
          name: "<alias>"
          lang: /lang/<lang>
          /s/alias/sources: ...
          /s/alias/count: ...
        }
        ...
      }
    """
    if language == None: language = flags.arg.language
    return self.resource("names@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/alias")

  def extract_names(self, aliases=None, language=None):
    "Task for selecting language-dependent names for items."""
    if language == None: language = flags.arg.language

    if aliases == None:
      # Get language-dependent aliases from Wikidata and Wikpedia.
      wikidata_aliases = self.map(self.wikidata_items(),
                                  "profile-alias-extractor",
                                   params={"language": language},
                                   format="message/alias",
                                   name="wikidata-alias-extractor")
      wikipedia_aliases = self.read(self.wikipedia_aliases(language))
      aliases = wikipedia_aliases + [wikidata_aliases]

    # Merge alias sources.
    names = self.item_names(language)
    merged_aliases = self.shuffle(aliases, len(names))

    # Filter and select aliases.
    self.reduce(merged_aliases, names, "profile-alias-reducer",
                params={"language": language})
    return names

  #---------------------------------------------------------------------------
  # Knowledge base
  #---------------------------------------------------------------------------

  def calendar_defs(self):
    """Resource for calendar definitions."""
    return self.resource("calendar.sling",
                         dir=corpora.repository("data/wiki"),
                         format="store/frame")

  def knowledge_base(self):
    """Resource for knowledge base. This is a SLING frame store with frames for
    each Wikidata item and property plus additional schema information.
    """
    return self.resource("kb.sling",
                         dir=corpora.wikidir(),
                         format="store/frame")

  def build_knowledge_base(self,
                           wikidata_items=None,
                           wikidata_properties=None,
                           schemas=None):
    """Task for building knowledge base store with items, properties, and
    schemas."""
    if wikidata_items == None:
      wikidata_items = self.wikidata_items()
    if wikidata_properties == None:
      wikidata_properties = self.wikidata_properties()
    if schemas == None:
      schemas = [
        self.language_defs(),
        self.calendar_defs(),
      ]

    with self.namespace("wikidata"):
      # Prune information from Wikidata items.
      pruned_items = self.map(wikidata_items, "wikidata-pruner")

      # Collect property catalog.
      property_catalog = self.map(wikidata_properties,
                                  "wikidata-property-collector")

      # Collect frames into knowledge base store.
      parts = self.collect(pruned_items, property_catalog, schemas)
      return self.write(parts, self.knowledge_base())


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

  def wikidata_dump(self):
    """Resource for wikidata dump"""
    return self.resource(corpora.wikidata_dump(), format="text/json")

  def wikidata_items(self):
    """Resource for wikidata items."""
    return self.resource("items@10.rec",
                         dir=corpora.wikidir(),
                         format="records/frame")

  def wikidata_properties(self):
    """Resource for wikidata properties."""
    return self.resource("properties.rec",
                         dir=corpora.wikidir(),
                         format="records/frame")

  def wikidata_import(self, input, name=None):
    """Convert WikiData JSON to SLING items and properties."""
    task = self.task("wikidata-importer", name=name)
    task.add_param("primary_language", flags.arg.language)
    self.connect(input, task)
    items = self.channel(task, name="items", format="message/frame")
    properties = self.channel(task, name="properties", format="message/frame")
    return items, properties

  def wikidata(self):
    """Import Wikidata dump."""
    with self.namespace("wikidata"):
      input = self.parallel(self.read(self.wikidata_dump()), threads=5)
      items, properties = self.wikidata_import(input)
      items_output = self.wikidata_items()
      self.write(items, items_output, name="item-writer")
      properties_output = self.wikidata_properties()
      self.write(properties, properties_output, name="property-writer")
      return items_output, properties_output

  def wikipedia_dump(self, language=None):
    """Resource for wikipedia dump"""
    if language == None: language = flags.arg.language
    return self.resource(corpora.wikipedia_dump(language),
                         format="xml/wikipage")

  def wikipedia_articles(self, language=None):
    """Resource for wikipedia articles."""
    if language == None: language = flags.arg.language
    return self.resource("articles@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/frame")

  def wikipedia_redirects(self, language=None):
    """Resource for wikidata redirects."""
    if language == None: language = flags.arg.language
    return self.resource("redirects.sling",
                         dir=corpora.wikidir(language),
                         format="store/frame")

  def wikipedia_import(self, input, name=None):
    """Convert Wikipedia dump to SLING documents and redirects."""
    task = self.task("wikipedia-importer", name=name)
    task.attach_input("input", input)
    articles = self.channel(task, name="articles", format="message/frame")
    redirects = self.channel(task, name="redirects", format="message/frame")
    return articles, redirects

  def wikipedia(self, language=None):
    """Import Wikipedia dump."""
    if language == None: language = flags.arg.language
    with self.namespace(language + "-wikipedia"):
      input = self.wikipedia_dump(language)
      articles, redirects = self.wikipedia_import(input)
      articles_output = self.wikipedia_articles(language)
      self.write(articles, articles_output, name="article-writer")
      redirects_output = self.wikipedia_redirects(language)
      self.write(redirects, redirects_output, name="redirect-writer")
      return articles_output, redirects_output

  def wikipedia_mapping(self, language=None):
    """Resource for wikipedia to wikidata mapping"""
    if language == None: language = flags.arg.language
    return self.resource("mapping",
                         dir=corpora.wikidir(language),
                         format="store/frame")

  def wikipedia_documents(self, language=None):
    """Resource for wikipedia documents."""
    if language == None: language = flags.arg.language
    return self.resource("documents@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/document")

  def wikipedia_aliases(self, language=None):
    """Resource for wikipedia aliases."""
    if language == None: language = flags.arg.language
    return self.resource("aliases@10.rec",
                         dir=corpora.wikidir(language),
                         format="records/alias")

  def language_defs(self):
    """Resource for language definitions"""
    return self.resource("languages.sling",
                         dir=corpora.repository("data/wiki"),
                         format="text/frame")

  def wikimap(self, language=None, name=None):
    """Create mapping from Wikipedia IDs to Wikidata IDs."""
    if language == None: language = flags.arg.language
    wikidata_items = self.wikidata_items()
    wiki_mapping = self.map(wikidata_items, "wikipedia-mapping",
                            params={"language": language},
                            name=name)
    output = self.wikipedia_mapping(language)
    self.write(wiki_mapping, output, name="mapping-writer")
    return output

  def parse_wikipedia_articles(self, language=None):
    """Parse Wikipedia articles to SLING documents and aliases."""
    if language == None: language = flags.arg.language
    articles = self.wikipedia_articles(language)
    redirects = self.wikipedia_redirects(language)
    parser = self.task("wikipedia-profile-builder", "wikipedia-documents")
    self.connect(self.read(articles, name="article-reader"), parser)
    parser.attach_input("commons", self.language_defs())
    parser.attach_input("wikimap", self.wikipedia_mapping(language))
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
      documents, aliases = self.parse_wikipedia_articles(language)

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


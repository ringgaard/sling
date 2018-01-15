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
    task.add_param("primary_language", flags.arg.language);
    self.connect(input, task)
    items = self.channel(task, name="items", format="message/frame")
    properties = self.channel(task, name="properties", format="message/frame")
    return items, properties

  def wikidata(self):
    """Import Wikidata dump."""
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
                         dir=corpora.wikidir() + "/" + language,
                         format="records/frame")

  def wikipedia_redirects(self, language=None):
    """Resource for wikidata redirects."""
    if language == None: language = flags.arg.language
    return self.resource("redirects.sling",
                         dir=corpora.wikidir() + "/" + language,
                         format="store/frame")

  def wikipedia_import(self, input, name=None):
    """Convert Wikipedia dump to SLING documents and redirects."""
    task = self.task("wikipedia-importer", name=name)
    task.attach_input("input", input)
    articles = self.channel(task, name="articles", format="message/frame")
    redirects = self.channel(task, name="redirects", format="message/frame")
    return articles, redirects

  def wikipedia(self, language=None, name=None):
    """Import Wikipedia dump."""
    if language == None: language = flags.arg.language
    input = self.wikipedia_dump(language)
    articles, redirects = self.wikipedia_import(input, name=name)
    articles_output = self.wikipedia_articles(language)
    self.write(articles, articles_output, name=language + "-article-writer")
    redirects_output = self.wikipedia_redirects(language)
    self.write(redirects, redirects_output, name=language + "-redirect-writer")
    return articles_output, redirects_output


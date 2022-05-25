# Copyright 2022 Ringgaard Research ApS
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

"""Workflow for building search index."""

import sling.flags as flags
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task import *

class SearchWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def item_terms(self, language=None):
    """Resource for item term table."""
    if language == None: language = flags.arg.language
    return self.wf.resource("item-terms.repo",
                            dir=corpora.workdir("search", language),
                            format="repository")

  def build_search_terms(self, items=None, language=None):
    "Task for building search terms for items."""
    if language == None: language = flags.arg.language
    if items == None: items = self.data.items()

    with self.wf.namespace("search"):
      builder = self.wf.task("search-terms-builder", params={
        "language": language,
        "normalization": "clnp",
      })
      self.wf.connect(self.wf.read(items, name="item-reader"), builder)

      repo = self.item_terms(language)
      builder.attach_output("repository", repo)

    return repo

def build_search_terms():
  for language in flags.arg.languages:
    log.info("Build " + language + " search terms")
    wf = SearchWorkflow(language + "-search")
    wf.build_search_terms(language=language)
    run(wf.wf)


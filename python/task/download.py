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

"""Workflows for downloading wiki dumps and datasets"""

import os
import time
import urllib3

import sling
import sling.task.corpora as corpora
import sling.flags as flags
import sling.log as log
from sling.task.workflow import *

flags.define("--dataurl",
             help="data set site",
             default="https://ringgaard.com/data",
             metavar="URL")

flags.define("--dataset",
             help="list of datasets to fetch",
             default="",
             metavar="LIST")

# Connection pool manager.
http =  urllib3.PoolManager()

# Number of concurrent downloads.
download_concurrency = 0

# Task for downloading files.
class UrlDownload:
  def run(self, task):
    # Get task parameters.
    name = task.param("shortname")
    baseurl = task.param("url")
    ratelimit = task.param("ratelimit", 0)
    chunksize = task.param("chunksize", 64 * 1024)
    priority = task.param("priority", 0)
    outputs = task.outputs("output")

    log.info("Download " + name + " from " + baseurl)
    for output in outputs:
      # Make sure directory exists.
      directory = os.path.dirname(output.name)
      if not os.path.exists(directory): os.makedirs(directory)

      # Do not overwrite existing file unless flag is set.
      if not flags.arg.overwrite and os.path.exists(output.name):
        raise Exception("file already exists: " + output.name + \
                        " (use --overwrite to overwrite existing files)")

      # Hold-off on low-prio tasks
      if priority > 0: time.sleep(priority)

      # Wait until we are below the rate limit.
      global download_concurrency
      if ratelimit > 0:
        while download_concurrency >= ratelimit: time.sleep(10)
        download_concurrency += 1

      # Compute url.
      if len(outputs) > 1:
        url = baseurl + "/" + os.path.basename(output.name)
      else:
        url = baseurl

      # Download from url to file.
      if ratelimit > 0: log.info("Start download of " + output.name)
      r = http.request('GET', url, preload_content=False, timeout=60)
      last_modified = time.mktime(time.strptime(r.headers['last-modified'],
                                                "%a, %d %b %Y %H:%M:%S GMT"))
      content_length = int(r.headers['content-length'])

      total_bytes_counter = "bytes_downloaded"
      bytes_counter = name + "_bytes_downloaded"
      bytes = 0
      with open(output.name, 'wb') as f:
        while bytes < content_length:
          try:
            chunk = r.read(chunksize)
          except urllib3.exceptions.ReadTimeoutError:
            log.info(name, "HTTP timeout", bytes, "of", content_length)
            r.close()
            r = http.request('GET', url, preload_content=False, timeout=60,
                             headers={"Range": "bytes=%s-" % bytes})
            continue

          if chunk is None:
            raise IOError("Download truncated %d bytes read, %d expected" %
                          (bytes, content_length))
          f.write(chunk)
          size = len(chunk)
          bytes += size
          task.increment(total_bytes_counter, size)
          task.increment(bytes_counter, size)
      r.release_conn()
      os.utime(output.name, (last_modified, last_modified))
      if ratelimit > 0: download_concurrency -= 1

    log.info(name + " downloaded")

register_task("url-download", UrlDownload)

# Task for making snapshot of frame store.
class StoreSnapshot:
  def run(self, task):
    filename = task.input("input").name
    store = sling.Store()
    log.info("Load store from", filename)
    store.load(filename)
    log.info("Coalesce store")
    store.coalesce()
    log.info("Snapshot store")
    store.snapshot(filename)

register_task("store-snapshot", StoreSnapshot)

class DownloadWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf

  #---------------------------------------------------------------------------
  # Wikipedia dumps
  #---------------------------------------------------------------------------

  def wikipedia_dump(self, language=None):
    """Resource for wikipedia dump. This can be downloaded from wikimedia.org
    and contains a full dump of Wikipedia in a particular language. This is
    in XML format with the articles in Wiki markup format."""
    if language == None: language = flags.arg.language
    return self.wf.resource(corpora.wikipedia_dump(language),
                            format="xml/wikipage")

  def download_wikipedia(self, url=None, dump=None, language=None):
    if language == None: language = flags.arg.language
    if url == None: url = corpora.wikipedia_url(language)
    if dump == None: dump = self.wikipedia_dump(language)
    priority = 1
    if language == "en": priority = 0

    with self.wf.namespace(language + "-wikipedia-download"):
      download = self.wf.task("url-download")
      download.add_params({
        "url": url,
        "shortname": language + "wiki",
        "ratelimit": 2,
        "priority": priority,
      })
      download.attach_output("output", dump)
      return dump

  #---------------------------------------------------------------------------
  # Wikidata dumps
  #---------------------------------------------------------------------------

  def wikidata_dump(self):
    """Resource for wikidata dump. This can be downloaded from wikimedia.org
    and contains a full dump of Wikidata in JSON format."""
    return self.wf.resource(corpora.wikidata_dump(), format="text/json")

  def download_wikidata(self, url=None, dump=None):
    if url == None: url = corpora.wikidata_url()
    if dump == None: dump = self.wikidata_dump()

    with self.wf.namespace("wikidata-download"):
      download = self.wf.task("url-download")
      download.add_params({
        "url": url,
        "shortname": "wikidata",
      })
      download.attach_output("output", dump)
      return dump

  #---------------------------------------------------------------------------
  # Datasets
  #---------------------------------------------------------------------------

  def dataset(self, path):
    if path.startswith("repo/"):
      return self.wf.resource(corpora.repository(path[5:]),
                              format="file")
    else:
      return self.wf.resource(path, dir=flags.arg.workdir, format="file")

  def download_dataset(self, name, path, files):
    download = self.wf.task("url-download")
    if type(files) is list:
      url = flags.arg.dataurl + "/" + path[:path.rfind("/")]
    else:
      url = flags.arg.dataurl + "/" + path
    download.add_params({
      "url": url,
      "shortname": name,
      "ratelimit": 5,
    })
    download.attach_output("output", files)

  def snapshot(self, store):
    snap = self.wf.task("store-snapshot")
    snap.attach_input("input", store)

# Datasets.
datasets = {
  "wikidata-items": "wiki/wikidata-items@10.rec",
  "wikidata-redirects": "wiki/wikidata-redirects@10.rec",
  "wikipedia-members": "wiki/wikipedia-members.rec",
  "links": "wiki/links@10.rec",
  "properties": "wiki/properties.rec",
  "summaries": "wiki/summaries.rec",
  "fanin": "wiki/fanin.rec",
  "popularity": "wiki/popularity.rec",

  "kb": "kb/kb.sling",
  "kbsnap": "kb/kb.sling.snap",
  "items": "kb/items@10.rec",
  "xrefs": "kb/xrefs.sling",

  "aliases": "kb/$LANG$/aliases@10.rec",
  "nametab": "kb/$LANG$/name-table.repo",
  "phrasetab": "kb/$LANG$/phrase-table.repo",

  "mapping": "wiki/$LANG$/mapping.sling",
  "articles": "wiki/$LANG$/articles@10.rec",
  "redirects": "wiki/$LANG$/redirects.sling",
  "categories": "wiki/$LANG$/categories.sling",
  "documents": "wiki/$LANG$/documents@10.rec",
  "category-documents": "wiki/$LANG$/category-documents@10.rec",
  "wikipedia-aliases": "wiki/$LANG$/aliases@10.rec",
  "wikipedia-summaries": "wiki/$LANG$/summaries.rec",

  "schemas": [
    "repo/data/nlp/schemas/catalog.sling",
    "repo/data/nlp/schemas/custom-properties.sling",
    "repo/data/nlp/schemas/document-schema.sling",
    "repo/data/nlp/schemas/meta-schema.sling",
  ],
  "wikidefs": [
    "repo/data/wiki/aliases.sling",
    "repo/data/wiki/calendar.sling",
    "repo/data/wiki/countries.sling",
    "repo/data/wiki/languages.sling",
    "repo/data/wiki/units.sling",
    "repo/data/wiki/wikidata.sling",
    "repo/data/wiki/wikipedia.sling",
  ],
  "templates": "repo/data/wiki/$LANG$/templates.sling",

  "caspar": "caspar/caspar.flow",
  "word2vec32": "caspar/word2vec-32-embeddings.bin",
}

# Commands.

wikidata_download = False
wikipedia_download = False

def download_wikidata():
  # Trigger download of wikidata dump.
  global wikidata_download
  wikidata_download = True

def download_wikipedia():
  # Trigger download of wikipedia dump.
  global wikipedia_download
  wikipedia_download = True

def download_wiki():
  wf = DownloadWorkflow("wiki-download")

  # Download wikidata dump.
  if wikidata_download:
    wf.download_wikidata()

  # Download wikipedia dumps.
  if wikipedia_download:
    for language in flags.arg.languages:
      wf.download_wikipedia(language=language)

  run(wf.wf)

def fetch():
  wf = DownloadWorkflow("data-download")
  for name in flags.arg.dataset.split(","):
    if len(name) == 0: continue
    paths = datasets.get(name)
    if paths is None:
      log.error("unknown dataset:", name)
      return
    if type(paths) != list: paths = [paths]
    for path in paths:
      if "$LANG$" in path:
        for language in flags.arg.languages:
          langpath = path.replace("$LANG$", language)
          res = wf.dataset(langpath)
          wf.download_dataset(name, langpath, res)
      else:
        res = wf.dataset(path)
        wf.download_dataset(name, path, res)

      if name == "kb": wf.snapshot(res)

  run(wf.wf)

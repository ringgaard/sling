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

"""SLING news article cache service"""

import sling
import sling.flags as flags
import sling.net

flags.define("--crawldb",
             help="database for crawled news articles",
             default="vault/crawl",
             metavar="DB")

class ArticleService:
  def __init__(self):
    self.crawldb = sling.Database(flags.arg.crawldb, "case/article")

  def handle(self, request):
    params = request.params()
    url = params["url"][0]

    # Try to fetch article from database.
    article = self.crawldb[url];

    # Handle redirects.
    if article and article.startswith(b"#REDIRECT "):
      url = article[10:]
      print("redirect", url)
      article = self.crawldb[url];

    # Redirect if article not in database.
    if article is None:
      print("article not found:", url)
      return 404

    # Get HTTP body.
    pos = article.find(b"\r\n\r\n")
    if pos == -1: return 500
    body = article[pos + 4:]

    # Return article.
    response = sling.net.HTTPResponse()
    response.body = body
    print("return article:", url)
    return response


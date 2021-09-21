# Copyright 2020 Ringgaard Research ApS
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

"""Get profile picture from Twitter profiles"""

import json
import requests
import urllib

import sling
import sling.flags as flags
import sling.log as log
import sling.task.corpora as corpora
import sling.task.data as data
from sling.task.workflow import *

flags.define("--twitterdb",
             help="database for storing Twitter profiles",
             default="twitter",
             metavar="DB")

bad_images = set([
  "",
  "http://pbs.twimg.com/profile_images/1302121919014207490/KaYYEC8b.jpg",
  "http://pbs.twimg.com/profile_images/825116317057298434/T8YRhnl8.jpg",
  "https://pbs.twimg.com/profile_images/1158806333686210560/iJj9gE9t.jpg",
  "https://pbs.twimg.com/profile_images/1267721157794361345/KnbubkxM.jpg",
  "https://pbs.twimg.com/profile_images/1278922852272467968/dAnpxA1L.jpg",
  "https://pbs.twimg.com/profile_images/1233065469189464066/dl-rt_ZK.jpg",
  "https://pbs.twimg.com/static/dmca/dmca-med.jpg",
])

# Task for extracting images from Twitter profiles.
class TwitterExtract:
  def run(self, task):
    # Get parameters.
    twitterdb = sling.Database(task.input("twitterdb").name, "twitter-extract")

    # Load knowledge base.
    log.info("Load knowledge base")
    kb = sling.Store()
    kb.load(task.input("kb").name)

    p_id = kb["id"]
    p_is = kb["is"]
    p_twitter = kb["P2002"]
    p_image = kb["P18"]
    p_media = kb["media"]
    p_stated_in = kb["P248"]
    p_deprecation = kb["P2241"]
    n_twitter = kb["Q918"]

    kb.freeze()

    # Open output file.
    fout = open(task.output("output").name, "w")

    # Find all items with twitter usernames.
    dbsession = requests.session()
    for item in kb:
      # Find twitter username for item.
      task.increment("items")
      imageurls = []
      for twitter in item(p_twitter):
        # Get twitter user name; skip if deprecated.
        if type(twitter) is sling.Frame:
          if twitter[p_deprecation] != None:
            task.increment("deprecated_users")
            continue
          username = twitter[p_is]
        else:
          username = twitter
        task.increment("twitter_users")

        # Fetch twitter profile from database.
        data = twitterdb[username]
        if data is None:
          task.increment("unknown_users")
          continue
        profile = json.loads(data)

        # Ignore if twitter profile does not exist.
        if "error" in profile:
          task.increment("deleted_users")
          continue

        # Ignore if there is no profile image.
        if profile["default_profile_image"]:
          task.increment("missing_profile_images")
          continue

        # Get profile image url.
        imageurl = profile["profile_image_url"]

        # Get url for original image url by removing "_normal".
        imageurl = ''.join(imageurl.rsplit("_normal", 1))

        # Ignore known bad images.
        if imageurl in bad_images:
          task.increment("bad_profile_images")
          continue

        # Add twiter profile image to item.
        imageurls.append(imageurl)

      if len(imageurls) > 0:
        # Create item frame with twitter profile.
        store = sling.Store(kb)
        slots = [(p_id, item.id)]
        for imageurl in imageurls:
          image = store.frame([(p_is, imageurl), (p_stated_in, n_twitter)])
          slots.append((p_media, image))
        frame = store.frame(slots)
        fout.write(frame.data(utf8=True))
        fout.write("\n")

        task.increment("profile_images")
        if p_image not in item: task.increment("imaged_items")

    fout.close()

register_task("twitter-extract", TwitterExtract)

class TwitterWorkflow:
  def __init__(self, name=None):
    self.wf = Workflow(name)
    self.data = data.Datasets(self.wf)

  def twitterdb(self):
    """Resource for Twitter database."""
    return self.wf.resource(flags.arg.twitterdb, format="db/json")

  def twitter_frames(self):
    """Resource for twitter frames."""
    return self.wf.resource("twitter-media.sling",
                            dir=corpora.workdir("media"),
                            format="text/frames")

  def extract_twitter(self):
    extractor = self.wf.task("twitter-extract")
    extractor.attach_input("kb", self.data.knowledge_base())
    extractor.attach_input("twitterdb", self.twitterdb())
    extractor.attach_output("output", self.twitter_frames())

# Commands.

def twitter_profiles():
  log.info("Extract twitter profiles")
  wf = TwitterWorkflow("twitter-profiles")
  wf.extract_twitter()
  run(wf.wf)


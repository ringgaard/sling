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

"""SLING Twitter profile service"""

import json
import tweepy

class TwitterService:
  def __init__(self):
    # Read twitter credentials.
    with open("local/keys/twitter.json", "r") as f:
      apikeys = json.load(f)

    # Connect to twitter.
    auth = tweepy.OAuthHandler(apikeys["consumer_key"],
                               apikeys["consumer_secret"])
    auth.set_access_token(apikeys["access_key"], apikeys["access_secret"])
    self.api = tweepy.API(auth)

  def handle(self, request):
    params = request.params()
    user = params["user"][0]
    print("fetch twitter profile for", user)
    return self.api.get_user(user)._json


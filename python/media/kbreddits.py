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

"""Make list of subreddits about items in knowledge base."""

import sling

kb = sling.Store()
kb.load("data/e/kb/kb.sling")

n_subreddit = kb["P3984"]
n_instance_of = kb["P31"]
n_human = kb["Q5"]

def is_human(item):
  for t in item(n_instance_of):
    if t == n_human: return True
  return False

for item in kb:
  if n_subreddit in item:
    for sr in item(n_subreddit):
      sr = kb.resolve(sr)
      if is_human(item):
        print(sr, item.id)


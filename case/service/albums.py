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

"""SLING photo album service"""

import sling
import sling.media.photo as photo

def process_albums(request):
  params = request.params()
  urls = params["url"]
  print("process albums:", urls)

  # Get photos from url(s).
  profile = photo.Profile(None)
  if params.get("captions") is None: profile.captionless = True
  for url in urls: profile.add_media(url, None, False)

  # Return extracted photos.
  return profile.frame


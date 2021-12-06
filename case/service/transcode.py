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

"""SLING video transcoding service"""

import os
import os.path
import tempfile
import urllib.parse
import requests
import sling.net

ffmpeg_options = "-g 24"

def tempfn():
  dir = tempfile._get_default_tempdir()
  base = next(tempfile._get_candidate_names())
  return dir + "/" + base

class TranscodeService:
  def handle(self, request):
    # Get URL for video.
    params = request.params()
    url = params["url"][0]
    print("transcode:", url)

    # Make filenames for temporary files.
    baseurl = url
    if baseurl.endswith("/file"): baseurl = baseurl[0:-5]
    path = urllib.parse.urlparse(baseurl).path
    ext = os.path.splitext(path)[1]
    inputfn = tempfn() + ext
    outputfn = tempfn() + ".mp4"

    # Download video and save to temporary file.
    print("Download", url, "to", inputfn)
    r = requests.get(url)
    r.raise_for_status()
    with open(inputfn, "wb") as f: f.write(r.content)

    # Transcode video using FFMPEG.
    cmd = "ffmpeg -i %s %s %s" % (inputfn, ffmpeg_options, outputfn)
    print("Transcode:", cmd)
    os.system(cmd)

    # Return MP4 file.
    return sling.net.HTTPFile(outputfn, "video/mp4")


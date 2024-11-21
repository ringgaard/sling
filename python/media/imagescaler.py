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

"""Image scaler for thumbnails"""

import io
import urllib3
import time
import PIL
from PIL import Image, JpegImagePlugin, PngImagePlugin, MpoImagePlugin

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8080,
             type=int,
             metavar="PORT")

flags.define("--thumbsize",
             help="default size for thumbnail",
             default=480,
             type=int,
             metavar="SIZE")

flags.define("--mediadb",
             help="database for images",
             default="vault/media",
             metavar="DB")

# Parse command line flags.
flags.parse()

urllib3.disable_warnings()
pool =  urllib3.PoolManager()
mediadb = sling.Database(flags.arg.mediadb)

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)

# Image transpose for rotated image.
transpose_method = {
  2: Image.FLIP_LEFT_RIGHT,
  3: Image.ROTATE_180,
  4: Image.FLIP_TOP_BOTTOM,
  5: Image.TRANSPOSE,
  6: Image.ROTATE_270,
  7: Image.TRANSVERSE,
  8: Image.ROTATE_90,
}

@sling.net.response(PIL.Image.Image)
def image_reponse(image, request, response):
  result = io.BytesIO()
  if image.mode in ("RGBA", "P"):
    image.save(result, format='PNG')
    response.ct = "image/png"
  else:
    image.save(result, format='JPEG', quality=95)
    response.ct = "image/jpeg"
  response.body = result.getvalue()
  request.measure("encode")

@sling.net.response(JpegImagePlugin.JpegImageFile)
def jpeg_reponse(image, request, response):
  result = io.BytesIO()
  image.save(result, format='JPEG', quality=95)
  response.ct = "image/jpeg"
  response.body = result.getvalue()
  request.measure("encode")

@sling.net.response(PngImagePlugin.PngImageFile)
def jpeg_reponse(image, request, response):
  result = io.BytesIO()
  image.save(result, format='PNG')
  response.ct = "image/png"
  response.body = result.getvalue()
  request.measure("encode")

@sling.net.response(MpoImagePlugin.MpoImageFile)
def mpo_reponse(image, request, response):
  return jpeg_reponse(image, request, response)

@app.route("/thumb")
def thumb(request):
  url = request.param("url")
  if url is None: return 400
  size = request.param("size")
  if size is None:
    size = flags.arg.thumbsize
  else:
    size = int(size)

  try:
    # Try to get image from media database.
    imagedata = mediadb[url]
    request.measure("db")

    # Fetch image from source if it is not in the media database.
    if imagedata is None:
      print("download", url)
      r = pool.request("GET", url, timeout=5)
      if r.status != 200: raise Exception("HTTP error %d" % r.status)
      imagedata = r.data
      request.measure("download")

    # Read image.
    image = Image.open(io.BytesIO(imagedata))

    # Handle image orientation in EXIF extension.
    exif = image._getexif()
    if exif is not None:
      orientation = exif.get(0x112, None)
      print("orientation", orientation, url)
      if orientation is not None:
        method = transpose_method.get(orientation)
        if method is not None:
          image = image.transpose(method)

    # Convert to thumbnail.
    image.thumbnail((size, size), Image.LANCZOS)
    request.measure("process")

    # Return scaled image.
    return image

  except Exception as e:
    print("Error scaling", url, ":", e)
    return sling.net.HTTPRedirect(url)

# Run app until shutdown.
log.info("running")
app.run()
log.info("stopped")

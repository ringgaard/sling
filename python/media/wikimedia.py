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

"""Extract Wikimedia filenames from Wikipedia infoboxes"""

import gzip
import hashlib
import html
import urllib.request
from datetime import date, timedelta

import sling
import sling.task.corpora as corpora
import sling.flags as flags
import sling.log as log
from sling.task.workflow import *
from sling.task.wiki import WikiWorkflow

# Known file extensions for media files.
known_extensions = set([
  ".jpg", ".jpeg", ".gif", ".png", ".svg", ".webp", ".bmp",
])
ignored_extensions = set([
  ".tif", ".tiff", ".pdf", ".webm", ".ogv", ".xcf", ".ogg", ".djvu",
])

# Default images for missing images.
default_images = set([
  "Defaut.svg",
  "Défaut-mannequin_(s).png",
  "Null.png",
])

# Compute MD5 hash for string.
def md5hash(s):
  md5 = hashlib.md5()
  md5.update(s.encode("utf8"))
  return md5.hexdigest()

# Make first character uppercase.
def titlecase(s):
  if len(s) == 0 or not s[0].islower(): return s
  return s[0].upper() + s[1:]

# Task for extracting images from Wikipedia infoboxes.
class WikiMediaExtract:
  def run(self, task):
    # Get parameters.
    language = task.param("language")

    # Load knowledge base.
    log.info("Load knowledge base")
    kb = sling.Store()
    kb.load(task.input("kb").name)

    n_infobox = kb["/wp/infobox"]
    n_page_item = kb["/wp/page/item"]
    n_file = kb["/wp/info/file"]
    n_media = kb["/wp/media"]

    image_fields = [
      (kb["/wp/info/image"], kb["/wp/info/caption"]),
      (kb["/wp/info/cover"], kb["/wp/info/caption"]),
      (kb["/wp/info/logo"], kb["/wp/info/logo_caption"]),
      (kb["/wp/info/photo"], kb["/wp/info/photo_caption"]),
      (kb["/wp/info/flag_image"], kb["/wp/info/flag_caption"]),
    ]

    p_media = kb["media"]
    p_id = kb["id"]
    p_is = kb["is"]
    p_imported_from = kb["P143"]
    p_media_legend = kb["P2096"]

    image_properties = [
      kb["P18"],   # image
      kb["P154"],  # logo image
      kb["P41"],   # flag image
    ]

    lang = kb["/lang/" + language]
    wikipedia_item = lang["/lang/wikilang/wikipedia"]

    docschema = sling.DocumentSchema(kb)

    kb.freeze()

    # Fetch media titles for Wikipedia from yesterday.
    log.info("Fetch local media titles")
    yesterday = (date.today() - timedelta(days=1)).strftime("%Y%m%d")
    mediaurl = "https://dumps.wikimedia.org/other/mediatitles/%s/" \
      "%swiki-%s-all-media-titles.gz" % (yesterday, language, yesterday)
    r = urllib.request.urlopen(mediaurl)
    mediatitles = set(gzip.decompress(r.read()).decode().split('\n'))
    task.increment("local_media_files", len(mediatitles))

    # Open output file.
    fout = open(task.output("output").name, "w")

    # Process input articles.
    for res in task.inputs("input"):
      log.info("Extract media files from", res.name)
      for _, data in sling.RecordReader(res.name):
        # Read article into store.
        store = sling.Store(kb)
        doc = store.parse(data)
        task.increment("documents")

        # Find first infobox.
        infobox = None
        for theme in doc(docschema.document_theme):
          if theme.isa(n_infobox):
            infobox = theme
            break
        if infobox is None: continue
        task.increment("infoboxes")

        # Find images in infobox.
        imagelist = []
        for n_image, n_caption in image_fields:
          image = infobox[n_image]
          caption = infobox[n_caption]
          if image is None: continue

          # Get image for repeated image field.
          if type(image) is sling.Frame:
            group = image
            image = group[n_file]
            caption = group[n_caption]
            if image is None: continue

          if "{" in image or "[" in image:
            # Structured annotations.
            annotations = sling.lex(image, store=store, schema=docschema)
            for theme in annotations.themes:
              if theme.isa(n_media):
                image = theme[p_is]
                if image is not None:
                  imagelist.append((image, None))
                  task.increment("structured_annotations")
          else:
            # Image filename.
            imagelist.append((image, caption))
        if len(imagelist) == 0: continue

        # Process list of images for item.
        known_images = 0
        image_frames = []
        item = doc[n_page_item]
        if item is None: continue
        for image, caption in imagelist:
          # Disregard direct URLs for now.
          if image.startswith("http://") or image.startswith("https://"):
            task.increment("url_images")
            continue

          # Trim image name. Remove File: prefix.
          colon = image.find(':')
          if colon > 0 and colon < 10: image = image[colon + 1:]
          image = titlecase(image.strip()).replace('_', ' ')
          if len(image) == 0 or image in default_images:
            task.increment("empty_images")
            continue
          if image.endswith("&lrm;"): image = image[:-5]
          image = html.unescape(image)
          frag = image.find('#')
          if frag > 0: image = image[:frag]

          # Discard media files with unknown or ignored extensions.
          dot = image.rfind('.')
          ext = image[dot:].lower() if dot > 0 else None
          if ext in ignored_extensions:
            task.increment("ignored_image_format")
            continue
          if ext not in known_extensions:
            log.info("unknown format:", item.id, image)
            task.increment("unknown_image_format")
            continue

          # Get item from KB and check if image is already known.
          task.increment("images")
          known = False
          for prop in image_properties:
            for img in item(prop):
              img = kb.resolve(img)
              if img == image: known = True
              known_images += 1
          if known:
            task.increment("known_images")
            continue
          task.increment("new_images")

          # Check if image is in local Wikipedia or Wikimedia Commons.
          fn = image.replace(' ', '_')
          if fn in mediatitles:
            urlbase = "https://upload.wikimedia.org/wikipedia/" + language
            task.increment("local_images")
          else:
            urlbase = "https://upload.wikimedia.org/wikipedia/commons"
            task.increment("commons_images")
            if known_images == 0: task.increment("commons_imaged_items")

          # Compute URL for image.
          md5 = md5hash(fn)
          url = "%s/%s/%s/%s" % (urlbase, md5[0], md5[0:2], fn)

          # Create frame for item with media image.
          slots = [
            (p_is, url),
            (p_imported_from, wikipedia_item),
          ]
          if caption != None:
            capdoc = sling.lex(caption, store=store, schema=docschema)
            captxt = capdoc.phrase(0, len(capdoc.tokens))
            slots.append((p_media_legend, captxt))
          image_frames.append(store.frame(slots))

        # Create item frame with extra image info.
        if len(image_frames) == 0: continue
        slots = [(p_id, item.id)]
        for image_frame in image_frames: slots.append((p_media, image_frame))
        frame = store.frame(slots)
        fout.write(frame.data(utf8=True))
        fout.write("\n")
        if known_images == 0: task.increment("imaged_items")

    fout.close()

register_task("wikimedia-extract", WikiMediaExtract)

class WikiMediaWorkflow:
  def __init__(self, name=None, wf=None):
    if wf == None: wf = Workflow(name)
    self.wf = wf
    self.wiki = WikiWorkflow(wf=wf)

  def image_frames(self, language=None):
    """Resource for media image frames."""
    if language == None: language = flags.arg.language
    return self.wf.resource(language + "wiki-media.sling",
                            dir=flags.arg.workdir + "/media",
                            format="text/frames")

  def extract_media(self, language=None):
    if language == None: language = flags.arg.language
    extractor = self.wf.task("wikimedia-extract")
    extractor.add_params({
      "language": language,
    })
    extractor.attach_input("kb", self.wiki.knowledge_base())
    extractor.attach_input("input", self.wiki.wikipedia_documents(language))
    extractor.attach_output("output", self.image_frames(language))

# Commands.

def extract_wikimedia():
  for language in flags.arg.languages:
    log.info("Extract " + language + " Wikipedia images")
    wf = WikiMediaWorkflow(language + "-wikimedia")
    wf.extract_media(language=language)
    run(wf.wf)


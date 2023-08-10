# Copyright 2023 Ringgaard Research ApS
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

"""Text extraction from documents"""

import re

import sling.extract.epub

extractors = {
  "application/epub+zip": sling.extract.epub.extract,
  ".epub": sling.extract.epub.extract,
}

def handle(request):
  extractor = None
  mime = request["Content-Type"]
  filename = None
  extension = None

  disposition = request["Content-Disposition"]
  if disposition:
    m = re.fullmatch(r'\w+\s*;\s*filename="(.+)\.(\w+)', disposition)
    if m != None:
      filename = m[1] + "." + m[2]
      extension = m[2]

  # Find extractor for document using MIME type or file extension.
  if mime: extractor = extractors.get(mime)
  if extractor is None: extractor = extractors.get(extension)
  if extractor is None: return 415

  # Use extractor for extracting text from document.
  return extractor(request.body, {
    "mime": mime,
    "filename": filename,
    "extension": extension,
  })


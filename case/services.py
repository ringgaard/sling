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

"""SLING case system services"""

import importlib

class Service:
  def __init__(self, package, function):
    module = importlib.import_module("." + package, "service")
    self.handler = getattr(module, function)

services = {
  "albums": Service("albums", "process_albums"),
}

def process(name, request):
  # Find service.
  service = services.get(name)
  if service is None: return 404;

  # Let service process the request.
  return service.handler(request)


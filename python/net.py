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

"""SLING web server micro-framework"""

import json
import os
import threading
import time
import traceback
import sling.pysling as api

# Map of HTTP response formatters for each handler return type.
http_reponse_formatters = {}

# Install HTTP response formatter.
def response(response_type):
  def inner(func):
    http_reponse_formatters[response_type] = func
    return func
  return inner

# Timestamp used for static content.
static_timestamp = time.strftime('%a, %d %b %Y %H:%M:%S GMT', time.gmtime())

class HTTPRequest:
  def __init__(self, method, path, query, headers, body):
    self.method = method
    self.path = path
    self.query = query
    self.headers = headers
    self.body = body

  def __getitem__(self, key):
    """Get HTTP request header"""
    if self.headers is not None:
      key = key.casefold()
      for header in self.headers:
        if key == header[0].casefold(): return header[1]
    return None

class HTTPResponse:
  def __init__(self):
    self.status = 200
    self.headers = None
    self.body = None
    self.file = None

  def __getitem__(self, key):
    """Get HTTP response header"""
    if self.headers is not None:
      key = key.casefold()
      for header in self.headers:
        if key == header[0].casefold(): return header[1]
    return None

  def __setitem__(self, key, value):
    """Set HTTP response header"""
    if self.headers is None: self.headers = []
    self.headers.append((key, value))

  @property
  def ct(self):
    """Get content type"""
    return self["Content-Type"]

  @ct.setter
  def ct(self, value):
    """Set content type"""
    self["Content-Type"] = value

  def error(self, status, message=None):
    self.status = status
    self.ct = "text/plain"
    self.body = message
    self.file = None

  def result(self):
    """Return response tuple"""
    return (self.status, self.headers, self.body, self.file)

class HTTPHandler:
  def __init__(self, func, method="GET", methods=None):
    self.func = func
    self.methods = methods
    if self.methods is None: self.methods = []
    if method is not None: self.methods.append(method)

  def handle(self, method, path, query, headers, body):
    response = HTTPResponse()
    try:
      # Check method.
      if method not in self.methods:
        response.error(405, "Method Not Allowed")
      else:
        # Make request object.
        request = HTTPRequest(method, path, query, headers, body)

        # Run handler.
        ret = self.func(request)

        # Format response.
        formatter = http_reponse_formatters.get(type(ret))
        if formatter is None: raise "No HTTP formatter for " + str(type())
        formatter(ret, request, response)

    except Exception as e:
      # Return error response with stack trace.
      trace = traceback.format_exception(type(e), e, tb=e.__traceback__)
      response.error(500, "".join(trace))

    # Return response.
    return response.result()

class HTTPStatic:
 def __init__(self, ct, content):
   self.ct = ct
   self.content = content

@response(HTTPStatic)
def static_page(value, request, response):
  response.ct = value.ct
  response["Last-Modified"] = static_timestamp
  response.body = value.content

class HTTPFile:
 def __init__(self, filename, ct=None):
   self.filename = filename
   self.ct = ct

@response(HTTPFile)
def file_page(value, request, response):
  response.ct = value.ct
  response.file = value.filename

class HTTPServer:
  def __init__(self, port, addr=""):
    self.httpd = api.HTTPServer(addr, port)
    self.stop = threading.Event()

  def run(self):
    # Start HTTP server.
    self.httpd.start()

    # Wait until shutdown.
    while not self.stop.is_set():
      try:
        self.stop.wait()
      except KeyboardInterrupt:
        self.shutdown()

    # Stop HTTP server.
    self.httpd.stop()

  def shutdown(self):
    # Signal shutdown of HTTP server.
    self.stop.set()

  def static(self, path, dir, internal=False):
    if not internal: dir = os.path.abspath(dir)
    self.httpd.static(path, dir)

  def dynamic(self, path, func):
    self.httpd.dynamic(path, HTTPHandler(func))

  def route(self, path, method="GET"):
    def inner(func):
      self.httpd.dynamic(path, HTTPHandler(func, method))
    return inner

  def page(self, path, content, ct="text/html"):
    static = HTTPStatic(ct, content)
    def inner(request):
      return static
    self.dynamic(path, inner)

  def css(self, path, content, ct="text/css"):
    static = HTTPStatic(ct, content)
    def inner(request):
      return static
    self.dynamic(path, inner)

  def js(self, path, content, ct="text/javascript"):
    static = HTTPStatic(ct, content)
    def inner(request):
      return static
    self.dynamic(path, inner)

@response(bytes)
def binary_page(value, request, response):
  response.body = value

@response(str)
def html_page(value, request, response):
  response.ct = "text/html"
  response.body = value

@response(int)
def error_page(value, request, response):
  response.status = value

@response(dict)
def json_page(value, request, response):
  response.ct = "application/json"
  response.body = json.dumps(value)


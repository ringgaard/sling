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
import os.path
import signal
import socket
import time
import traceback
import urllib.parse
import sling
import sling.log as log
import sling.pysling as api

# Convert timestamp to HTTP time.
def http_time(ts):
  return time.strftime('%a, %d %b %Y %H:%M:%S GMT', time.gmtime(ts))

# Timestamp used for static content.
static_timestamp = http_time(time.time())

# Map of HTTP response formatters for each handler return type.
http_reponse_formatters = {}

# Install HTTP response formatter.
def response(response_type):
  def inner(func):
    http_reponse_formatters[response_type] = func
    return func
  return inner

class HTTPRequest:
  def __init__(self, method, path, query, headers, body):
    self.method = method
    self.path = path
    self.query = query
    self.headers = headers
    self.body = body
    self.qs = None
    self.timing = None
    self.start = time.time()

  def __getitem__(self, key):
    """Get HTTP request header"""
    if self.headers is not None:
      key = key.casefold()
      for header in self.headers:
        if key == header[0].casefold(): return header[1]
    return None

  def params(self):
    # Return query string parameters.
    if self.qs is None: self.qs = urllib.parse.parse_qs(self.query)
    return self.qs;

  def param(self, name):
    params = self.params()
    if name not in params: return None
    return params[name][0]

  def json(self):
    return json.loads(self.body)

  def form(self):
    return urllib.parse.parse_qs(self.body.decode())

  def frame(self, store=None):
    if store is None: store = sling.Store()
    return store.parse(self.body)

  def measure(self, event, start=None):
    end = time.time()
    if start is None: start = self.start
    ms = int((end - start) * 1000)
    if self.timing is None: self.timing = []
    self.timing.append("%s;dur=%d" % (event, ms))
    self.start = end

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

@response(HTTPResponse)
def http_response(value, request, response):
  response.status = value.status
  response.headers = value.headers
  response.body = value.body
  response.file = value.file

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
        if ret is not None:
          formatter = http_reponse_formatters.get(type(ret))
          if formatter is None:
            raise Exception("No HTTP formatter for " + str(type(ret)))
          formatter(ret, request, response)

        # Add timing header.
        if request.timing is not None:
          response["Server-Timing"] = ",".join(request.timing)

    except Exception as e:
      # Return error response with stack trace.
      trace = traceback.format_exception(type(e), e, tb=e.__traceback__)
      errmsg = "".join(trace)
      log.error(errmsg)
      response.error(500, errmsg)

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

class CachedFile:
 def __init__(self, filename, ct=None):
   self.filename = filename
   self.ct = ct
   self.content = None
   self.mtime = 0

@response(CachedFile)
def cached_file(value, request, response):
  mtime = os.path.getmtime(value.filename)
  if mtime > value.mtime: value.content = None
  if value.content is None:
    with open(value.filename, "rb") as f:
      value.content = f.read()
    value.mtime = mtime

  response["Last-Modified"] = http_time(value.mtime)
  response.body = value.content
  response.ct = value.ct

class MemoryFile:
 def __init__(self, content, mtime=None, ct=None):
   self.content = content
   self.ct = ct
   self.mtime = mtime

@response(MemoryFile)
def memory_file(value, request, response):
  response["Last-Modified"] = http_time(value.mtime)
  response.body = value.content
  response.ct = value.ct

class HTTPRedirect:
 def __init__(self, location, status=307):
   self.location = location
   self.status = status

@response(HTTPRedirect)
def redirect_page(value, request, response):
  response.status = value.status
  response["Location"] = value.location

class HTTPServer:
  def __init__(self, port, addr="", cors=False):
    self.httpd = api.HTTPServer(addr, port)
    if cors: self.httpd.cors()
    self.terminate = False

  def start(self):
    self.httpd.start()

  def shutdown(self):
    self.terminate = True
    self.httpd.shutdown()

  def stop(self):
    self.httpd.stop()
    self.httpd = None

  def run(self):
    # Start HTTP server.
    self.start()

    # Wait until shutdown.
    try:
      while not self.terminate: time.sleep(1)
    except KeyboardInterrupt:
      pass

    # Stop HTTP server.
    self.stop()

  def static(self, path, dir, internal=False):
    if not internal: dir = os.path.abspath(dir)
    self.httpd.static(path, dir)

  def dynamic(self, path, func):
    self.httpd.dynamic(path, HTTPHandler(func))

  def route(self, path, method="GET", methods=None):
    def inner(func):
      self.httpd.dynamic(path, HTTPHandler(func, method, methods))
    return inner

  def file(self, path, filename, ct):
    f = CachedFile(filename, ct)
    def inner(request):
      return f
    self.dynamic(path, inner)

  def redirect(self, path, location, status=307):
    redir = HTTPRedirect(location, status)
    def inner(func):
      return redir
    self.dynamic(path, inner)

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

@response(sling.Frame)
def json_page(value, request, response):
  response.ct = "application/sling"
  response.body = value.data(binary=True)

@response(sling.Array)
def json_page(value, request, response):
  response.ct = "application/sling"
  response.body = value.data(binary=True, shallow=False)

checked_hostnames = set(["drive.ringgaard.com"])

def private(url):
  # Check that url is not on local network.
  addr = urllib.parse.urlsplit(url)
  if addr.hostname in checked_hostnames: return False
  ipaddr = socket.gethostbyname(addr.hostname)
  if ipaddr.startswith("10."): return True
  if ipaddr.startswith("192.168."): return True
  if ipaddr.startswith("127."): return True
  checked_hostnames.add(addr.hostname)
  return False


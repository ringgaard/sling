import requests
import time

# HTTP Server-Sent Events (SSE) stream for server event monitoring.
class SSEStream(object):
  def __init__(self, url, last=None, since=None, retry=3000,
               chunk_size=4096, **kwargs):
    self.url = url
    self.last = last
    self.lastts = None
    self.since = since
    self.retry = retry
    self.chunk_size = chunk_size
    self.session = requests.Session()

    # Pass additional named arguments to request.
    self.args = kwargs
    self.headers = self.args.get("headers")
    if self.headers is None:
      self.headers = {}
      self.args["headers"] = self.headers

    self.headers["Cache-Control"] = "no-cache"
    self.headers["Accept"] = "text/event-stream"

  def __iter__(self):
    while True:
      # Send request.
      url = self.url

      if self.lastts:
        t = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(self.lastts - 60))
        print("Restart stream on", t)
        url = url + "?since=" + t
      elif self.last:
        print("Restart stream from", self.last)
        self.headers["Last-Event-ID"] = self.last
      elif self.since:
        print("Restart stream at", self.since)
        url = url + "?since=" + str(self.since)

      r = self.session.get(url, stream=True, **self.args)
      r.raise_for_status()

      # Receive response stream and break it into messages.
      buf = b""
      pos = 0
      for chunk in r.iter_content(self.chunk_size):
        buf = buf + chunk
        while True:
          # Check for end of message.
          n = buf.find(b"\n\n", pos)
          if n == -1: break

          # Get message from buffer.
          msg = buf[pos:n]
          pos = n + 2

          # Create event from raw message.
          event = SSEEvent(msg)
          if event.id: self.last = event.id
          if event.retry: self.retry = event.retry

          # Return next event.
          yield event

        buf = buf[pos:]
        pos = 0

      # Delay before retry.
      time.sleep(self.retry / 1000.0)

# HTTP SSE event.
class SSEEvent(object):
  def __init__(self, msg):
    self.msg = msg
    self.id = None
    self.event = None
    self.data = None
    self.retry = None

    # Parse message.
    for line in msg.split(b'\n'):
      colon = line.find(b':')

      # Ignore comments and non-field lines.
      if colon <= 0: continue

      # Parse field.
      name = line[:colon].strip()
      value = line[colon + 1:].strip()

      if name == b"id":
        self.id = value
      elif name == b"event":
        self.event = value
      elif name == b"data":
        if self.data is None:
          self.data = value
        else:
          self.data = self.data + b'\n' + self.value
      elif name == b"retry":
        self.retry = int(value)

  def __str__():
    return self.msg

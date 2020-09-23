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

import os
import os.path
import socket
import subprocess
import sys
import threading
import time
import queue
import traceback
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlsplit

import sling
import sling.flags as flags
import sling.log as log

flags.define("--tasklist",
             default="local/tasks.sling",
             help="task list")

flags.define("--logdir",
             default="local/logs",
             help="directory for job logs")

flags.define("--port",
             help="port number for the HTTP server",
             default=5050,
             type=int,
             metavar="PORT")

# Get unused TCP port.
def get_free_port():
  s = socket.socket()
  s.bind(('', 0))
  return s.getsockname()[1]

# Get unused job id.
last_job_id = time.strftime("%Y%m%d%H%M%S")
def get_jobid():
  global last_job_id
  while True:
    next_job_id = time.strftime("%Y%m%d%H%M%S")
    if next_job_id != last_job_id:
      last_job_id = next_job_id
      return next_job_id
    time.sleep(1)

# Timestamp to string.
def ts2str(ts):
  return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts))

# Duration to string.
def dur2str(duration):
  days = int(duration / (24 * 60 * 60))
  duration -= days * (24 * 60 * 60)
  hours = int(duration / (60 * 60))
  duration -= hours * (60 * 60)
  mins = int(duration / 60)
  duration -= mins * 60
  secs = int(duration)

  s = []
  if days > 0: s.append(str(days) + "d")
  if hours > 0: s.append(str(hours) + "h")
  if mins > 0: s.append(str(mins) + "m")
  s.append(str(secs) + "s")
  return "".join(s)

class Task:
  def __init__(self, config):
    self.name = config.name
    self.description = config.description
    self.program = None
    self.args = []
    argv = config.program
    if type(argv) is str:
      self.program = argv
    else:
      self.program = argv[0]
      for arg in argv[1:]:
        self.args.append((None, arg))
    if "args" in config:
      for key, value in config.args:
        self.args.append((key.id, value))
    self.queue = config.queue
    self.monitor = config.monitor
    self.statistics = config.statistics

class Job:
  # Job states.
  PENDING = 0
  RUNNING = 1
  COMPLETED = 2
  FAILED = 3

  def __init__(self, task, args):
    self.task = task
    self.queue = None
    self.args = args
    self.id = get_jobid()
    self.port = None
    self.state = Job.PENDING
    self.submitted = time.time()
    self.started = None
    self.ended = None
    self.stdout = None
    self.stderr = None
    self.status = None
    self.error = ""

  def runtime(self):
    if self.started is None:
      return 0
    elif self.ended is None:
      return time.time() - self.started
    else:
      return self.ended - self.started

  def waittime(self):
    if self.submitted is None:
      return 0
    elif self.started is None:
      return time.time() - self.submitted
    else:
      return self.started - self.submitted

  def queuename(self):
    if self.queue is None:
      return ""
    else:
      return self.queue.name

  def hasarg(self, argname):
    if argname is None: return False
    for arg in self.args:
      if arg[0] == argname: return True
    return False

  def command(self):
    cmd = [self.task.program]

    for arg in self.task.args:
      if self.hasarg(arg[0]): continue
      if arg[0] is not None: cmd.append("--" + str(arg[0]))
      if arg[1] is not None: cmd.append(str(arg[1]))

    for arg in self.args:
      if arg[0] is not None: cmd.append("--" + str(arg[0]))
      if arg[1] is not None: cmd.append(str(arg[1]))

    if self.task.monitor:
      if self.port is None: self.port = get_free_port()
      cmd.append("--monitor")
      cmd.append(str(self.port))

    if self.task.statistics:
      cmd.append("--logdir")
      cmd.append(flags.arg.logdir)
      cmd.append("--jobid")
      cmd.append(self.id)

    return cmd

  def __str__(self):
    s = [self.task.name]
    if self.args != None and len(self.args) > 0:
      s.append("(")
      first = True
      for a in self.args:
        if not first: s.append(", ")
        first = False
        if a[0] is not None:
          s.append(a[0])
        if a[1] is not None:
          s.append("=")
          s.append(a[1])
      s.append(")")
    return ''.join(s)

class Queue(threading.Thread):
  def __init__(self, name):
    threading.Thread.__init__(self)

    # Initialize queue for pending jobs.
    self.name = name
    self.pending = queue.Queue(1024)

    # Initialize worker thread.
    self.daemon = True
    self.start()

  def submit(self, job):
    log.info("submit job", job.id, str(job))
    job.queue = self;
    self.pending.put(job)
    jobs.append(job)

  def execute(self, job):
    log.info("execute job", job.id, str(job))
    job.started = time.time()
    job.state = Job.RUNNING

    # Get command for executing job.
    cmd = job.command()

    log.info("command:", job.command())

    # Output files for stdout and strerr.
    job.stdout = flags.arg.logdir + "/" + job.id + ".log"
    job.stderr = flags.arg.logdir + "/" + job.id + ".err"
    if job.task.statistics:
      job.status = flags.arg.logdir + "/" + job.id + ".json"
    out = open(job.stdout, "w")
    err = open(job.stderr, "w")

    # Run job.
    try:
      process = subprocess.run(cmd,
                               stdin=None,
                               stdout=out,
                               stderr=err,
                               bufsize=1,
                               shell=False,
                               close_fds=True)
    except Exception as e:
      job.error = e
    finally:
      out.close()
      err.close()

    # Remove empty log files.
    if os.path.getsize(job.stdout) == 0:
      os.remove(job.stdout)
      job.stdout = None
    if os.path.getsize(job.stderr) == 0:
      os.remove(job.stderr)
      job.stderr = None
    if job.status is not None:
      if not os.path.exists(job.status):
        job.status = None
      elif os.path.getsize(job.status) == 0:
        os.remove(job.status)
        job.status = None

    # Get job results.
    job.ended = time.time()
    if job.error:
      log.error("failed to lauch job", job.id, str(job), job.error)
      job.state = Job.FAILED
    elif process.returncode != 0:
      log.error("job", job.id, str(job), "failed, returned", process.returncode)
      job.state = Job.FAILED
      job.error = "Error " + str(process.returncode)
    else:
      log.info("completed job", job.id, str(job))
      job.state = Job.COMPLETED

  def run(self):
    log.info("job queue", self.name, "ready to execute jobs")
    while True:
      job = self.pending.get()
      try:
        self.execute(job)
      except Exception as e:
        log.info("Error executing job", job.id, ":", e)
        traceback.print_exc()
      finally:
        self.pending.task_done()

tasks = {}
queues = {}
jobs = []

main_queue = Queue("main")
queues["main"] = main_queue
last_task_timestamp = None

def refresh_task_list():
  global last_task_timestamp, tasks
  ts = os.stat(flags.arg.tasklist).st_mtime
  if ts == last_task_timestamp: return

  try:
    tasklist = {}
    store = sling.Store()
    for t in store.load(flags.arg.tasklist):
      tasklist[t.name] = Task(t)
    tasks = tasklist
  except:
    log.info("Error loading task list")
    traceback.print_exc(file=sys.stdout)
    return
  last_task_timestamp = ts
  log.info("Loaded", len(tasks), "tasks")

def submit_job(taskname, queuename, args):
  # Re-read task list if it has changed.
  refresh_task_list()

  # Get task for new job.
  task = tasks.get(taskname)
  if task is None: return None

  # Get queue name for new job.
  if queuename is None:
    queuename = task.queue
    if queuename is None: queuename = "main"

  # Get or create queue for new job.
  queue = queues.get(queuename)
  if queue is None:
    queue = Queue(queuename)
    queues[queuename] = queue

  # Submit job to queue.
  job = Job(task, args)
  queue.submit(job)
  return job

def get_job(jobid):
  for job in jobs:
    if job.id == jobid: return job
  return None

class SchedulerService(BaseHTTPRequestHandler):
  def do_GET(self):
    url = urlsplit(self.path)

    if url.path == '/favicon.ico':
      self.send_response(404)
      return

    if url.path.startswith("/log/"):
      jobid = url.path[5:]
      job = get_job(jobid)
      if job is None:
        self.send_response(500)
        return
      if job.stdout is None:
        self.send_response(404)
        return
      self.return_file(job.stdout, "text/plain")
      return

    if url.path.startswith("/errors/"):
      jobid = url.path[8:]
      job = get_job(jobid)
      if job is None:
        self.send_response(500)
        return
      if job.stderr is None:
        self.send_response(404)
        return
      self.return_file(job.stderr, "text/plain")
      return

    if url.path.startswith("/status/"):
      jobid = url.path[8:]
      job = get_job(jobid)
      if job is None:
        self.send_response(500)
        return
      if job.status is None:
        self.send_response(404)
        return
      self.return_file(job.status, "application/json")
      return

    hostname = self.headers["Host"].split(':')[0]

    self.send_response(200)
    self.send_header("Content-type", "text/html")
    self.end_headers()

    self.out("<html>")
    self.out("<head>")
    self.out("<title>SLING Job Scheduler</title>")
    self.out("<meta charset='UTF-8'>")
    self.out("</head>")
    self.out("<body>")

    self.out("<h1><a href='/'>SLING Job Scheduler</a></h1>")

    self.out("<h2>Running</h2>")
    self.out("<table border=1>")
    self.out("<tr>")
    self.out("<th>Job</th>")
    self.out("<th>Task</th>")
    self.out("<th>Command</th>")
    self.out("<th>Queue</th>")
    self.out("<th>Started</th>")
    self.out("<th>Time</th>")
    self.out("<th>Status</th>")
    self.out("</tr>")
    for job in jobs:
      if job.state != Job.RUNNING: continue;
      self.out("<tr>")
      self.out("<td>" + job.id + "</td>")
      self.out("<td>" + job.task.description + "</td>")
      self.out("<td>" + str(job) + "</td>")
      self.out("<td>" + job.queuename() + "</td>")
      self.out("<td>"+ ts2str(job.started) + "</td>")
      self.out("<td>" + dur2str(job.runtime()) + "</td>")

      self.out("<td>")
      if job.port:
        self.out('<a href="http://%s:%d">status</a> ' % (hostname, job.port))
      if job.stdout:
        self.out('<a href="/log/%s">log</a> ' % (job.id))
      if job.stderr:
        self.out('<a href="/errors/%s">errors</a> ' % (job.id))
      self.out("</td>")

      self.out("</tr>")
    self.out("</table>")

    self.out("<h2>Pending</h2>")
    self.out("<table border=1>")
    self.out("<tr>")
    self.out("<th>Job</th>")
    self.out("<th>Task</th>")
    self.out("<th>Command</th>")
    self.out("<th>Queue</th>")
    self.out("<th>Submitted</th>")
    self.out("<th>Time</th>")
    self.out("</tr>")
    for job in jobs:
      if job.state != Job.PENDING: continue;
      self.out("<tr>")
      self.out("<td>" + job.id + "</td>")
      self.out("<td>" + job.task.description + "</td>")
      self.out("<td>" + str(job) + "</td>")
      self.out("<td>" + job.queuename() + "</td>")
      self.out("<td>"+ ts2str(job.submitted) + "</td>")
      self.out("<td>" + dur2str(job.waittime()) + "</td>")
      self.out("</tr>")
    self.out("</table>")

    self.out("<h2>Done</h2>")
    self.out("<table border=1>")
    self.out("<tr>")
    self.out("<th>Job</th>")
    self.out("<th>Task</th>")
    self.out("<th>Command</th>")
    self.out("<th>Queue</th>")
    self.out("<th>Started</th>")
    self.out("<th>Ended</th>")
    self.out("<th>Time</th>")
    self.out("<th>Status</th>")
    self.out("</tr>")
    for job in reversed(jobs):
      if job.state != Job.COMPLETED and job.state != Job.FAILED: continue;
      if job.state == Job.FAILED:
        self.out("<tr style='color: red;'>")
      else:
        self.out("<tr>")
      self.out("<td>" + job.id + "</td>")
      self.out("<td>" + job.task.description + "</td>")
      self.out("<td>" + str(job) + "</td>")
      self.out("<td>" + job.queuename() + "</td>")
      self.out("<td>"+ ts2str(job.started) + "</td>")
      self.out("<td>"+ ts2str(job.ended) + "</td>")
      self.out("<td>" + dur2str(job.runtime()) + "</td>")

      self.out("<td>")
      if job.stdout:
        self.out('<a href="/log/%s">log</a> ' % (job.id))
      if job.stderr:
        self.out('<a href="/errors/%s">errors</a> ' % (job.id))
      if job.status:
        self.out('<a href="/status/%s">status</a> ' % (job.id))
      if job.error:
        self.out("\n<pre>\n")
        self.out(str(job.error))
        self.out("\n</pre>\n")
      self.out("</td>")

      self.out("</tr>")
    self.out("</table>")

    self.out("</body>")
    self.out("</html>")

  def do_POST(self):
    url = urlsplit(self.path)
    path = url.path[1:].split("/")

    if len(path) < 1:
      self.reply(500, "no command\n")
    elif path[0] == "submit":
      self.do_submit(path, url.query)
    else:
      self.reply(500, "unknown command\n")

  def log_message(self, format, *args):
    log.info("HTTP", self.address_string(), format % args)

  def do_submit(self, path, query):
    # Check for illegal characters in arguments.
    for ch in "|<>;()[]":
      if ch in query:
        self.reply(500, "malformed argument\n")
        return

    # Get job arguments from query string.
    args = []
    for part in query.split("&"):
      if len(part) == 0: continue
      eq = part.find('=')
      if eq == -1:
        args.append((part, None))
      else:
        args.append((part[:eq], part[eq + 1:]))

    # Submit job.
    if len(path) == 2:
      job = submit_job(path[1], None, args)
    elif len(path) == 3:
      job = submit_job(path[2], path[1], args)
    else:
      job = None

    # Reply with job id.
    if job is None:
      self.reply(500, "error submitting job\n")
    else:
      self.reply(200,
                 "job %s submitted to %s queue\n" % (job.id, job.queue.name))

  def return_file(self, filename, mimetype):
    f = open(filename, "rb")
    content = f.read()
    f.close()
    self.send_response(200)
    if mimetype: self.send_header("Content-type", mimetype)
    self.end_headers()
    self.wfile.write(content)

  def out(self, text):
    self.wfile.write(text.encode("utf8"))

  def reply(self, code, message):
    self.send_response(code)
    self.send_header("Content-type", "text/plain")
    self.end_headers()
    self.out(message)

if __name__ == "__main__":
  # Parse command line flags.
  flags.parse()

  # Load task list.
  refresh_task_list()

  # Start web server for submitting and monitoring jobs.
  httpd = HTTPServer(("", flags.arg.port), SchedulerService)
  log.info('job scheduler running: http://localhost:%d/' % flags.arg.port)
  try:
    httpd.serve_forever()
  except KeyboardInterrupt:
    pass
  httpd.server_close()
  log.info("stopped")


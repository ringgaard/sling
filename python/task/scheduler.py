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
import requests
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
  hours = int(duration / 3600)
  mins = int((duration % 3600) / 60)
  secs = int(duration % 60)
  return "%dh %02dm %02ds" % (hours, mins, secs)

class Trigger:
  def __init__(self, config):
    self.task = config.task
    self.machine = config.machine
    self.port = config.port
    if self.port is None: self.port = 5050
    self.queue = config.queue
    self.args = []
    if "args" in config:
      for key, value in config.args:
        self.args.append((key.id, value))

class Task:
  def __init__(self, config):
    self.name = config.name
    self.description = config.description
    self.shell = config.shell == True
    self.program = None
    self.args = []
    argv = config.program
    if type(argv) is str:
      self.program = argv
    elif self.shell:
      self.program = '; '.join(argv)
    else:
      self.program = argv[0]
      for arg in argv[1:]:
        self.args.append((None, arg))
    if "args" in config:
      for key, value in config.args:
        self.args.append((key.id, value))
    self.queue = config.queue
    self.triggers = []
    for trigger in config("trigger"):
      self.triggers.append(Trigger(trigger))
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
    if self.task.shell:
      cmd = self.task.program
      for args in [self.args, self.task.args]:
        for arg in args:
          if arg[0] is None or arg[1] is None: continue
          cmd = cmd.replace("[" + arg[0] + "]", str(arg[1]))
    else:
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
    job.queue = self
    self.pending.put(job)
    jobs.append(job)

  def execute(self, job):
    log.info("execute job", job.id, str(job))
    job.started = time.time()
    job.state = Job.RUNNING

    # Get command for executing job.
    cmd = job.command()
    log.info("command:", cmd)

    # Output files for stdout and strerr.
    job.stdout = flags.arg.logdir + "/" + job.id + ".log"
    job.stderr = flags.arg.logdir + "/" + job.id + ".err"
    if job.task.statistics:
      job.status = flags.arg.logdir + "/" + job.id + ".json"
    out = open(job.stdout, "w")
    err = open(job.stderr, "w")
    out.write("# cmd: %s\n" % str(cmd))
    out.flush()

    # Run job.
    try:
      process = subprocess.run(cmd,
                               stdin=None,
                               stdout=out,
                               stderr=err,
                               bufsize=1,
                               shell=job.task.shell,
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

    # Submit triggers.
    for trigger in job.task.triggers:
      if trigger.machine is None:
        # Submit local job.
        log.info("trigger", trigger.task)
        submit_job(trigger.task, trigger.queue, trigger.args)
      else:
        # Submit remote job.
        log.info("trigger", trigger.task, "on", trigger.machine)
        if trigger.queue is None:
          url = "http://%s:%d/submit/%s" % (
            trigger.machine, trigger.port, trigger.task)
        else:
          url = "http://%s:%d/submit/%s/%s" % (
            trigger.machine, trigger.port, trigger.queue, trigger.task)
        if len(trigger.args) > 0:
          args = []
          for arg in trigger.args:
            if arg[1] is None:
              args.append(arg[0])
            else:
              args.append(arg[0] + "=" + str(arg[1]))
          url = url + "?" + "&".join(args)

        requests.post(url)

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

stylesheet = """
@import url(https://fonts.googleapis.com/css?family=Roboto:400,400italic,500,500italic,700,700italic,900,900italic,300italic,300,100italic,100);

@font-face {
  font-family: 'Material Icons';
  font-style: normal;
  font-weight: 400;
  src: url(https://fonts.gstatic.com/s/materialicons/v55/flUhRq6tzZclQEJ-Vdg-IuiaDsNc.woff2) format('woff2');
}

html {
  width: 100%;
  height: 100%;
  min-height: 100%;
  position:relative;
}

body {
  font-family: Roboto,Helvetica,sans-serif;
  font-size: 14px;
  font-weight: 400;
  line-height: 20px;
  padding: 0;
  margin: 0;
  box-sizing: border-box;

  width: 100%;
  height: 100%;
  min-height: 100%;
  position:relative;
}

.mdt-column-layout {
  display: flex;
  flex-direction: column;
  margin: 0;
  width: 100%;
  height: 100%;
  min-height: 100%;
}

.mdt-toolbar {
  display: flex;
  flex-direction: row;
  align-items: center;
  background-color: #00A0D6;
  color: rgb(255,255,255);
  height: 56px;
  max-height: 56px;
  font-size: 20px;
  padding: 0px 16px;
  margin: 0;
  box-shadow: 0 1px 8px 0 rgba(0,0,0,.2),
              0 3px 4px 0 rgba(0,0,0,.14),
              0 3px 3px -2px rgba(0,0,0,.12);
  z-index: 2;
}

.mdt-icon-button {
  border-radius: 50%;
  border: 0;
  height: 40px;
  width: 40px;
  margin: 0 6px;
  padding: 8px;
  background: transparent;
  user-select: none;
  cursor: pointer;
}

.mdt-icon-button:hover {
  background-color: rgba(0,0,0,0.07);
}

.mdt-toolbar .mdt-icon-button {
  color: rgb(255,255,255);
}

.mdt-toolbar>.mdt-icon-button:first-child {
  margin-left: -8px;
}

.mdt-icon-button:focus {
  outline: none;
}

.mdt-icon {
  font-family: 'Material Icons';
  font-weight: normal;
  font-style: normal;
  font-size: 24px;
  line-height: 1;
  letter-spacing: normal;
  text-transform: none;
  display: inline-block;
  white-space: nowrap;
  word-wrap: normal;
  direction: ltr;
}

.mdt-spacer {
  flex: 1;
}

.mdt-content {
  flex: 1;
  padding: 8px;
  display: block;
  overflow: auto;
  color: rgb(0,0,0);
  background-color: rgb(250,250,250);

  position:relative;

  flex-basis: 0%;
  flex-grow: 1;
  flex-shrink: 1;
}

.mdt-card {
  background-color: rgb(255, 255, 255);
  box-shadow: rgba(0, 0, 0, 0.16) 0px 2px 4px 0px,
              rgba(0, 0, 0, 0.23) 0px 2px 4px 0px;
  margin: 10px 5px;
  padding: 10px;
}

.mdt-card-toolbar {
  display: flex;
  flex-direction: row;
  align-items: center;
  line-height:32px;

  font-size: 24px;
  margin-bottom: 8px;
  margin-top: 3px;
}

.mdt-data-table {
  border: 0;
  border-collapse: collapse;
  white-space: nowrap;
  font-size: 14px;
  text-align: left;
}

.mdt-data-table tr td {
  vertical-align: top;
}

.mdt-data-table thead {
  padding-bottom: 3px;
}

.mdt-left thead {
  text-align: left;
}

.mdt-right thead {
  text-align: right;
}

.mdt-data-table th {
  vertical-align: bottom;
  padding: 8px 12px;
  box-sizing: border-box;
  text-overflow: ellipsis;
  color: rgba(0,0,0,.54);
}

.mdt-data-table td {
  vertical-align: middle;
  border-top: 1px solid rgba(0,0,0,.12);
  border-bottom: 1px solid rgba(0,0,0,.12);
  padding: 8px 12px;
  box-sizing: border-box;
  text-overflow: ellipsis;
}

.mdt-data-table td:first-of-type, .mdt-data-table th:first-of-type {
  padding-left: 24px;
}
"""

job_template = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>%s - SLING Job Scheduler</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<div class="mdt-column-layout">

<div class="mdt-toolbar">
  <button class="mdt-icon-button">
    <i class="mdt-icon">menu</i>
  </button>
  <div>SLING Job Scheduler on %s</div>
  <div class="mdt-spacer"></div>
  <button class="mdt-icon-button">
    <i class="mdt-icon" onclick="window.location.reload()">refresh</i>
  </button>
</div>

<div class="mdt-content">
  <div class="mdt-card">
    <div class="mdt-card-toolbar">Running jobs</div>
      <table class="mdt-data-table">
        <thead>
          <tr>
            <th>Job</th>
            <th>Task</th>
            <th>Command</th>
            <th>Queue</th>
            <th>Started</th>
            <th>Time</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody>
          %s
        </tbody>
      </table>
  </div>

  <div class="mdt-card">
    <div class="mdt-card-toolbar">Pending jobs</div>
      <table class="mdt-data-table">
        <thead>
          <tr>
            <th>Job</th>
            <th>Task</th>
            <th>Command</th>
            <th>Queue</th>
            <th>Submitted</th>
            <th>Time</th>
          </tr>
        </thead>
        <tbody>
          %s
        </tbody>
      </table>
  </div>

  <div class="mdt-card">
    <div class="mdt-card-toolbar">Terminated jobs</div>
      <table class="mdt-data-table">
        <thead>
          <tr>
            <th>Job</th>
            <th>Task</th>
            <th>Command</th>
            <th>Queue</th>
            <th>Started</th>
            <th>Ended</th>
            <th>Time</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody>
          %s
        </tbody>
      </table>
  </div>

</div>

</body>
</html>
"""

class SchedulerService(BaseHTTPRequestHandler):
  def do_GET(self):
    url = urlsplit(self.path)

    if url.path == '/favicon.ico':
      self.reply(404, "no fav icon")
      return

    if url.path == '/style.css':
      self.reply(200, stylesheet, "text/css")
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

    running = []
    pending = []
    terminated = []

    for job in jobs:
      if job.state != Job.RUNNING: continue
      running.extend((
        "\n<tr>",
        "<td>", job.id, "</td>",
        "<td>", job.task.description, "</td>",
        "<td>", str(job), "</td>",
        "<td>", job.queuename(), "</td>",
        "<td>", ts2str(job.started), "</td>",
        "<td>", dur2str(job.runtime()), "</td>",
      ))

      running.append("<td>")
      if job.port:
        statusurl = "http://%s:%d" % (hostname, job.port)
        running.append(
          '<a href="%s" target="_blank">status</a> ' % (statusurl))
      if job.stdout:
        running.append(
          '<a href="/log/%s" target="_blank">log</a> ' % (job.id))
      if job.stderr:
        running.append(
          '<a href="/errors/%s" target="_blank">errors</a> ' % (job.id))
      running.append("</td>")
      running.append("</tr>")

    for job in jobs:
      if job.state != Job.PENDING: continue
      pending.extend((
        "\n<tr>",
        "<td>", job.id, "</td>",
        "<td>", job.task.description, "</td>",
        "<td>", str(job), "</td>",
        "<td>", job.queuename(), "</td>",
        "<td>", ts2str(job.submitted), "</td>",
        "<td>", dur2str(job.waittime()), "</td>",
        "</tr>"
      ))

    for job in reversed(jobs):
      if job.state != Job.COMPLETED and job.state != Job.FAILED: continue
      if job.state == Job.FAILED:
        terminated.append("\n<tr style='background-color: #FCE4EC;'>")
      else:
        terminated.append("\n<tr>")

      terminated.extend((
        "<td>", job.id, "</td>",
        "<td>", job.task.description, "</td>",
        "<td>", str(job), "</td>",
        "<td>", job.queuename(), "</td>",
        "<td>", ts2str(job.started), "</td>",
        "<td>", ts2str(job.ended), "</td>",
        "<td>", dur2str(job.runtime()), "</td>",
      ))

      terminated.append("<td>")
      if job.stdout:
        terminated.append(
          '<a href="/log/%s" target="_blank">log</a> ' % (job.id))
      if job.stderr:
        terminated.append(
          '<a href="/errors/%s" target="_blank">errors</a> ' % (job.id))
      if job.status:
        terminated.append(
          '<a href="/status/%s" target="_blank">status</a> ' % (job.id))
      if job.error: terminated.append(str(job.error));
      terminated.append("</td>")
      terminated.append("</tr>")

    fillers =  (
      hostname,
      hostname,
      "".join(running),
      "".join(pending),
      "".join(terminated)
    )
    self.out(job_template % fillers)
    return


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

  def reply(self, code, message, ct="text/plain"):
    self.send_response(code)
    self.send_header("Content-type", ct)
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


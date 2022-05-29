#!/usr/bin/python3

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
import queue
import requests
import socket
import subprocess
import sys
import threading
import time
import traceback

import sling
import sling.flags as flags
import sling.log as log
import sling.net

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

# Parse command line flags.
flags.parse()

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
    if type(cmd) is list:
      out.write("# cmd: %s\n" % " ".join(cmd))
    else:
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

# Load task list.
refresh_task_list()

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)

# Main page.
app.page("/",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>SLING Job Scheduler</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/common/lib/material.js"></script>
  <script type="module" src="scheduler.js"></script>
</head>
<body style="display: none">
  <scheduler-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">
        SLING Job Scheduler on <md-text id="host"></md-text>
      </div>
      <md-spacer></md-spacer>
      <md-icon-button id="refresh" icon="refresh"></md-icon-button>
    </md-toolbar>

    <md-content>

      <md-card id="running">
        <md-card-toolbar>
          <div>Running jobs</div>
        </md-card-toolbar>

        <md-data-table id="running-jobs">
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="started">Started</md-data-field>
          <md-data-field field="time">Time</md-data-field>
          <md-data-field field="status" html=1>Status</md-data-field>
        </md-data-table>
      </md-card>

      <md-card id="pending">
        <md-card-toolbar>
          <div>Pending jobs</div>
        </md-card-toolbar>

        <md-data-table id="pending-jobs">
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="submitted">Submitted</md-data-field>
          <md-data-field field="time">Time</md-data-field>
        </md-data-table>
      </md-card>

      <md-card id="terminated">
        <md-card-toolbar>
          <div>Terminated jobs</div>
        </md-card-toolbar>

        <md-data-table id="terminated-jobs">
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="started">Started</md-data-field>
          <md-data-field field="ended">Ended</md-data-field>
          <md-data-field field="time" style="text-align: right">Time</md-data-field>
          <md-data-field field="status" html=1>Status</md-data-field>
        </md-data-table>
      </md-card>

    </md-content>
  </scheduler-app>
</body>
</html>
""")

app.js("/scheduler.js",
"""
import {Component} from "/common/lib/component.js";
import {MdApp} from "/common/lib/material.js";

class SchedulerApp extends MdApp {
  onconnected() {
    this.bind("#refresh", "click", (e) => this.onrefresh(e));
    window.document.title = `${this.host()} - SLING Job Scheduler`;
    this.find("#host").update(this.host());
    this.reload();
  }

  onrefresh(e) {
    this.reload()
  }

  reload() {
    fetch("/jobs").then(response => response.json()).then((data) => {
      this.update(data);
    }).catch(err => {
      console.log('Error fetching jobs', err);
    });
  }

  onupdate() {
    this.find("#running-jobs").update(this.state.running);
    this.find("#pending-jobs").update(this.state.pending);
    this.find("#terminated-jobs").update(this.state.terminated);
  }

  host() {
    if (window.location.port == 5050) {
      return window.location.hostname;
    } else {
      return window.location.hostname + ":" + window.location.port;
    }
  };
}

Component.register(SchedulerApp);

document.body.style = null;
""")

@app.route("/jobs")
def jobs_page(request):
  running = []
  pending = []
  terminated = []

  for job in jobs:
    if job.state == Job.RUNNING:
      status = ""
      if job.port:
        hostname = request["Host"]
        if hostname is None: hostname = "localhost"
        if ':' in hostname: hostname =  hostname[:hostname.find(':')]
        statusurl = "http://%s:%d" % (hostname, job.port)
        status += '<a href="%s" target="_blank">status</a> ' % statusurl
      if job.stdout:
        status += '<a href="/log/%s" target="_blank">log</a> ' % job.id
      if job.stderr:
        status += '<a href="/errors/%s" target="_blank">errors</a> ' % job.id

      running.append({
        "job": job.id,
        "task": job.task.description,
        "command": str(job),
        "queue": job.queuename(),
        "started": ts2str(job.started),
        "time": dur2str(job.runtime()),
        "status": status
      })
    elif job.state == Job.PENDING:
      pending.append({
        "job": job.id,
        "task": job.task.description,
        "command": str(job),
        "queue": job.queuename(),
        "submitted": ts2str(job.submitted),
        "time": dur2str(job.waittime())
      })
    else:
      style = None
      if job.state == Job.FAILED: style = "background-color: #FCE4EC;"

      status = ""
      if job.stdout:
        status += '<a href="/log/%s" target="_blank">log</a> ' % job.id
      if job.stderr:
        status += '<a href="/errors/%s" target="_blank">errors</a> ' % job.id
      if job.status:
        status += '<a href="/status/%s" target="_blank">status</a> ' % job.id
      if job.error:
        status += str(job.error)

      terminated.append({
        "job": job.id,
        "task": job.task.description,
        "command": str(job),
        "queue": job.queuename(),
        "started": ts2str(job.started),
        "ended": ts2str(job.ended),
        "time": dur2str(job.runtime()),
        "status": status,
        "style": style
      })

  terminated.reverse()

  return {
    "running": running,
    "pending": pending,
    "terminated": terminated
  }

@app.route("/log")
def log_page(request):
  jobid = request.path[1:]
  job = get_job(jobid)
  if job is None: return 500
  if job.stdout is None: return 404;
  return sling.net.HTTPFile(job.stdout, "text/plain; charset=utf-8")

@app.route("/errors")
def errors_page(request):
  jobid = request.path[1:]
  job = get_job(jobid)
  if job is None: return 500
  if job.stderr is None: return 404;
  return sling.net.HTTPFile(job.stderr, "text/plain; charset=utf-8")

@app.route("/status")
def status_page(request):
  jobid = request.path[1:]
  job = get_job(jobid)
  if job is None: return 500
  if job.status is None: return 404;
  return sling.net.HTTPFile(job.status, "text/plain; charset=utf-8")

@app.route("/submit", method="POST")
def submit_command(request):
  # Get job and optionally queue from path.
  path = request.path[1:].split("/")

  args = []
  if request.query is not None:
    # Check for illegal characters in arguments.
    for ch in "|<>;()[]":
      if ch in request.query: return 500

    # Get job arguments from query string.
    for part in request.query.split("&"):
      if len(part) == 0: continue
      eq = part.find('=')
      if eq == -1:
        args.append((part, None))
      else:
        args.append((part[:eq], part[eq + 1:]))

  # Submit job.
  if len(path) == 1:
    job = submit_job(path[0], None, args)
  elif len(path) == 2:
    job = submit_job(path[1], path[1], args)
  else:
    job = None

  # Reply with job id.
  if job is None: return 500
  return "job %s submitted to %s queue\n" % (job.id, job.queue.name)

restart = False

@app.route("/restart", method="POST")
def restart_command(request):
  global restart
  log.info("Restarting scheduler")
  restart = True
  app.shutdown()
  return "restarting job scheduler...\n"

# Run app until shutdown.
app.run()
if restart:
  log.info("restart")
  os.execv(sys.argv[0], sys.argv)
else:
  log.info("stopped")


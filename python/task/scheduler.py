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
import signal
import sys
import threading
import time
import traceback

import sling
import sling.flags as flags
import sling.log as log
import sling.net
import sling.task.alert as alert

flags.define("--tasklist",
             default="local/tasks.sling",
             help="task list")

flags.define("--logpath",
             default="local/logs",
             help="directory for job logs")

flags.define("--port",
             help="port number for the HTTP server",
             default=5050,
             type=int,
             metavar="PORT")

# Scheduler status.
tasks = {}
queues = {}
jobs = []

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
  days = int(duration / 86400)
  hours = int((duration % 86400) / 3600)
  mins = int((duration % 3600) / 60)
  secs = int(duration % 60)
  if days > 0:
    return "%dd %dh %02dm %02ds" % (days, hours, mins, secs)
  else:
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
    self.daemon = config.daemon == True
    self.port = config.port
    self.program = None
    self.args = []
    argv = config.program
    if argv is None:
      self.program = None
    elif type(argv) is str:
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

    self.dependencies = []
    for dependency in config("await"):
      self.dependencies.append(dependency)

    self.triggers = []
    for trigger in config("trigger"):
      self.triggers.append(Trigger(trigger))

    self.monitor = config.monitor
    self.statistics = config.statistics

class Job:
  # Job states.
  PENDING   = 0
  PAUSED    = 1
  WAITING   = 2
  RUNNING   = 3
  COMPLETED = 4
  FAILED    = 5
  DAEMON    = 6
  CANCELED  = 7

  def __init__(self, task, args):
    self.task = task
    self.queue = None
    self.args = args
    self.id = get_jobid()
    self.port = task.port
    self.state = Job.PENDING
    self.process = None
    self.submitted = time.time()
    self.ready = None
    self.started = None
    self.ended = None
    self.stdout = None
    self.stderr = None
    self.status = None
    self.error = ""
    self.awaits = None

  def runtime(self):
    if self.started is None:
      return 0
    elif self.ended is None:
      return time.time() - self.started
    else:
      return self.ended - self.started

  def readytime(self):
    if self.ready is None:
      return 0
    else:
      return time.time() - self.ready

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

  def noop(self):
    return self.task.program is None

  def command(self):
    if self.task.shell:
      cmd = self.task.program
      for args in [self.args, self.task.args]:
        for arg in args:
          if arg[0] is None or arg[1] is None: continue
          cmd = cmd.replace("[" + arg[0] + "]", str(arg[1]))
    elif self.task.program:
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

      if self.task.daemon and self.port != None:
        cmd.append("--port")
        cmd.append(str(self.port))

      if self.task.statistics:
        cmd.append("--logdir")
        cmd.append(flags.arg.logpath)
        cmd.append("--jobid")
        cmd.append(self.id)
    else:
      cmd = None

    return cmd

  def run(self, out=None, err=None):
    # Get command for executing job.
    cmd = self.command()
    self.started = time.time()
    self.state = Job.RUNNING

    # Start process.
    self.process = subprocess.Popen(
        cmd,
        stdin=None,
        stdout=out,
        stderr=err,
        bufsize=1,
        shell=self.task.shell,
        close_fds=True)

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
    # Skip job if it has been canceled.
    if job.state == Job.CANCELED: return

    # Wait if job is paused.
    while job.state == Job.PAUSED: time.sleep(5)
    job.ready = time.time()

    # Wait for dependencies.
    for dependency in job.task.dependencies:
      log.info(job.task.name, "waiting for", dependency)
      job.state = Job.WAITING
      job.awaits = dependency
      queue = queues.get(dependency)
      if queue: queue.wait()
      job.awaits = None

    if job.noop():
      job.started = time.time()
      rc = 0
    else:
      # Get command for executing job.
      log.info("execute job", job.id, str(job))
      cmd = job.command()
      log.info("command:", cmd)

      # Output files for stdout and strerr.
      job.stdout = flags.arg.logpath + "/" + job.id + ".log"
      job.stderr = flags.arg.logpath + "/" + job.id + ".err"
      if job.task.statistics:
        job.status = flags.arg.logpath + "/" + job.id + ".json"
      out = open(job.stdout, "w")
      err = open(job.stderr, "w")
      if type(cmd) is list:
        out.write("# cmd: %s\n" % " ".join(cmd))
      else:
        out.write("# cmd: %s\n" % str(cmd))
      out.flush()

      # Run job.
      try:
        job.run(out, err)
        job.process.wait()
      except Exception as e:
        job.error = e
      finally:
        rc = job.process.returncode
        job.process = None
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
      alert.send("Job %s failed to launch" % str(job),
        "Error launching job %s %s on %s: %s" %
          (job.id, str(job), socket.gethostname(), job.error))
    elif rc != 0:
      log.error("job", job.id, str(job), "failed, returned", rc)
      job.state = Job.FAILED
      if rc < 0:
        job.error = "signal " + signal.Signals(-rc).name
      else:
        job.error = "error " + str(rc)
      alert.send("Job %s failed" % str(job),
        "Job %s %s failed on %s: %s" %
          (job.id, str(job), socket.gethostname(), job.error))
    else:
      log.info("completed job", job.id, str(job))
      job.state = Job.COMPLETED

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

  def wait(self):
    # Wait for all jobs on queue to finish.
    self.pending.join()

def get_queue(name):
  queue = queues.get(name)
  if queue is None:
    queue = Queue(name)
    queues[name] = queue
  return queue

main_queue = get_queue("main")
last_task_timestamp = None

def get_job(jobid):
  for job in jobs:
    if job.id == jobid: return job
  return None

def get_job_for_task(taskname, state):
  for job in jobs:
    if job.state == state and job.task.name == taskname: return job
  return None

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

  # Get queue for new job.
  if queuename is None:
    queuename = task.queue
    if queuename is None: queuename = "main"
  queue = get_queue(queuename)

  # Submit job to queue.
  job = Job(task, args)
  queue.submit(job)
  return job

def pause_job(jobid):
  # Get job for jobid.
  job = get_job(jobid)
  if job is None: return None

  # Put pending job on hold.
  if job.state != Job.PENDING: return None
  job.state = Job.PAUSED
  return job

def resume_job(jobid):
  # Get job for jobid.
  job = get_job(jobid)
  if job is None: return None

  # Resume job.
  if job.state != Job.PAUSED: return None
  job.state = Job.PENDING
  return job

def cancel_job(jobid):
  # Get job for jobid.
  job = get_job(jobid)
  if job is None: return None

  if job.state == Job.RUNNING:
    # Terminate running job.
    job.process.terminate()
  elif job.state == Job.PENDING:
    jobs.remove(job)
    job.state = Job.CANCELED
  else:
    return None

  return job

def start_daemon(taskname, args):
  # Re-read task list if it has changed.
  refresh_task_list()

  # Check that task is not already running.
  if get_job_for_task(taskname, Job.DAEMON):
    log.error("daemon already running for", taskname)
    return None

  # Get task for daemon.
  task = tasks.get(taskname)
  if task is None: return None
  if not task.daemon: return None

  # Log file for stdout and strerr.
  job = Job(task, args)
  job.stdout = flags.arg.logpath + "/" + job.task.name + ".log"
  out = open(job.stdout, "a")

  # Start job as daemon.
  job.run(out, subprocess.STDOUT)
  job.state = Job.DAEMON
  out.close()
  jobs.append(job)
  log.info("started daemon", job.id, job.task.name, "pid", job.process.pid)
  return job

def stop_daemon(taskname):
  # Find current running daemon.
  job = get_job_for_task(taskname, Job.DAEMON)
  if job is None:
    log.error("no daemon is running for", taskname)
    return None

  # Stop daemon.
  log.info("stop daemon", job.id, job.task.name, "pid", job.process.pid)
  job.process.terminate()
  job.process.wait()
  job.state = Job.COMPLETED
  job.ended = time.time()
  job.process = None
  jobs.remove(job)
  return job

# Load task list.
refresh_task_list()

# Initialize web server.
app = sling.net.HTTPServer(flags.arg.port)
app.static("/common", "app", internal=True)
app.static("/dashboard", "sling/task/app", internal=True)

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

      <job-card id="daemons">
        <md-card-toolbar>
          <div>Daemons</div>
        </md-card-toolbar>

        <md-data-table>
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="pid">PID</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Service</md-data-field>
          <md-data-field field="started">Started</md-data-field>
          <md-data-field field="time">Time</md-data-field>
        </md-data-table>
      </job-card>

      <job-card id="running">
        <md-card-toolbar>
          <div>Running jobs</div>
        </md-card-toolbar>

        <md-data-table>
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="pid">PID</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="started">Started</md-data-field>
          <md-data-field field="time">Time</md-data-field>
          <md-data-field field="status" html=1>Status</md-data-field>
        </md-data-table>
      </job-card>

      <job-card id="waiting">
        <md-card-toolbar>
          <div>Waiting jobs</div>
        </md-card-toolbar>

        <md-data-table>
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="ready">Ready</md-data-field>
          <md-data-field field="time">Time</md-data-field>
          <md-data-field field="awaits">Awaits</md-data-field>
        </md-data-table>
      </job-card>

      <job-card id="pending">
        <md-card-toolbar>
          <div>Pending jobs</div>
        </md-card-toolbar>

        <md-data-table>
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="submitted">Submitted</md-data-field>
          <md-data-field field="time">Time</md-data-field>
        </md-data-table>
      </job-card>

      <job-card id="terminated">
        <md-card-toolbar>
          <div>Terminated jobs</div>
        </md-card-toolbar>

        <md-data-table>
          <md-data-field field="job">Job</md-data-field>
          <md-data-field field="task">Task</md-data-field>
          <md-data-field field="command">Command</md-data-field>
          <md-data-field field="queue">Queue</md-data-field>
          <md-data-field field="started">Started</md-data-field>
          <md-data-field field="ended">Ended</md-data-field>
          <md-data-field field="time" style="text-align: right">Time</md-data-field>
          <md-data-field field="status" html=1>Status</md-data-field>
        </md-data-table>
      </job-card>

    </md-content>
  </scheduler-app>
</body>
</html>
""")

app.js("/scheduler.js",
"""
import {Component} from "/common/lib/component.js";
import {MdApp, MdCard} from "/common/lib/material.js";

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
    this.find("#daemons").update(this.state.daemons);
    this.find("#running").update(this.state.running);
    this.find("#waiting").update(this.state.waiting);
    this.find("#pending").update(this.state.pending);
    this.find("#terminated").update(this.state.terminated);
  }

  host() {
    if (window.location.port == 5050) {
      return window.location.hostname;
    } else {
      return window.location.hostname + ":" + window.location.port;
    }
  }
}

Component.register(SchedulerApp);

class JobCard extends MdCard {
  visible() { return this.state && this.state.length > 0; }

  onupdate() {
    this.find("md-data-table").update(this.state);
  }
}

Component.register(JobCard);

document.body.style = null;
""")

# Job report page.
app.page("/report",
"""
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name=viewport content="width=device-width, initial-scale=1">
  <title>SLING Job Report</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script src="https://www.gstatic.com/charts/loader.js"></script>
  <script type="module" src="/common/lib/material.js"></script>
  <script type="module" src="/dashboard/dashboard.js"></script>
</head>
<body style="display: none">
  <md-app id="app">
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">
        SLING Job Report for <md-text id="jobid"></md-text>
      </div>
    </md-toolbar>

    <md-content>
      <dashboard-status id="status"></dashboard-status>
    </md-content>
  </md-app>
</body>
<script type="module">
  let jobid = window.location.pathname.match(/\/report\/(\d+)/)[1];
  fetch("/status/" + jobid)
  .then(response => response.json())
  .then((data) => {
    document.getElementById("jobid").update(jobid);
    document.getElementById("status").update(data);
  });
</script>
</html>
""")

@app.route("/jobs")
def jobs_request(request):
  daemons = []
  running = []
  waiting = []
  pending = []
  terminated = []

  for job in jobs:
    if job.state == Job.DAEMON:
      daemons.append({
        "job": job.id,
        "task": job.task.description,
        "command": str(job),
        "started": ts2str(job.started),
        "time": dur2str(job.runtime()),
        "pid": job.process.pid,
      })
    elif job.state == Job.WAITING:
      waiting.append({
        "job": job.id,
        "task": job.task.description,
        "command": str(job),
        "ready": ts2str(job.ready),
        "time": dur2str(job.readytime()),
        "awaits": job.awaits,
      })
    elif job.state == Job.RUNNING:
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
        "status": status,
        "pid": job.process.pid,
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
    elif job.state == Job.PAUSED:
      pending.append({
        "job": job.id,
        "task": job.task.description + " [ON HOLD]",
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
        status += '<a href="/report/%s" target="_blank">report</a> ' % job.id
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
  del terminated[500:]

  return {
    "daemons": daemons,
    "running": running,
    "waiting": waiting,
    "pending": pending,
    "terminated": terminated,
  }

@app.route("/summary")
def summary_request(request):
  summary = {}
  for job in jobs:
    summary[job.state] = summary.get(job.state, 0) + 1

  return {
    "running": summary.get(Job.RUNNING, 0),
    "waiting": summary.get(Job.WAITING, 0),
    "pending": summary.get(Job.PENDING, 0),
    "completed": summary.get(Job.COMPLETED, 0),
    "failed": summary.get(Job.FAILED, 0),
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
  return sling.net.HTTPFile(job.status, "application/json")

def parse_args(request):
  args = []
  if request.query is not None:
    # Check for illegal characters in arguments.
    for ch in "|<>;()[]":
      if ch in request.query: return None

    # Get job arguments from query string.
    for part in request.query.split("&"):
      if len(part) == 0: continue
      eq = part.find('=')
      if eq == -1:
        args.append((part, None))
      else:
        args.append((part[:eq], part[eq + 1:]))
  return args

@app.route("/submit", method="POST")
def submit_command(request):
  # Get job and optionally queue from path.
  path = request.path[1:].split("/")

  args = parse_args(request)
  if args is None: return 500

  # Submit job.
  if len(path) == 1:
    job = submit_job(path[0], None, args)
  elif len(path) == 2:
    job = submit_job(path[1], path[0], args)
  else:
    job = None

  # Reply with job id.
  if job is None: return 500
  return "job %s submitted to %s queue\n" % (job.id, job.queue.name)

@app.route("/pause", method="POST")
def stop_command(request):
  # Get job id from path.
  jobid = request.path[1:]

  # Pause job.
  job = pause_job(jobid)

  # Reply with job id.
  if job is None: return 500
  return "job %s paused\n" % (job.id)

@app.route("/resume", method="POST")
def stop_command(request):
  # Get job id from path.
  jobid = request.path[1:]

  # Resume job.
  job = resume_job(jobid)

  # Reply with job id.
  if job is None: return 500
  return "job %s resumed\n" % (job.id)

@app.route("/cancel", method="POST")
def cancel_command(request):
  # Get job id from path.
  jobid = request.path[1:]

  # Cancel job.
  job = cancel_job(jobid)

  # Reply with job id.
  if job is None: return 500
  return "job %s cancelled\n" % (job.id)

@app.route("/start", method="POST")
def start_command(request):
  # Get service task from path.
  taskname = request.path[1:]
  args = parse_args(request)
  if args is None: return 500

  # Start daemon.
  job = start_daemon(taskname, args)

  # Reply with job id.
  if job is None: return 500
  return "daemon %s started for %s\n" % (job.id, job.task.name)

@app.route("/stop", method="POST")
def stop_command(request):
  # Get service task from path.
  taskname = request.path[1:]

  # Stop daemon.
  job = stop_daemon(taskname)

  # Reply with job id.
  if job is None: return 500
  return "daemon %s stopped for %s\n" % (job.id, job.task.name)

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


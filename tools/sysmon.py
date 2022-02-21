#!/usr/bin/python3

# Copyright 2022 Ringgaard Research ApS
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

"""SLING system monitor"""

import os
import sys
import time
import requests

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8888,
             type=int,
             metavar="PORT")

flags.define("--schedulers",
             help="list of machines with job schedulers")

flags.define("--monitors",
             help="list of machines with monitors")

# Parse command line flags.
flags.parse()

# Initialize web server.
log.info("Starting system monitor on port", flags.arg.port)
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
  <title>SLING System Monitor</title>
  <link rel="icon" href="/common/image/appicon.ico" type="image/x-icon" />
  <script type="module" src="/common/lib/material.js"></script>
  <script type="module" src="sysmon.js"></script>
</head>
<body style="display: none">
  <monitor-app id="app">
    <md-column-layout>

      <md-toolbar>
        <md-toolbar-logo></md-toolbar-logo>
        <div id="title">SLING System Monitor</div>
        <md-spacer></md-spacer>
        <md-icon-button id="refresh" icon="refresh"></md-icon-button>
      </md-toolbar>

      <md-content>

        <md-card id="alerts">
          <md-card-toolbar>
            <div>Alerts</div>
          </md-card-toolbar>

          <md-data-table id="alert-table">
            <md-data-field field="time">Time</md-data-field>
            <md-data-field field="machine" html=1>Machine</md-data-field>
            <md-data-field field="alert">Alert</md-data-field>
          </md-data-table>
        </md-card>

        <md-card id="schedulers">
          <md-card-toolbar>
            <div>Jobs</div>
          </md-card-toolbar>

          <md-data-table id="scheduler-table">
            <md-data-field field="link" html=1>Machine</md-data-field>
            <md-data-field field="status">Status</md-data-field>
            <md-data-field field="running" class="right">Running</md-data-field>
            <md-data-field field="done" class="right">Done</md-data-field>
            <md-data-field field="failed" class="right">Failed</md-data-field>
          </md-data-table>
        </md-card>

        <md-card id="monitors">
          <md-card-toolbar>
            <div>Monitors</div>
          </md-card-toolbar>

          <md-data-table id="monitor-table">
            <md-data-field field="link" html=1>Machine</md-data-field>
            <md-data-field field="status">Status</md-data-field>
            <md-data-field field="os">OS</md-data-field>
            <md-data-field field="arch">Arch</md-data-field>
            <md-data-field field="cpus" class="right">CPUs</md-data-field>
            <md-data-field field="ram" class="right">RAM</md-data-field>
            <md-data-field field="probes" class="right">Probes</md-data-field>
            <md-data-field field="failures" class="right">Failures</md-data-field>
          </md-data-table>
        </md-card>

      </md-content>

    </md-column-layout>
  </monitor-app>
</body>
</html>
""")

app.js("/sysmon.js",
"""
import {Component} from "/common/lib/component.js";

class MonitorApp extends Component {
  onconnected() {
    this.bind("#refresh", "click", (e) => this.onrefresh(e));
    this.reload();
  }

  onrefresh(e) {
    this.reload()
  }

  reload() {
    fetch("/status").then(response => response.json()).then((data) => {
      this.update(data);
    }).catch(err => {
      console.log('Error fetching status', err);
    });
  }

  onupdate() {
    this.find("#alert-table").update(this.state.alerts);
    this.find("#scheduler-table").update(this.state.schedulers);
    this.find("#monitor-table").update(this.state.monitors);
  }

  static stylesheet() {
    return `
      td.right {
        text-align: right;
      }
    `;
  }
}

Component.register(MonitorApp);

document.body.style = null;
""")

session = requests.Session()
commons = sling.Store()

n_running = commons["running"]
n_pending = commons["pending"]
n_terminated = commons["terminated"]
n_style = commons["style"]
n_ended = commons["ended"]
n_command = commons["command"]

n_monit = commons["monit"]
n_platform = commons["platform"]
n_name = commons["name"]
n_release = commons["release"]
n_machine = commons["machine"]
n_cpu = commons["cpu"]
n_memory = commons["memory"]
n_service = commons["service"]
n_status = commons["status"]
n_collected_sec = commons["collected_sec"]

commons.freeze()

status = {
  "alerts": [],
  "schedulers": {},
  "monitors": {},
}

schedulers = flags.arg.schedulers.split(",")
for s in schedulers:
  status["schedulers"][s] = {
    "machine": s,
    "link": '<a href="http://%s:5050/">%s</a>' % (s, s),
  }

monitors = flags.arg.monitors.split(",")
for m in monitors:
  status["monitors"][m] = {
    "machine": m,
    "link": '<a href="http://%s:2812/">%s</a>' % (m, m),
  }

def refresh():
  # Clear alert list.
  alerts = status["alerts"]
  alerts.clear()

  # Refresh jobs scheduler status.
  for s in schedulers:
    scheduler = status["schedulers"][s]
    try:
      url = "http://" + s + ":5050"
      r = session.get(url + "/jobs")
      r.raise_for_status()
      store = sling.Store(commons)
      data = store.parse(r.content, json=True)
      failed = 0
      done = 0
      for job in data[n_terminated]:
        if job[n_style]:
          failed += 1
          alerts.append({
            "time": job[n_ended],
            "machine": '<a href="%s">%s</a>' % (url, s),
            "alert": "Job %s failed" % (job[n_command])
          })
        else:
          done += 1
      scheduler["status"] = "OK"
      scheduler["running"] = len(data[n_running])
      scheduler["pending"] = len(data[n_pending])
      scheduler["done"] = done
      scheduler["failed"] = failed
    except Exception as e:
      scheduler["status"] = str(e)

  # Refresh monitor status.
  for m in monitors:
    monitor = status["monitors"][m]
    try:
      url = "http://" + m + ":2812"
      r = session.get(url + "/_status?format=xml")
      r.raise_for_status()
      store = sling.Store(commons)
      data = store.parse(r.content, xml=True)
      monit = data[n_monit]

      # Get system information.
      platform = monit[n_platform]
      if platform:
        monitor["os"] = platform[n_name] + " " + platform[n_release]
        monitor["arch"] = platform[n_machine]
        monitor["cpus"] = platform[n_cpu]
        monitor["ram"] = str(round(int(platform[n_memory]) / 1048576)) + " GB"

      # Get status for services.
      probes = 0
      failures = 0
      for service in monit(n_service):
        probes += 1
        st = int(service[n_status])
        if st != 0:
          failures += 1
          tm = time.gmtime(int(service[n_collected_sec]))
          alerts.append({
            "time": time.strftime("%Y-%m-%d %H:%M:%S", tm),
            "machine": '<a href="%s">%s</a>' % (url, m),
            "alert": "Probe %s alarm" % (service["name"])
          })

      monitor["status"] = "OK"
      monitor["probes"] = probes
      monitor["failures"] = failures

    except Exception as e:
      monitor["status"] = str(e)

@app.route("/status")
def status_page(request):
  refresh()
  return status

restart = False

@app.route("/restart", method="POST")
def restart_command(request):
  global restart
  log.info("Restarting monitor")
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


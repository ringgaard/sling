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
import asyncore
import smtpd
import email
import email.policy
import traceback

import sling
import sling.flags as flags
import sling.log as log
import sling.net

flags.define("--port",
             help="port number for the HTTP server",
             default=8888,
             type=int,
             metavar="PORT")

flags.define("--smtpport",
             help="port number for the SMTP server",
             default=2525,
             type=int,
             metavar="PORT")

flags.define("--cfg",
             help="system monitor configuration",
             default="sysmon.sling")

flags.define("--mbox",
             help="file for writing received messages",
             default="mbox",
             metavar="FILE")

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
    <md-toolbar>
      <md-toolbar-logo></md-toolbar-logo>
      <div id="title">SLING System Monitor</div>
      <md-spacer></md-spacer>
      <md-icon-button id="refresh" icon="refresh"></md-icon-button>
    </md-toolbar>

    <md-content>

      <md-card id="alerts">
        <md-text id="status"></md-text>
        <md-data-table id="alert-table">
          <md-data-field field="time">Time</md-data-field>
          <md-data-field field="source" html=1>Source</md-data-field>
          <md-data-field field="alert" html=1>Alert</md-data-field>
          <md-data-field field="ack" class="ack" html=1>Ack</md-data-field>
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
          <md-data-field field="pending" class="right">Pending</md-data-field>
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
  </monitor-app>
</body>
</html>
""")

app.js("/sysmon.js",
"""
import {Component} from "/common/lib/component.js";
import {MdApp} from "/common/lib/material.js";

class MonitorApp extends MdApp {
  onconnected() {
    this.bind("#refresh", "click", (e) => this.onrefresh(e));
    this.bind("#alerts", "click", (e) => this.onack(e));
    this.reload();
  }

  onrefresh(e) {
    this.reload()
  }

  async onack(e) {
    let button = e.target.closest("md-icon-button");
    if (!button) return;
    let alert_type = button.getAttribute("alerttype");
    let alert_id = button.getAttribute("alertid");
    await fetch(`/ack?type=${alert_type}&id=${alert_id}`, {method: "POST"})
    this.reload();
  }

  reload() {
    fetch("/status").then(response => response.json()).then((data) => {
      this.update(data);
    }).catch(err => {
      console.log('Error fetching status', err);
    });
  }

  alarms() {
    let n = this.state.alerts.length;
    for (let m of Object.values(this.state.monitors)) {
      n += m.failures;
    }
    return n;
  }

  onupdate() {
    this.find("#alert-table").update(this.state.alerts);
    this.find("#scheduler-table").update(this.state.schedulers);
    this.find("#monitor-table").update(this.state.monitors);

    let alarms = this.alarms();
    let status = this.find("#status");
    if (alarms == 0) {
      status.update("ALL SYSTEMS GO");
      status.className = "success"
    } else if (alarms == 1) {
      status.update("1 ALARM");
      status.className = "failure";
    } else {
      status.update(`${alarms} ALARMS`);
      status.className = "failure";
    }
  }

  static stylesheet() {
    return `
      $ td {
        vertical-align: top;
      }
      td.right {
        text-align: right;
      }
      $ md-icon-button {
        color: #808080;
      }
      $ td.ack {
        padding: 0px;
      }
      $ #status {
        display: block;
        text-align: center;
        margin-bottom: 10px;
        padding: 10px;
        font-size: 20px;
        font-weight: bold;
        color: white;
      }
      $ .success {
        background-color: green;
      }
      $ .failure {
        background-color: red;
      }
    `;
  }
}

Component.register(MonitorApp);

document.body.style = null;
""")

session = requests.Session()
commons = sling.Store()

config = commons.load(flags.arg.cfg)

n_running = commons["running"]
n_pending = commons["pending"]
n_completed = commons["completed"]
n_failed = commons["failed"]

n_monit = commons["monit"]
n_idsym = commons["_id"]
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

# Received messages.
inbox = []

# Current system status.
status = {}

def refresh():
  # Add alerts from inbox.
  alerts = []
  for m in inbox:
    if m["ack"]: continue
    mail = m["mail"]
    sender = m["from"]
    tm = time.localtime(m["time"])
    subject = mail["Subject"]
    body = mail.get_payload()
    alerts.append({
      "time": time.strftime("%Y-%m-%d %H:%M:%S", tm),
      "source": sender,
      "alert": (subject + "\n\n" + body).replace("\n", "<br>"),
      "ack": '<md-icon-button icon="delete" alerttype="inbox" alertid="' +
             str(m["msgid"]) + '"></md-icon-button>',
    })

  # Refresh jobs scheduler status.
  schedulers = []
  for s in config.schedulers:
    scheduler = {
      "machine": s.machine,
      "link": '<a href="%s">%s</a>' % (s.url, s.machine),
    }
    schedulers.append(scheduler)
    try:
      r = session.get(s["url"] + "/summary")
      r.raise_for_status()
      store = sling.Store(commons)
      summary = store.parse(r.content, json=True)
      scheduler["status"] = "OK"
      scheduler["running"] = summary[n_running]
      scheduler["pending"] = summary[n_pending]
      scheduler["done"] = summary[n_completed]
      scheduler["failed"] = summary[n_failed]
    except Exception as e:
      scheduler["status"] = str(e)
      scheduler["failed"] = 1

  # Refresh monitor status.
  monitors = []
  for m in config.monitors:
    monitor = {
      "machine": m.machine,
      "link": '<a href="%s">%s</a>' % (m.url, m.machine),
    }
    monitors.append(monitor)
    try:
      r = session.get(m["url"] + "/_status?format=xml")
      r.raise_for_status()
      store = sling.Store(commons)
      data = store.parse(r.content, xml=True, idsym=n_idsym)
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
        if st != 0: failures += 1

      monitor["status"] = "OK"
      monitor["probes"] = probes
      monitor["failures"] = failures

    except Exception as e:
      monitor["status"] = str(e)

  global status
  status = {
    "alerts": alerts,
    "schedulers": schedulers,
    "monitors": monitors,
  }

def notify(subject, message):
  for n in config.notifications:
    if n.type == "telegram":
      url = f"https://api.telegram.org/bot{n.token}"
      params = {
        "chat_id": n.chat,
        "text": subject + "\n" + message,
      }
      session.get(url + "/sendMessage", params=params)
    else:
      log.error("unknown notification type:", n.type)

@app.route("/status")
def status_page(request):
  refresh()
  return status

@app.route("/ack", method="POST")
def ack_handler(request):
  acktype = request.param("type")
  ackid = request.param("id")
  log.info("acknowledge alarm", acktype, ackid)
  if acktype == "job":
    acked_job_alerts.add(ackid)
  elif acktype == "inbox":
    for m in inbox:
      if m["msgid"] == ackid:
        m["ack"] = True
        break
  else:
    return 404

next_inbox_id = 0

# SMTP server for receiveing alert messages.
class AlertServer(smtpd.SMTPServer):
  def process_message(self, peer, mailfrom, rcpttos, data, **kwargs):
    try:
      global next_inbox_id;
      log.info("message received from", mailfrom)
      mail = email.message_from_bytes(data, policy=email.policy.default)
      now = time.asctime(time.gmtime(time.time()))
      mail.set_unixfrom("From %s %s" % (mailfrom, now))

      # Write message to mail box.
      mbox = open(flags.arg.mbox, "ab")
      mbox.write(mail.as_bytes(unixfrom=True))
      mbox.write(b"\n\n")
      mbox.close()

      # Add message to inbox.
      inbox.append({
        "mail": mail,
        "time": time.time(),
        "from": mailfrom,
        "msgid": str(next_inbox_id),
        "ack": False,
      })
      next_inbox_id += 1

      # Send notifications.
      notify(mail["Subject"], mail.get_payload())

    except Exception as e:
      log.error("Error receiving message:", e)
      traceback.print_exc()

smtpsrv = AlertServer(("0.0.0.0", flags.arg.smtpport), None)

# Run app until shutdown.
try:
  if flags.arg.smtpport != 0:
    app.start()
    asyncore.loop()
    app.stop()
  else:
    app.run()
except KeyboardInterrupt:
  pass

log.info("stopped")


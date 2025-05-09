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
import urllib3
import asyncio
import aiosmtpd.controller
import email
import email.policy
import traceback
import socket

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
          <md-data-field field="waiting" class="right">Waiting</md-data-field>
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

      <md-card id="controllers">
        <md-card-toolbar>
          <div>Machines</div>
        </md-card-toolbar>

        <md-data-table id="controller-table">
          <md-data-field field="machine">Machine</md-data-field>
          <md-data-field field="status">Status</md-data-field>
          <md-data-field field="since">Since</md-data-field>
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
    this.find("#controller-table").update(this.state.controllers);

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

pool =  urllib3.PoolManager()

commons = sling.Store()

config = commons.load(flags.arg.cfg)

n_running = commons["running"]
n_waiting = commons["waiting"]
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

machines = {}
for c in config.controllers:
  machines[c.machine] = {
    "host": c.host,
    "hwa": c.hwa,
    "online": c.online,
    "last": int(time.time()),
  }

commons.freeze()

# Received messages.
inbox = []

# Current system status.
status = {}

def offline(machine):
  if machine in machines: return not machines[machine]["online"]
  return False

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
    if offline(s.machine): continue
    scheduler = {
      "machine": s.machine,
      "link": '<a href="%s">%s</a>' % (s.url, s.machine),
    }
    schedulers.append(scheduler)
    try:
      r = pool.request("GET", s["url"] + "/summary", timeout=2)
      store = sling.Store(commons)
      summary = store.parse(r.data, json=True)
      scheduler["status"] = "OK"
      scheduler["running"] = summary[n_running]
      scheduler["waiting"] = summary[n_waiting]
      scheduler["pending"] = summary[n_pending]
      scheduler["done"] = summary[n_completed]
      scheduler["failed"] = summary[n_failed]
    except Exception as e:
      scheduler["status"] = "Error"
      scheduler["failed"] = 1
      traceback.print_exc()

  # Refresh monitor status.
  monitors = []
  for m in config.monitors:
    if offline(m.machine): continue
    monitor = {
      "machine": m.machine,
      "link": '<a href="%s">%s</a>' % (m.url, m.machine),
    }
    monitors.append(monitor)
    try:
      r = pool.request("GET", m["url"] + "/_status?format=xml", timeout=2)
      store = sling.Store(commons)
      data = store.parse(r.data, xml=True, idsym=n_idsym)
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
      monitor["status"] = "Error"
      monitor["failures"] = 1
      traceback.print_exc()

  # Refresh machine controller status.
  controllers = []
  for c in config.controllers:
    mach = machines[c.machine]
    controller = {
      "machine": c.machine,
      "status": "Online" if mach["online"] else "Offline",
      "since": time.asctime(time.localtime(mach["last"])),
    }
    controllers.append(controller)

  global status
  status = {
    "alerts": alerts,
    "schedulers": schedulers,
    "monitors": monitors,
    "controllers": controllers,
  }

def notify(subject, message):
  for n in config.notifications:
    if n.type == "telegram":
      url = f"https://api.telegram.org/bot{n.token}"
      params = {
        "chat_id": n.chat,
        "text": subject + "\n" + message,
      }
      pool.request("GET", url + "/sendMessage", fields=params)
    else:
      log.error("unknown notification type:", n.type)

def wake_on_lan(macaddr):
  octets = []
  for octet in macaddr.split(":"): octets.append(int(octet, 16))
  hwa =  bytes(octets)
  msg = b"\xff\xff\xff\xff\xff\xff" + hwa * 16

  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
  sock.sendto(msg, ("255.255.255.255", 9))
  time.sleep(1)
  sock.sendto(msg, ("255.255.255.255", 9))
  time.sleep(1)
  sock.sendto(msg, ("255.255.255.255", 9))
  sock.close()

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

@app.route("/on", method="POST")
def on_handler(request):
  machine = request.param("machine")
  log.info("machine online", machine)
  mach = machines[machine]
  if mach is None: return 404

  wake_on_lan(mach["hwa"])

  mach["online"] = True
  mach["last"] = int(time.time())

@app.route("/off", method="POST")
def on_handler(request):
  machine = request.param("machine")
  log.info("machine offline", machine)
  mach = machines[machine]
  if mach is None: return 404

  cmd = "ssh %s sudo systemctl suspend --no-wall" % mach["host"]
  os.system(cmd)

  mach["online"] = False
  mach["last"] = int(time.time())

next_inbox_id = 0

# SMTP server for receiveing alert messages.
class AlertServer:
  async def handle_RCPT(self, server, session, envelope, address, rcpt_options):
    envelope.rcpt_tos.append(address)
    return '250 OK'

  async def handle_DATA(self, server, session, envelope):
    global next_inbox_id;
    mailfrom = envelope.mail_from
    log.info("message received from", mailfrom)
    mail = email.message_from_bytes(envelope.content,
                                    policy=email.policy.default)
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


    return '250 Message accepted for delivery'

if flags.arg.smtpport != 0:
  log.info("Starting system alert server on port", flags.arg.smtpport)
  controller = aiosmtpd.controller.Controller(AlertServer(),
                                              hostname="",
                                              port=flags.arg.smtpport)
  controller.start()

# Run app until shutdown.
try:
  if flags.arg.smtpport != 0:
    app.start()
    asyncio.new_event_loop().run_forever()
    app.stop()
  else:
    app.run()
except KeyboardInterrupt:
  pass

log.info("stopped")

// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Workflow dashboard app.

import {Component} from "/common/lib/component.js";
import {MdApp, MdCard} from "/common/lib/material.js";

function pad(num, width) {
  return ("0000" + num).slice(-width);
}

function int(num) {
  if (!isFinite(num)) return "--";
  return Math.round(num).toString();
}

function dec(num, decimals) {
  if (!isFinite(num)) return "---";
  return num.toLocaleString("en", {
    minimumFractionDigits: decimals, maximumFractionDigits: decimals });
}

function sort(array, key) {
  return array.sort((a, b) => {
    let x = a[key];
    let y = b[key];
    return ((x < y) ? -1 : ((x > y) ? 1 : 0));
  });
}

let gcharts_loaded = false;

async function gcharts() {
  if (gcharts_loaded) return;
  google.charts.load('current', {'packages':['corechart']});
  return new Promise((resolve, reject) => {
    google.charts.setOnLoadCallback(() => {
      gcharts_loaded = true;
      resolve();
    });
  });
}

class DashboardApp extends MdApp {
  constructor() {
    super();
    this.auto = false;
    this.freq = 15;
    this.ticks = 0;
  }

  onconnected() {
    this.bind("#play5", "click", (e) => this.onplay(e, 5));
    this.bind("#play10", "click", (e) => this.onplay(e, 10));
    this.bind("#play30", "click", (e) => this.onplay(e, 30));
    this.bind("#pause", "click", (e) => this.onpause(e));
    this.bind("#refresh", "click", (e) => this.onrefresh(e));
    this.find("#title").innerHTML = "Job status for " + this.host();
    this.find("#done").update(false);
    this.find("#error").update(false);
    this.reload();
    setInterval(() => this.tick(), 1000);
  }

  onplay(e, interval) {
    this.ticks = 0;
    this.auto = true;
    this.freq = interval;
    this.find("#pause").enable();
    this.reload();
  }

  onpause(e) {
    this.auto = false;
    this.find("#pause").disable();
  }

  tick() {
    this.ticks++;
    if (this.auto && this.ticks % this.freq == 0) {
      this.reload();
    }
  }

  onrefresh(e) {
    this.reload();
  }

  reload() {
    // Fetch status from server.
    fetch("/status").then(response => response.json()).then((data) => {
      // Update job status.
      this.find("#status").update(data);

      // Check for workflow completed.
      if (data.finished) this.done(true);
    }).catch(err => {
      console.log('Error fetching status', err);
      this.done(false);
    });
  }

  host() {
    return window.location.hostname + ":" + window.location.port;
  };

  updateTitle(msg) {
    window.document.title = msg + " - SLING jobs on " + this.host();
  }

  done(success) {
    if (success) {
      this.updateTitle("Done");
      this.find("#done").update(true);
    } else {
      this.updateTitle("Error");
      this.find("#error").update(true);
    }

    this.auto = false;
    for (const id of ["#play5", "#play10", "#play30", "#pause", "#refresh"]) {
      this.find(id).style.display = "none";
    }
  }
}

Component.register(DashboardApp);

class DashboardStatus extends Component {
  constructor(state) {
    super(state);
    this.jobs = [];
    this.selected = 0;
    this.resources = null;
  }

  select(jobid) {
    this.selected = jobid;
    this.state.selected = jobid;
    this.update(this.state);
  }

  onupdated() {
    let status = this.state;

    // Update resource usage.
    let runtime = status.time - status.started;
    let res = {
      time: status.time,
      cpu: (status.utime + status.stime) / 1e6,
      gflops: status.flops / 1e9,
      ram: status.mem,
      io: status.ioread + status.iowrite,
      disk: status.filerd + status.filewr,
      net: status.netrx + status.nettx,
    };
    var census = {};
    census.hours = Math.floor(runtime / 3600);
    census.mins = Math.floor((runtime % 3600) / 60);
    census.secs = Math.floor(runtime % 60);
    census.temp = status.temperature;
    if (status.finished) {
      census.cpu = res.cpu / runtime;
      census.gflops = res.gflops / runtime;
      census.ram = res.ram;
      census.io = res.io / runtime;
      census.disk = res.disk / runtime;
      census.net = res.net / runtime;
    } else if (this.resources) {
      let dt = res.time - this.resources.time;
      census.cpu = (res.cpu - this.resources.cpu) / dt;
      census.gflops = (res.gflops - this.resources.gflops) / dt;
      census.ram = res.ram;
      census.io = (res.io - this.resources.io) / dt;
      census.disk = (res.disk - this.resources.disk) / dt;
      census.net = (res.net - this.resources.net) / dt;
    }
    this.resources = res;

    // Update job list.
    for (let i = 0; i < status.jobs.length; ++i) {
      let jobstatus = status.jobs[i];

      // Create new job if needed.
      let job = this.jobs[i];
      if (job == undefined) {
        job = {};
        job.id = i;
        job.name = jobstatus.name;
        job.prev_counters = null;
        job.prev_channels = null;
        job.prev_time = null;
        this.jobs[i] = job;
        this.selected = i;
      }

      // Compute elapsed time for job.
      let ended = jobstatus.ended ? jobstatus.ended : status.time;
      let elapsed = ended - jobstatus.started;
      job.hours = Math.floor(elapsed / 3600);
      job.mins = Math.floor((elapsed % 3600) / 60);
      job.secs = Math.floor(elapsed % 60);

      // Compute task progress for job.
      let progress = "";
      if (jobstatus.stages) {
        for (let j = 0; j < jobstatus.stages.length; ++j) {
          let stage = jobstatus.stages[j];
          if (j > 0) progress += "│ ";
          progress += "█ ".repeat(stage.done);
          progress += "░ ".repeat(stage.tasks - stage.done);
        }
      } else {
        progress = "✔";
      }
      job.progress = progress;

      // Process job counters.
      let counters = [];
      let channels = [];
      let channel_map = {};
      let prev_counters = job.prev_counters;
      let prev_channels = job.prev_channels;
      let period = status.time - job.prev_time;
      let channel_pattern = /(input|output)_(.+)\[(.+\..+)\]/;
      for (name in jobstatus.counters) {
        // Check for channel stat counter.
        let m = name.match(channel_pattern);
        if (m) {
          // Look up channel.
          var direction = m[1];
          var metric = m[2];
          var channel_name = m[3];
          var ch = channel_map[channel_name];
          if (ch == null) {
            ch = {};
            ch.name = channel_name;
            ch.direction = direction;
            channel_map[channel_name] = ch;
            channels.push(ch);
          }

          // Update channel metrics.
          var value = jobstatus.counters[name];
          if (metric == "key_bytes") {
            ch.key_bytes = value;
          } else if (metric == "value_bytes") {
            ch.value_bytes = value;
          } else if (metric == "messages") {
            ch.messages = value;
          } else if (metric == "shards") {
            ch.shards_total = value;
          } else if (metric == "shards_done") {
            ch.shards_done = value;
          }
        } else {
          // Add counter.
          let item = {name: name, value: jobstatus.counters[name]};
          if (jobstatus.ended) {
            item.rate = item.value / elapsed;
          } else if (prev_counters && period > 0) {
            let prev_value = prev_counters[name];
            let delta = item.value - prev_value;
            item.rate = delta / period;
          }
          counters.push(item);
        }
      }

      // Compute bandwidth and throughput.
      if (prev_channels) {
        for (let j = 0; j < channels.length; ++j) {
          let ch = channels[j];
          let prev = prev_channels[ch.name];
          if (jobstatus.ended) {
            ch.bandwidth = (ch.key_bytes + ch.value_bytes) / elapsed;
            ch.throughput = ch.messages / elapsed;
          } else if (prev) {
            let current_bytes = ch.key_bytes + ch.value_bytes;
            let prev_bytes = prev.key_bytes + prev.value_bytes;
            ch.bandwidth = (current_bytes - prev_bytes) / period;
            ch.throughput = (ch.messages - prev.messages) / period;
          }
        }
      }

      // Update job.
      job.counters = counters;
      job.channels = channels;
      job.prev_counters = jobstatus.counters;
      job.prev_channels = channel_map;
      job.prev_time = status.time;
    }

    this.find("#jobs").update({jobs: this.jobs, selected: this.selected});
    this.find("#counters").update(this.jobs[this.selected].counters);
    this.find("#channels").update(this.jobs[this.selected].channels);
    this.find("#perf").update(census);
    this.find("#charts").update(status.history);
  }

  prerender() {
    return `
      <md-row-layout>
        <dashboard-jobs id="jobs">
          <md-data-table id="job-table">
            <md-data-field field="select" html=true style="padding: 0"></md-data-field>
            <md-data-field field="job">Job</md-data-field>
            <md-data-field field="name">Name</md-data-field>
            <md-data-field field="time" style="text-align: right">Time</md-data-field>
            <md-data-field field="status" style="word-break: break-all; white-space: normal">Status</md-data-field>
          </md-data-table>
        </dashboard-jobs>
        <md-spacer></md-spacer>
        <dashboard-perf id="perf"></dashboard-perf>
      </md-row-layout>
      <dashboard-counters id="counters">
        <md-data-table id="counter-table">
          <md-data-field field="counter">Counter</md-data-field>
          <md-data-field field="value" style="text-align: right">Value</md-data-field>
          <md-data-field field="rate" style="text-align: right">Rate (/s)</md-data-field>
        </md-data-table>
      </dashboard-counters>
      <dashboard-channels id="channels">
        <md-data-table id="channel-table">
          <md-data-field field="channel">Channel</md-data-field>
          <md-data-field field="direction">Direction</md-data-field>
          <md-data-field field="key_bytes" style="text-align: right">Key bytes</md-data-field>
          <md-data-field field="value_bytes" style="text-align: right">Value bytes</md-data-field>
          <md-data-field field="bandwidth" style="text-align: right">Bandwidth</md-data-field>
          <md-data-field field="messages" style="text-align: right">Messages</md-data-field>
          <md-data-field field="throughput" style="text-align: right">Throughput</md-data-field>
          <md-data-field field="shards" style="text-align: right">Shards</md-data-field>
        </md-data-table>
      </dashboard-channels>
      <dashboard-charts id="charts">
        <div id="cpuchart"></div>
        <div id="ramchart"></div>
        <div id="tempchart"></div>
        <div id="iochart"></div>
        <div id="diskchart"></div>
      </dashboard-charts>
    `;
  }
}

Component.register(DashboardStatus);

class DashboardJobs extends MdCard {
  onupdate() {
    // Build data table for job list.
    let table = [];
    for (let i = 0; i < this.state.jobs.length; ++i) {
      let job = this.state.jobs[i];
      table.push({
        job: "#" + job.id,
        name: job.name,
        time: `${job.hours}h ${pad(job.mins, 2)}m ${pad(job.secs, 2)}s`,
        status: job.progress,
        select: `<md-radio-button
                     name="job"
                     value="${i}"
                     selected=${this.state.selected == i}>
                 </md-radio-button>`
      });
    }

    // Update job list table.
    this.find("#job-table").update(table);
  }

  onconnected() {
    this.bind(null, "change", (e) => this.onchange(e));
  }

  onchange(e) {
    // Select job.
    this.match("dashboard-status").select(e.target.value);
  }

  static stylesheet() {
    return `
      $ {
        margin: 10px;
      }
    `;
  }
}

Component.register(DashboardJobs);

class DashboardPerf extends Component {
  render() {
    let c = this.state;
    if (!c) return "";
    let time = c.hours + ":" + pad(c.mins, 2) + ":" + pad(c.secs, 2);
    return `
      <table>
        <tbody>
          <tr>
            <td>TIME</td>
            <td class="lcd" colspan=2>${time}
            </td>
          </tr>
          <tr>
            <td>CPU</td>
            <td class="lcd">${int(c.cpu * 100)}</td>
            <td>%</td>
          </tr>
          <tr>
            <td>RAM</td>
            <td class="lcd">${int(c.ram / (1024 * 1024))}</td>
            <td>MB</td>
          </tr>
          <tr>
            <td>FILE</td>
            <td class="lcd">${int(c.disk / 1000000)}</td>
            <td>MB/s</td>
          </tr>
          <tr>
            <td>NET</td>
            <td class="lcd">${int(c.net / 1000000)}</td>
            <td>MB/s</td>
          </tr>
          <tr>
            <td>TEMP</td>
            <td class="lcd">${int(c.temp)}</td>
            <td>°C</td>
          </tr>
          <tr>
            <td>DISK</td>
            <td class="lcd">${int(c.io / 1000)}</td>
            <td>KIOPS</td>
          </tr>
          <tr>
            <td>GPU</td>
            <td class="lcd">${int(c.gflops)}</td>
            <td>GFLOPS</td>
          </tr>
        </tbody>
      </table>
    `;
  }

  static stylesheet() {
    return `
      @font-face {
        font-family: lcd;
        src: url(/common/font/digital-7.mono.ttf);
      }

      $ {
        margin-top: 10px;
        margin-right: 20px;
      }

      $ table {
        padding: 4px;
        width: 160px;
        margin-left: 8px;
        margin-bottom: 8px;
        margin-top: 8px;
        border-radius: 4px;
        box-shadow: inset 0px 0px 24px 2px rgba(0,0,0,0.2);
        color: #303060;
        background: #BAC2B6;
        font-family: arial;
        font-weight: normal;
        font-size: 12pt;
        text-shadow: 1px 1px 4px rgba(150, 150, 150, 1);
      }

      $ td {
        vertical-align: baseline;
      }

      .lcd {
        font-family: lcd;
        font-size: 20pt;
        font-weight: normal;
        text-align: right;
        width: 100%;
      }
    `
  }
}

Component.register(DashboardPerf);

class DashboardCounters extends MdCard {
  visible() { return this.state && this.state.length > 0; }

  onupdated() {
    // Build data table for counter list.
    let table = [];
    for (const counter of sort(this.state, "name")) {
      table.push({
        counter: counter.name.replaceAll("_", " "),
        value: dec(counter.value, 0),
        rate: dec(counter.rate, 0),
      });
    }

    this.find("#counter-table").update(table);
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        margin-left: 10px;
        margin-right: 10px;
      }
    `;
  }
}

Component.register(DashboardCounters);

class DashboardChannels extends MdCard {
  visible() { return this.state && this.state.length > 0; }

  onupdated() {
    // Build data table for counter list.
    let table = [];
    for (const channel of sort(this.state, "name")) {
      table.push({
        channel: channel.name,
        direction: channel.direction,
        key_bytes: dec(channel.key_bytes, 0),
        value_bytes: dec(channel.value_bytes, 0),
        bandwidth: dec(channel.bandwidth / 1e6, 3) + " MB/s",
        messages: dec(channel.messages, 0),
        throughput: dec(channel.throughput, 0) + " MPS",
        shards: `${channel.shards_done}/${channel.shards_total}`,
      });
    }

    this.find("#channel-table").update(table);
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        margin-left: 10px;
        margin-right: 10px;
      }
    `;
  }
}

Component.register(DashboardChannels);

class DashboardCharts extends MdCard {
  visible() { return this.state && this.state.length > 0; }

  async onupdated() {
    await gcharts();
    if (!this.cpu_chart) this.cpu_chart = this.linechart("#cpuchart");
    if (!this.ram_chart) this.ram_chart = this.linechart("#ramchart");
    if (!this.temp_chart) this.temp_chart = this.linechart("#tempchart");
    if (!this.io_chart) this.io_chart = this.linechart("#iochart");
    if (!this.disk_chart) this.disk_chart = this.linechart("#diskchart");

    let history = this.state;

    let cpu = new google.visualization.DataTable();
    cpu.addColumn("date", "Time");
    cpu.addColumn("number", "CPU");

    let ram = new google.visualization.DataTable();
    ram.addColumn("date", "Time");
    ram.addColumn("number", "RAM");

    let temp = new google.visualization.DataTable();
    temp.addColumn("date", "Time");
    temp.addColumn("number", "TEMP");

    let io = new google.visualization.DataTable();
    io.addColumn("date", "Time");
    io.addColumn("number", "RD");
    io.addColumn("number", "WR");
    io.addColumn("number", "RX");
    io.addColumn("number", "TX");

    let disk = new google.visualization.DataTable();
    disk.addColumn("date", "Time");
    disk.addColumn("number", "IOPS");

    for (let h of history) {
      let d = new Date(h.t * 1000);
      cpu.addRow([d, h.cpu / 1e6]);
      ram.addRow([d, h.ram]);
      temp.addRow([d, h.temp]);
      io.addRow([d, h.rd, h.wr, h.rx, h.tx]);
      disk.addRow([d, h.io]);
    }

    this.cpu_chart.draw(cpu);
    this.ram_chart.draw(ram);
    this.temp_chart.draw(temp);
    this.io_chart.draw(io);
    this.disk_chart.draw(disk);
  }

  linechart(parent) {
    return new google.visualization.LineChart(this.find(parent));
  }

  static stylesheet() {
    return `
      $ {
        display: block;
        margin-left: 10px;
        margin-right: 10px;
      }
    `;
  }
}

Component.register(DashboardCharts);


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLINGDB admin app.

import {Component} from "/common/lib/component.js";
import {MdApp, MdCard} from "/common/lib/material.js";

function int(num) {
  if (!isFinite(num)) return "---";
  return Math.round(num).toString();
}

function dec(num, decimals) {
  if (!isFinite(num)) return "---";
  return num.toLocaleString("en", {
    minimumFractionDigits: decimals, maximumFractionDigits: decimals });
}

function yn(bool) {
  return bool ? "yes" : "no";
}

function host() {
  if (window.location.port == 7070) {
    return window.location.hostname;
  } else {
    return window.location.hostname + ":" + window.location.port;
  }
}

class DbApp extends MdApp {
  onconnected() {
    this.bind("#refresh", "click", (e) => this.refresh());
    this.bind("#tabs", "click", (e) => this.onselect(e));
    this.find("#title").innerHTML = "SLINGDB on " + host();
  }

  onselect(e) {
    this.show(e.target.id);
  }

  show(tab) {
    let router = this.find("#router");
    if (tab == "databases") {
      router.update("#databases-card", {});
    } else if (tab == "connections") {
      router.update("#connections-card", {});
    } else if (tab == "statistics") {
      router.update("#statistics-card", {});
    }
  }

  refresh() {
    let router = this.find("#router");
    if (router.active && router.active.refresh) {
      router.active.refresh();
    }
  }
}

Component.register(DbApp);

class DbDatabasesCard extends MdCard {
  onconnected() {
    this.refresh();
  }

  onupdate() {
    if (this.state) this.refresh();
  }

  refresh() {
    fetch("/", {method: "OPTIONS"})
    .then(response => response.json())
    .then((data) => {
      let table = [];
      for (const db of data["databases"]) {
        table.push({
          name: db.name,
          dir: db.dbdir,
          records: dec(db.records, 0),
          size: dec(db.size, 0),
          shards: dec(db.shards, 0),
          dirty: yn(db.dirty),
          bulk: yn(db.bulk),
          deletions: dec(db.deletions, 0),
          index_capacity: dec(db.index_capacity, 0),
          epoch: db.epoch,
        });
      }
      this.find("#db-table").update(table);
    }).catch(err => {
      console.log('Error fetching status', err);
    });
  }
}

Component.register(DbDatabasesCard);

class DbConnectionsCard extends MdCard {
  onconnected() {
    this.refresh();
  }

  onupdate() {
    if (this.state) this.refresh();
  }

  refresh() {
    fetch("/sockz")
    .then(response => response.json())
    .then((data) => {
      let table = [];
      for (const conn of data["connections"]) {
        table.push({
          socket: conn.socket,
          protocol: conn.protocol,
          address: conn.client_address,
          port: conn.client_port,
          status: conn.status,
          state: conn.state,
          idle: conn.idle + " s",
          requests: dec(conn.requests, 0),
          rx_bytes: dec(conn.rx_bytes, 0),
          tx_bytes: dec(conn.tx_bytes, 0),
          agent: conn.agent,
        });
      }
      this.find("#conn-table").update(table);
    }).catch(err => {
      console.log('Error fetching status', err);
    });
  }
}

Component.register(DbConnectionsCard);

class DbStatisticsCard extends MdCard {
  onconnected() {
    this.refresh();
  }

  onupdate() {
    if (this.state) this.refresh();
  }

  refresh() {
    fetch("/statusz")
    .then(response => response.json())
    .then((data) => {
      let table = [];
      for (const db of data["databases"]) {
        table.push({
          name: db.name,
          get: dec(db.GET, 0),
          put: dec(db.PUT, 0),
          del: dec(db.DELETE, 0),
          next: dec(db.NEXT, 0),
          read: dec(db.READ, 0),
          write: dec(db.WRITE, 0),
          hit: dec(db.HIT, 0),
          miss: dec(db.MISS, 0),
        });
      }
      this.find("#stat-table").update(table);
    }).catch(err => {
      console.log('Error fetching status', err);
    });
  }
}

Component.register(DbStatisticsCard);

document.body.style = null;


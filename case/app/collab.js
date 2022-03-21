// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {inform} from "/common/lib/material.js";
import {Encoder, Decoder} from "/common/lib/frame.js";
import {store} from "./global.js";

const n_topics = store.lookup("topics");

const COLLAB_CREATE  = 1;
const COLLAB_DELETE  = 2;
const COLLAB_INVITE  = 3;
const COLLAB_JOIN    = 4;
const COLLAB_LOGIN   = 5;
const COLLAB_NEWID   = 6;
const COLLAB_UPDATE  = 7;
const COLLAB_ERROR   = 127;

export class Collaboration {
  // Connect to collaboration server.
  async connect(url) {
    this.socket = new WebSocket(url);
    this.socket.addEventListener("message", e => this.onrecv(e));
    return new Promise((resolve, reject) => {
      this.socket.addEventListener("open", e => resolve(this));
      this.socket.addEventListener("error", e => reject(this));
    });
  }

  // Close connection to collaboration server.
  close() {
    this.socket.close();
  }

  // Message received from server.
  async onrecv(e) {
    // Decode message from server.
    let decoder = new Decoder(store, await e.data.arrayBuffer(), false);
    let op = decoder.readVarint32();
    switch (op) {
      case COLLAB_CREATE:
        let credentials = decoder.readVarString();
        this.oncreated && this.oncreated(credentials);
        break;

      case COLLAB_LOGIN:
        let casefile = decoder.readAll();
        this.onlogin && this.onlogin(casefile);
        break;

      case COLLAB_ERROR:
        let message = decoder.readVarString();
        inform(`Collaboration error: ${message}`);
        break;

      default:
        console.log("unexpected collab message op", op);
    }
  }

  // Send message to server.
  send(msg) {
    this.socket.send(msg);
  }

  // Create collaboration for case and return user credentials.
  async create(casefile) {
    // Send collaboration create request to server.
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_CREATE);
    for (let topic of casefile.get(n_topics)) {
      encoder.encode(topic);
    }
    encoder.encode(casefile);
    let packet = encoder.output();
    this.send(packet);
  }

  // Log in to collaboration to send and receive updates.
  async login(caseid, userid, credentials) {
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_LOGIN);
    encoder.writeVarInt(caseid);
    encoder.writeVarString(userid);
    encoder.writeVarString(credentials);
    let packet = encoder.output();
    this.send(packet);
  }
};


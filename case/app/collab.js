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

const CCU_TOPIC   = 1;
const CCU_FOLDER  = 2;
const CCU_FOLDERS = 3;
const CCU_DELETE  = 4;
const CCU_RENAME  = 5;

export class Collaboration {
  // Connect to collaboration server.
  async connect(url) {
    this.url = url;
    this.socket = new WebSocket(url);
    this.socket.addEventListener("message", e => this.onrecv(e));
    return new Promise((resolve, reject) => {
      this.socket.addEventListener("open", e => {
        resolve(this);
      });
      this.socket.addEventListener("error", e => {
        reject("Error connecting to collaboration server " + url);
      });
    });
  }

  // Close connection to collaboration server.
  close() {
    this.connected = false;
    this.socket.close();
  }

  // Check if connected to collaboration server.
  connected() {
    return this.socket.readyState == 1;
  }

  // Message received from server.
  async onrecv(e) {
    // Decode message from server.
    let decoder = new Decoder(store, await e.data.arrayBuffer(), false);
    let op = decoder.readVarint32();
    switch (op) {
      case COLLAB_UPDATE: {
        if (this.listener) {
          let type = decoder.readVarint32();
          switch (type) {
            case CCU_TOPIC: {
              let topic = decoder.readAll();
              this.listener.remote_topic_update(topic);
              break;
            }
            case CCU_FOLDER: {
              let folder = decoder.readVarString();
              let topics = decoder.readAll();
              this.listener.remote_folder_update(folder, topics);
              break;
            }
            case CCU_FOLDERS: {
              let folders = decoder.readAll();
              this.listener.remote_folders_update(folders);
              break;
            }
            case CCU_DELETE: {
              let topicid = decoder.readVarString();
              this.listener.remote_topic_delete(topicid);
              break;
            }
            case CCU_RENAME: {
              let oldname = decoder.readVarString();
              let newname = decoder.readVarString();
              this.listener.remote_folder_rename(oldname, newname);
              break;
            }
            default: {
              console.log("unexpected collab update type", type);
            }
          }
        }
        break;
      }
      case COLLAB_CREATE: {
        let credentials = decoder.readVarString();
        this.oncreated && this.oncreated(credentials);
        break;
      }
      case COLLAB_LOGIN: {
        let casefile = decoder.readAll();
        this.onlogin && this.onlogin(casefile);
        break;
      }
      case COLLAB_INVITE: {
        let key = decoder.readVarString();
        this.oninvite && this.oninvite(key);
        break;
      }
      case COLLAB_JOIN: {
        let credentials = decoder.readVarString();
        this.onjoin && this.onjoin(credentials);
        break;
      }
      case COLLAB_NEWID: {
        let next = decoder.readVarint32();
        this.onnewid && this.onnewid(next);
        break;
      }
      case COLLAB_ERROR: {
        let message = decoder.readVarString();
        inform(`Collaboration error: ${message}`);
        break;
      }
      default: {
        console.log("unexpected collab message op", op);
      }
    }
  }

  // Send message to server.
  send(msg) {
    this.socket.send(msg);
  }

  // Create collaboration for case and return user credentials.
  async create(casefile) {
    return new Promise((resolve, reject) => {
      this.oncreated = credentials => resolve(credentials);

      // Send collaboration create request to server.
      let encoder = new Encoder(store, false);
      encoder.writeVarInt(COLLAB_CREATE);
      for (let topic of casefile.get(n_topics)) {
        encoder.encode(topic);
      }
      encoder.encode(casefile);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Log in to collaboration to send and receive updates.
  async login(caseid, userid, credentials) {
    return new Promise((resolve, reject) => {
      this.onlogin = casefile => resolve(casefile);

      let encoder = new Encoder(store, false);
      encoder.writeVarInt(COLLAB_LOGIN);
      encoder.writeVarInt(caseid);
      encoder.writeVarString(userid);
      encoder.writeVarString(credentials);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Invite participant to collaborate.
  async invite(userid) {
    return new Promise((resolve, reject) => {
      this.oninvite = key => resolve(key);

      let encoder = new Encoder(store, false);
      encoder.writeVarInt(COLLAB_INVITE);
      encoder.writeVarString(userid);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Join collaboration.
  async join(caseid, userid, key) {
    return new Promise((resolve, reject) => {
      this.onjoin = key => resolve(key);

      let encoder = new Encoder(store, false);
      encoder.writeVarInt(COLLAB_JOIN);
      encoder.writeVarInt(caseid);
      encoder.writeVarString(userid);
      encoder.writeVarString(key);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Get new topic id.
  async nextid() {
    return new Promise((resolve, reject) => {
      this.onnewid = next => resolve(next);

      let encoder = new Encoder(store, false);
      encoder.writeVarInt(COLLAB_NEWID);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Send topic update.
  topic_updated(topic) {
    console.log("send topic update", topic.id, topic);
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_UPDATE);
    encoder.writeVarInt(CCU_TOPIC);
    encoder.encodeLink(topic);
    encoder.encode(topic);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send topic deletion.
  topic_deleted(topic) {
    console.log("send topic delete", topic.id);
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_UPDATE);
    encoder.writeVarInt(CCU_DELETE);
    encoder.writeVarString(topic.id);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send folder update.
  folder_updated(name, content) {
    console.log("send folder update", name, content);
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_UPDATE);
    encoder.writeVarInt(CCU_FOLDER);
    encoder.writeVarString(name);
    encoder.encode(content);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send folder rename.
  folder_renamed(oldname, newname) {
    console.log("send folder rename", oldname, newname);
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_UPDATE);
    encoder.writeVarInt(CCU_RENAME);
    encoder.writeVarString(oldname);
    encoder.writeVarString(newname);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send folder list update.
  folders_updated(folders) {
    let folder_names = new Array();
    for (let [name, content] of folders) {
      folder_names.push(name);
    }
    console.log("send folder list update", folder_names);
    let encoder = new Encoder(store, false);
    encoder.writeVarInt(COLLAB_UPDATE);
    encoder.writeVarInt(CCU_FOLDERS);
    encoder.encode(folder_names);
    let packet = encoder.output();
    this.send(packet);
  }
};


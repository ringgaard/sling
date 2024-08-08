// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {inform} from "/common/lib/material.js";
import {Encoder, Decoder} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";

const n_topics = frame("topics");

const COLLAB_CREATE  = 1;
const COLLAB_DELETE  = 2;
const COLLAB_INVITE  = 3;
const COLLAB_JOIN    = 4;
const COLLAB_LOGIN   = 5;
const COLLAB_NEWID   = 6;
const COLLAB_UPDATE  = 7;
const COLLAB_FLUSH   = 8;
const COLLAB_IMPORT  = 9;
const COLLAB_ERROR   = 127;

const CCU_TOPIC   = 1;
const CCU_FOLDER  = 2;
const CCU_FOLDERS = 3;
const CCU_DELETE  = 4;
const CCU_RENAME  = 5;
const CCU_SAVE    = 6;

export class Collaboration {
  // Connect to collaboration server.
  async connect(url) {
    this.socket = new WebSocket(url);
    this.socket.addEventListener("message", e => this.onrecv(e));
    this.socket.addEventListener("error", e => this.onerror(e));
    this.socket.addEventListener("close", e => this.onclose(e));
    return new Promise((resolve, reject) => {
      this.socket.addEventListener("open", e => {
        resolve(this);
      });
      this.socket.addEventListener("error", e => {
        reject("Error connecting to collaboration server " + url);
      });
    });
  }

  onerror(e) {
    if (this.socket) {
      if (this.listener) this.listener.remote_error("Collab websocket error");
    }
  }

  onclose(e) {
    if (this.socket) {
      console.log("Collab closed", e);
      if (this.listener) this.listener.remote_closed(this);
    }
  }

  // Close connection to collaboration server.
  close() {
    this.socket.close();
    this.socket = undefined;
  }

  // Check if connected to collaboration server.
  connected() {
    return this.socket.readyState == 1;
  }

  // Return url for websocket.
  get url() {
    return this.socket.url;
  }

  // Message received from server.
  async onrecv(e) {
    // Decode message from server.
    let decoder = new Decoder(store, await e.data.arrayBuffer(), false);
    let op = decoder.read_varint32();
    switch (op) {
      case COLLAB_UPDATE: {
        if (this.listener) {
          let type = decoder.read_varint32();
          switch (type) {
            case CCU_TOPIC: {
              let topic = decoder.readall();
              this.listener.remote_topic_update(topic);
              break;
            }
            case CCU_FOLDER: {
              let folder = decoder.read_varstring();
              let topics = decoder.readall();
              this.listener.remote_folder_update(folder, topics);
              break;
            }
            case CCU_FOLDERS: {
              let folders = decoder.readall();
              this.listener.remote_folders_update(folders);
              break;
            }
            case CCU_DELETE: {
              let topicid = decoder.read_varstring();
              this.listener.remote_topic_delete(topicid);
              break;
            }
            case CCU_RENAME: {
              let oldname = decoder.read_varstring();
              let newname = decoder.read_varstring();
              this.listener.remote_folder_rename(oldname, newname);
              break;
            }
            case CCU_SAVE: {
              let modtime = decoder.read_varstring();
              this.listener.remote_save(modtime);
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
        let credentials = decoder.read_varstring();
        this.onfail = null;
        if (this.oncreated) {
          this.oncreated(credentials);
          this.oncreated = null;
        }
        break;
      }
      case COLLAB_LOGIN: {
        let casefile = decoder.readall();
        this.onfail = null;
        if (this.onlogin) {
          this.onlogin(casefile);
          this.onlogin = null;
        }
        break;
      }
      case COLLAB_INVITE: {
        let key = decoder.read_varstring();
        this.onfail = null;
        if (this.oninvite) {
          this.oninvite(key);
          this.oninvite = null;
        }
        break;
      }
      case COLLAB_JOIN: {
        let credentials = decoder.read_varstring();
        this.onfail = null;
        if (this.onjoin) {
          this.onjoin(credentials);
          this.onjoin = null;
        }
        break;
      }
      case COLLAB_NEWID: {
        let next = decoder.read_varint32();
        this.onfail = null;
        if (this.onnewid) {
          this.onnewid(next);
          this.onnewid = null;
        }
        break;
      }
      case COLLAB_FLUSH: {
        let modtime = decoder.read_varstring();
        this.onfail = null;
        if (this.onflush) {
          this.onflush(modtime);
          this.onflush = null;
        }
        break;
      }
      case COLLAB_IMPORT: {
        let num_topics = decoder.read_varint32();
        this.onfail = null;
        if (this.onimport) {
          this.onimport(num_topics);
          this.onimport = null;
        }
        break;
      }
      case COLLAB_ERROR: {
        let message = decoder.read_varstring();
        if (this.onfail) {
          this.onfail(message);
        } else {
          inform(`Collaboration error: ${message}`);
        }
        this.onfail = null;
        break;
      }
      default: {
        console.log("unexpected collab message op", op);
      }
    }
  }

  // Send message to server.
  send(msg) {
    if (!this.connected()) {
      this.listener.remote_error("No connection to collaboration server")
    }
    this.socket.send(msg);
  }

  // Create collaboration for case and return user credentials.
  async create(casefile) {
    return new Promise((resolve, reject) => {
      this.oncreated = credentials => resolve(credentials);

      // Send collaboration create request to server.
      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_CREATE);
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
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_LOGIN);
      encoder.write_varint(caseid);
      encoder.write_varstring(userid);
      encoder.write_varstring(credentials);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Invite participant to collaborate.
  async invite(userid) {
    return new Promise((resolve, reject) => {
      this.oninvite = key => resolve(key);
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_INVITE);
      encoder.write_varstring(userid);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Join collaboration.
  async join(caseid, userid, key) {
    return new Promise((resolve, reject) => {
      this.onjoin = key => resolve(key);
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_JOIN);
      encoder.write_varint(caseid);
      encoder.write_varstring(userid);
      encoder.write_varstring(key);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Get new topic id.
  async nextid() {
    return new Promise((resolve, reject) => {
      this.onnewid = next => resolve(next);
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_NEWID);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Flush changes to disk on server.
  async flush() {
    return new Promise((resolve, reject) => {
      this.onflush = modtime => resolve(modtime);
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_FLUSH);
      let packet = encoder.output();
      this.send(packet);
    });
  }

  // Import topics on server.
  async bulkimport(folder, data) {
    return new Promise((resolve, reject) => {
      this.onimport = num_topics => resolve(num_topics);
      this.onfail = e => reject(e);

      let encoder = new Encoder(store, false);
      encoder.write_varint(COLLAB_IMPORT);
      encoder.write_varstring(folder || "");
      encoder.write_blob(data);
      let packet = encoder.output();

      this.send(packet);
    });
  }

  // Send topic update.
  topic_updated(topic) {
    console.log("send topic update", topic.id);
    let encoder = new Encoder(store, false);
    encoder.write_varint(COLLAB_UPDATE);
    encoder.write_varint(CCU_TOPIC);
    encoder.encode_link(topic);
    encoder.encode(topic);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send topic deletion.
  topic_deleted(topic) {
    console.log("send topic delete", topic.id);
    let encoder = new Encoder(store, false);
    encoder.write_varint(COLLAB_UPDATE);
    encoder.write_varint(CCU_DELETE);
    encoder.write_varstring(topic.id);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send folder update.
  folder_updated(name, content) {
    console.log("send folder update", name, content);
    let encoder = new Encoder(store, false);
    encoder.write_varint(COLLAB_UPDATE);
    encoder.write_varint(CCU_FOLDER);
    encoder.write_varstring(name);
    encoder.encode(content);
    let packet = encoder.output();
    this.send(packet);
  }

  // Send folder rename.
  folder_renamed(oldname, newname) {
    console.log("send folder rename", oldname, newname);
    let encoder = new Encoder(store, false);
    encoder.write_varint(COLLAB_UPDATE);
    encoder.write_varint(CCU_RENAME);
    encoder.write_varstring(oldname);
    encoder.write_varstring(newname);
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
    encoder.write_varint(COLLAB_UPDATE);
    encoder.write_varint(CCU_FOLDERS);
    encoder.encode(folder_names);
    let packet = encoder.output();
    this.send(packet);
  }
};


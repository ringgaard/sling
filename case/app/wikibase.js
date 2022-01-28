// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Component} from "/common/lib/component.js";
import {store, settings, save_settings} from "./global.js";
import {Frame} from "/common/lib/frame.js";
import {Time} from "./value.js";

const n_id = store.id;
const n_is = store.is;
const n_name = store.lookup("name");
const n_description = store.lookup("description");
const n_alias = store.lookup("alias");
const n_instance_of = store.lookup("P31");
const n_case_file = store.lookup("Q108673968");
const n_english = store.lookup("/lang/en");
const n_target = store.lookup("target");
const n_lat = store.lookup("/w/lat");
const n_lng = store.lookup("/w/lng");
const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");

const n_item_type = store.lookup("/w/item");
const n_string_type = store.lookup("/w/string");
const n_text_type = store.lookup("/w/text");
const n_xref_type = store.lookup("/w/xref");
const n_time_type = store.lookup("/w/time");
const n_url_type = store.lookup("/w/url");
const n_media_type = store.lookup("/w/media");
const n_quantity_type = store.lookup("/w/quantity");
const n_geo_type = store.lookup("/w/geo");

async function wikidata_initiate() {
  // Initiate authorization and get redirect url.
  let callback = window.location.href;
  let r = await fetch("/case/wikibase/authorize?cb=" +
                      encodeURIComponent(callback));
  let response = await r.json()

  // Rememeber auth id.
  settings.wikidata_authid = response.authid;
  settings.wikidata_consumer = response.consumer;
  save_settings();

  // Redirect to let user authorize SLING.
  window.location.href = response.redirect;
}

export async function oauth_callback() {
  // Get auth id.
  let authid = settings.wikidata_authid;
  settings.wikidata_authid = null;

  // Get client access token.
  let params = window.location.search + "&authid=" + authid;
  let r = await fetch("/case/wikibase/access" + params);
  let response = await r.json()
  if (!response.key) {
    console.log("Error accessing wikidata client token", response);
    return false;
  }

  // Store client token and secret.
  settings.wikidata_key = response.key;
  settings.wikidata_secret = response.secret;
  save_settings();

  return true;
}

async function identify() {
  let r = await fetch("/case/wikibase/identify", {
    headers: {
      "Client-Key": settings.wikidata_key,
      "Client-Secret": settings.wikidata_secret,
    }
  });
  return await r.json();
}

async function get_csrf_token() {
  let r = await fetch("/case/wikibase/token", {
    headers: {
      "Client-Key": settings.wikidata_key,
      "Client-Secret": settings.wikidata_secret,
    },
  });
  let reply = await r.json();
  return reply.query.tokens.csrftoken;
}

async function edit_entity(entity, token) {
  let r = await fetch("/case/wikibase/edit", {
    method: "POST",
    headers: {
      "Client-Key": settings.wikidata_key,
      "Client-Secret": settings.wikidata_secret,
      "CSFR-Token": token,
    },
    body: JSON.stringify(entity)
  });
  let reply = await r.json();
  return reply;
}

function is_qid(id) {
  return /Q\d+/.test(id);
}

function is_pid(id) {
  return /P\d+/.test(id);
}

function get_qid(topic) {
  for (let [name, value] of topic) {
    if (name == n_id) {
      if (is_qid(value)) return value;
    } else if (name == n_is) {
      let id = value.id;
      if (is_qid(id)) return id;
    }
  }
}

function get_language(s) {
  if (s.qual) {
    let id = s.qual.id;
    if (id.startsWith("/lang/")) return id.slice(6);
  }
  return "en";
}

function itemtext(item) {
  let name = item.get(n_name);
  if (name) return item.id + " " + name;
  return item.id;
}

class WikibaseExporter {
  constructor(topics) {
    this.topics = topics;
    this.entities = new Map();
    this.deferred = new Map();

    for (let topic of this.topics) {
      // Never export main case file topic.
      if (topic.get(n_instance_of) == n_case_file) continue;

      // Convert topic to Wikibase JSON format.
      this.convert_topic(topic)
    }

    for (const [topic, value] of this.deferred) {
      console.log("deferred", topic.id, JSON.stringify(value));
    }
    for (const [topic, entity] of this.entities) {
      console.log(topic.id, JSON.stringify(entity, null, "  "));
    }
  }

  async publish() {
    // Get csrf token.
    let token = await get_csrf_token();

    // Publish topic updates.
    for (const [topic, entity] of this.entities) {
      console.log("publish", topic.id);
      let response = await edit_entity(entity, token);
      if (!entity.id) {
        entity.id = response.entity.id;
        topic.put(n_is, store.lookup(entity.id));
      }
    }
  }

  convert_topic(topic) {
    // Add entity with optional existing Wikidata item id.
    let entity = {type: "item"};
    let qid = get_qid(topic);
    if (qid) entity.id = qid;
    this.entities.set(topic, entity);

    // Add labels, description, aliases, and claims.
    for (let [name, value] of topic) {
      switch (name) {
        case n_id:
        case n_is:
          // Ignore.
          break;

        case n_name: {
          let label = value.toString();
          let lang = get_language(value);
          if (!entity.labels) entity.labels = {};
          entity.labels[lang] = {language: lang, value: label};
          break;
        }

        case n_description: {
          let description = value.toString();
          let lang = get_language(value);
          if (!entity.descriptions) entity.descriptions = {};
          entity.descriptions[lang] = {language: lang, value: description};
          break;
        }

        case n_alias: {
          let alias = value.toString();
          let lang = get_language(value);
          if (!entity.aliases) entity.aliases = {};
          if (!entity.aliases[lang]) entity.aliases[lang] = [];
          entity.aliases[lang].push({language: lang, value: alias});
          break;
        }

        default: {
          let pid = name.id;
          if (!is_pid(pid)) continue;

          // Build main snak.
          let t = name.get(n_target);
          let v = store.resolve(value);
          if (this.skip_value(v)) {
            console.log("skip", itemtext(name), itemtext(v),
                        "for", itemtext(topic));
            continue;
          }

          let [datatype, datavalue] = this.convert_value(t, v);
          let snak = {
            "snaktype": "value",
            "property": pid,
            "datatype": datatype,
            "datavalue": datavalue,
          }

          // Build claim.
          let claim = {
            "type": "statement",
            "rank": "normal",
            "mainsnak": snak,
          };

          if (v != value) {
            // Add qualifiers.
            claim.qualifiers = {};
            for (let [qname, qvalue] of value) {
              if (qname == n_is) continue;
              let pid = qname.id;
              if (!is_pid(qname)) continue;

              // Build snak for qualifier.
              let t = qname.get(n_target);
              let v = store.resolve(qvalue);
              if (this.skip_value(v)) {
                console.log("skip qualifier", itemtext(qname), itemtext(v),
                            "for", itemtext(topic));
                continue;
              }
              let [datatype, datavalue] = this.convert_value(t, v);
              let snak = {
                "snaktype": "value",
                "property": pid,
                "datatype": datatype,
                "datavalue": datavalue,
              };

              // Add qualifier to claim.
              if (!claim.qualifiers[pid]) claim.qualifiers[pid] = [];
              claim.qualifiers[pid].push(snak);
            }
          }

          // Add claim to entity.
          if (!entity.claims) entity.claims = {};
          if (!entity.claims[pid]) entity.claims[pid] = [];
          entity.claims[pid].push(claim);
        }
      }
    }
  }

  convert_value(type, value) {
    if (!type) type = n_item_type;
    var datatype, datavalue;
    switch (type) {
      case n_item_type: {
        let qid = get_qid(value);
        datatype = "wikibase-item";
        if (qid) {
          datavalue = {
            "value": {
              "entity-type": "item",
              "id": qid
            },
            "type": "wikibase-entityid"
          };
        } else {
          datavalue = this.deferred.get(value);
          if (!datavalue) {
            datavalue = {
              "value": {
                "entity-type": "item",
                "id": value.id
              },
              "type": "wikibase-entityid"
            };
            this.deferred.set(value, datavalue);
          }
        }
        break;
      }

      case n_string_type: {
        datatype = "string";
        datavalue = {
          "value": value,
          "type": "string"
        };
        break;
      }

      case n_text_type: {
        datatype = "monolingualtext";
        datavalue = {
          "language": get_language(value),
          "value": value.toString(),
          "type": "string"
        };
        break;
      }

      case n_xref_type: {
        datatype = "external-id";
        datavalue = {
          "value": value,
          "type": "string"
        };
        break;
      }

      case n_url_type: {
        datatype = "url";
        datavalue = {
          "value": value,
          "type": "string"
        };
        break;
      }

      case n_time_type: {
        let t = new Time(value);
        let [time, precision] = t.wikidate();
        datatype = "time";
        datavalue = {
          "value": {
            "time": time,
            "precision": precision
          },
          "type": "time"
        };
        break;
      }

      case n_media_type: {
        datatype = "commonsMedia";
        datavalue = {
          "value": value,
          "type": "string"
        };
        break;
      }

      case n_quantity_type: {
        datatype = "quantity";
        datavalue = {
          "value": {
            "amount": value.get(n_amount).toString(),
            "unit": "http://www.wikidata.org/entity/" + value.get(n_unit).id,
          },
          "type": "quantity"
        };
        break;
      }

      case n_geo_type: {
        datatype = "globecoordinate";
        datavalue = {
          "value": {
            "latitude": value.get(n_lat),
            "longitude": value.get(n_lng),
          },
          "type": "globecoordinate"
        };
        break;
      }
    }

    return [datatype, datavalue];
  }

  skip_value(value) {
    if (value instanceof Frame) {
      let id = value.id;
      if (!id) return false;
      if (is_qid(id)) return false;
      if (this.topics.includes(value)) return false;
      return true;
    } else {
      return false;
    }
  }
}


export async function wikidata_export(casefile, topics) {
  // Initiate OAuth authorization if we don't have an access token.
  if (!settings.wikidata_key) {
    wikidata_initiate();
    return;
  }

  if (false) {
    console.log("wikidata client token", settings.wikidata_key);
    let identity = await identify();
    console.log("identify", identity);
  } else {
    let exporter = new WikibaseExporter(topics);
    await exporter.publish();
  }
}


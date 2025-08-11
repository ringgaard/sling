// Copyright 2024 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING importer plug-in for importing genealogical data from GEDCOM files.

import {store, frame} from "/common/lib/global.js";
import {parsers} from "/common/lib/datatype.js";

import {get_property_index} from "/case/app/schema.js";

const n_id = store.id;
const n_is = store.is;

const n_type = frame("P31");
const n_human = frame("Q5");
const n_gcid = frame("PGCID");
const n_name = frame("name");
const n_alias = frame("alias");
const n_birth_name = frame("P1477");
const n_married_name = frame("P2562");
const n_denmark = frame("Q756617");
const n_country = frame("P27");
const n_email = frame("P968");

const n_start = frame("P580");
const n_pom = frame("P2842");

const n_dob = frame("P569");
const n_pob = frame("P19");

const n_dod = frame("P570");
const n_pod = frame("P20");

const n_gender = frame("P21");
const n_female = frame("Q6581072");
const n_male = frame("Q6581097");

const n_spouse = frame("P26");
const n_partner = frame("P451");
const n_father = frame("P22");
const n_mother = frame("P25");
const n_child = frame("P40");

const country_from_language = {
  "Danish": n_denmark,
}

const months = {
  "JAN": 1, "FEB": 2, "MAR": 3, "APR": 4, "MAY": 5, "JUN": 6,
  "JUL": 7, "AUG": 8, "SEP": 9, "OCT": 10, "NOV": 11, "DEC": 12
};

function date(s) {
  try {
    let f = s.split(" ");
    if (f.length == 3) {
      let day = parseInt(f[0]);
      let mon = months[f[1]];
      let year = parseInt(f[2]);
      return year * 10000 + mon * 100 + day;
    } else if (f.length == 2) {
      let mon = months[f[0]];
      let year = parseInt(f[1]);
      return year * 100 + mon;
    } else if (f.length == 1) {
      let year = parseInt(f[0]);
      return year;
    }
  } catch {
  }
}

function lookup(node, field) {
  if (node.children) {
    for (let n of node.children) {
      if (n.field == field) return n.value;
    }
  }
}

export default class GEDCOMImporter {
  async process(file, context) {
    // Parse GEDCOM file into tree with nodes.
    let data = await file.text();
    let top = new Array();
    let stack = new Array();
    for (let line of data.split("\n")) {
      if (line.length == 0) continue;
      if (line[0] < '0' || line[0] > '9') continue;
      let cols = line.trim().split(" ");
      if (cols < 2) continue;
      let level = parseInt(cols[0]);
      let field = cols[1].trim();
      let value = cols.slice(2).join(" ").trim();
      let node = {level, field, value, children: null};

      while (level < stack.length) stack.pop();
      if (level == 0) {
        top.push(node);
      } else {
        let parent = stack[stack.length - 1];
        if (!parent.children) parent.children = new Array();
        parent.children.push(node);
      }
      stack.push(node);
    }

    // Find existing topics with GEDCOM ids.
    let topics = new Map();
    for (let t of context.editor.topics) {
      let uid = t.get(n_gcid);
      if (uid) topics.set(uid, t);
    }

    // Import individuals and families.
    let num_persons = 0;
    let num_families = 0;
    let pmap = new Map();
    let country;
    for (let node of top) {
      //console.log(node.field, node.value);
      if (node.value == "INDI") {
        let uid, gender;
        let name, married, given, surname;
        let married_name, birth_name;
        let dob, pob, dod, pod;
        if (node.children) {
          for (let c of node.children) {
            if (c.field == "NAME") {
              name = c.value.replace(/\//g, "").trim();
              if (c.children) {
                for (let n of c.children) {
                  if (n.field == "GIVN") {
                    given = n.value;
                  } else if (n.field == "SURN") {
                    surname = n.value;
                  } else if (n.field == "_MARNM") {
                    married = n.value;
                  }
                }
              }
            } else if (c.field == "SEX") {
              if (c.value == "M") {
                gender = n_male;
              } else if (c.value == "F") {
                gender = n_female;
              }
            } else if (c.field == "BIRT") {
              if (c.children) {
                for (let n of c.children) {
                  if (n.field == "DATE") {
                    dob = date(n.value) || n.value;
                  } else if (n.field == "PLAC") {
                    pob = n.value;
                  }
                }
              }
            } else if (c.field == "DEAT") {
              if (c.children) {
                for (let n of c .children) {
                  if (n.field == "DATE") {
                    dod = date(n.value) || n.value;
                  } else if (n.field == "PLAC") {
                    pod = n.value;
                  }
                }
              }
            } else if (c.field == "_UID") {
              uid = c.value;
            }
          }

          if (given && surname) birth_name = given + " " + surname;
          if (given && married) married_name = given + " " + married;
        }

        // Try to find existing topic with matching uid.
        let topic = uid && topics.get(uid);

        // Create new topic for new individuals.
        if (!topic) topic = await context.new_topic();

        // Name.
        let topic_name = topic.get(n_name);
        if (!topic_name) {
          topic.put(n_name, name);
          topic_name = name;
        } else if (name != birth_name && name != married_name) {
          if (name != topic_name) topic.put(n_alias, name);
        }
        if (birth_name != topic_name) topic.put(n_birth_name, birth_name);
        if (married_name != topic_name) topic.put(n_married_name, married_name);

        // Type and gender.
        topic.put(n_type, n_human);
        topic.put(n_gender, gender);

        // Birth.
        if (dob) {
          let birth = topic.get(n_dob);
          if (store.isqualifier(birth)) {
            birth.put(n_is, dob);
          } else {
            topic.put(n_dob, dob);
          }
        }
        if (!topic.has(n_pob)) topic.put(n_pob, pob);

        // Death.
        if (dod) {
          let death = topic.get(n_dod);
          if (store.isqualifier(death)) {
            death.put(n_is, dod);
          } else {
            topic.put(n_dod, dod);
          }
        }
        if (!topic.has(n_pod)) topic.put(n_pod, pod);

        // Country.
        if (!topic.has(n_country)) {
          topic.put(n_country, country);
        }

        topic.put(n_gcid, uid);

        pmap.set(node.field, topic);
        num_persons++;
      } else if (node.value == "FAM" && node.children) {
        let husband, wife;
        let children = new Array();
        let dom, pom;
        let relationship;
        for (let c of node.children) {
          if (c.field == "HUSB") {
            husband = pmap.get(c.value);
          } else if (c.field == "WIFE") {
            wife = pmap.get(c.value);
          } else if (c.field == "CHIL") {
            children.push(pmap.get(c.value));
          } else if (c.field == "MARR") {
            dom = date(lookup(c, "DATE"))
            pom = lookup(c, "PLAC");
          } else if (c.field == "EVEN") {
            relationship = lookup(c, "TYPE");
            dom = date(lookup(c, "DATE"))
          }
        }

        for (let child of children) {
          if (husband) child.put(n_father, husband);
          if (wife) child.put(n_mother, wife);
        }
        if (husband && wife) {
          if (dom || pom) {
            if (!husband.has(n_spouse, wife) && !husband.has(n_partner, wife)) {
              let bride = store.frame();
              bride.add(n_is, wife);
              if (dom) bride.add(n_start, dom);
              if (pom) bride.add(n_pom, pom);
              if (relationship == "MYHERITAGE:REL_PARTNERS") {
                husband.put(n_partner, bride);
              } else {
                husband.put(n_spouse, bride);
              }
            }

            if (!wife.has(n_spouse, husband) && !wife.has(n_partner, husband)) {
              let groom = store.frame();
              groom.add(n_is, husband);
              if (dom) groom.add(n_start, dom);
              if (pom) groom.add(n_pom, pom);
              if (relationship == "MYHERITAGE:REL_PARTNERS") {
                wife.put(n_partner, groom);
              } else {
                wife.put(n_spouse, groom);
              }
            }
          } else if (relationship == "MYHERITAGE:REL_PARTNERS") {
            husband.put(n_partner, wife);
            wife.put(n_partner, husband);
          } else {
            if (!husband.has(n_partner, wife)) husband.put(n_spouse, wife);
            if (!wife.has(n_partner, husband)) wife.put(n_spouse, husband);
          }
        }
        for (let child of children) {
          if (husband) husband.put(n_child, child);
          if (wife) wife.put(n_child, child);
        }

        num_families++;
      } else if (node.field == "HEAD") {
        let lang = lookup(node, "LANG");
        country = country_from_language[lang];
      }
    }
    console.log(`${num_persons} persons and ${num_families} families`);
  }
};

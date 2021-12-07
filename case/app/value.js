// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Frame, QString} from "/common/lib/frame.js";
import {store, settings} from "./global.js";

const n_isa = store.isa;
const n_name = store.lookup("name");
const n_amount = store.lookup("/w/amount");
const n_unit = store.lookup("/w/unit");
const n_cm = store.lookup("Q174728");
const n_kg = store.lookup("Q11570");
const n_geo = store.lookup("/w/geo");
const n_lat = store.lookup("/w/lat");
const n_lng = store.lookup("/w/lng");

function lookup(mapping) {
  let m = new Map();
  for (let [key, value] of Object.entries(mapping)) {
    m[key] = store.lookup(value);
  }
  return m;
}

const units = lookup({
  "m": "Q11573",
  "mm": "Q174789",
  "mm3": "Q3675550",
  "cm": "Q174728",
  "km": "Q828224",
  "km2": "Q712226",
  "g": "Q41803",
  "l": "Q11582",
  "kg": "Q11570",
  "mi": "Q253276",
  "in": "Q218593",
  "ft": "Q3710",
  "USD": "Q4917",
  "EUR": "Q4916",
  "DKK": "Q25417",
});

// Convert geo coordinate from decimal to degrees, minutes and seconds.
export function decimal2degree(coord, latitude) {
  // Compute direction.
  var direction;
  if (coord < 0) {
    coord = -coord;
    direction = latitude ? "S" : "W";
  } else {
    direction = latitude ? "N" : "E";
  }

  // Compute degrees.
  let degrees = Math.floor(coord);

  // Compute minutes.
  let minutes = Math.floor(coord * 60) % 60;

  // Compute seconds.
  let seconds = Math.floor(coord * 3600) % 60;

  // Build coordinate string.
  return degrees +  "°" + minutes + "′" + seconds + "″" + direction;
}

export function latlong(lat, lng) {
  return decimal2degree(lat, true) + ", " + decimal2degree(lng, false);
}

// Granularity for time.
const MILLENNIUM = 1;
const CENTURY = 2;
const DECADE = 3
const YEAR = 4;
const MONTH = 5
const DAY = 6;

const month_names = [
  "January", "February", "March",
  "April", "May", "June",
  "July", "August", "September",
  "October", "November", "December",
];

const month_mapping = {
  "jan": 1, "feb": 2, "mar": 3, "apr": 4, "may": 5, "jun": 6,
  "jul": 7, "aug": 8, "sep": 9, "oct": 10, "nov": 11, "dec": 12,

  "maj": 5, "okt": 10,
};

function monthnum(name) {
  return month_mapping[name.substring(0, 3).toLowerCase()];
}

function pad(num, size) {
  var s = "0000" + num;
  return s.substr(s.length - size);
}

function spad(num, size) {
  return (num < 0 ? "-" : "+") + pad(num, size);
}

export class Time {
  constructor(t) {
    if (!t) return;
    if (typeof(t) === "number") {
      if (t >= 1000000) {
        // YYYYMMDD
        this.year = Math.floor(t / 10000);
        this.month = Math.floor((t % 10000) / 100);
        this.day = Math.floor(t % 100);
        this.precision = DAY;
      } else if (t >= 10000) {
        // YYYYMM
        this.year = Math.floor(t / 100);
        this.month = Math.floor(t % 100);
        this.precision = MONTH;
      } else if (t >= 1000) {
        // YYYY
        this.year = Math.floor(t);
        this.precision = YEAR;
      } else if (t >= 100) {
        // YYY*
        this.year = Math.floor(t * 10);
        this.precision = DECADE;
      } else if (t >= 10) {
        // YY**
        this.year = Math.floor(t * 100 + 1);
        this.precision = CENTURY;
      } else if (t >= 0) {
        // Y***
        this.year = Math.floor(t * 1000 + 1);
        this.precision = MILLENNIUM;
      }
    } else if (typeof(t) === "string") {
      let m = t.match(/^(\+|\-)(\d{4})(\d{2})?(\d{2})?$/);
      if (m) {
        this.year = parseInt(m[2]) * (m[1] == "-" ? -1 : 1);
        this.precision = YEAR;
        if (m[3]) {
          this.month = parseInt(m[3]);
          this.precision = MONTH;
        }
        if (m[4]) {
          this.day = parseInt(m[4]);
          this.precision = DAY;
        }
      }
    } else if (t instanceof Date) {
      this.year = t.getFullYear();
      this.month = t.getMonth() + 1;
      this.day = t.getDate();
      this.precision = DAY;
    }

    if (!this.precision) {
      let d = new Date(t);
      if (isFinite(d)) {
        this.year = d.getFullYear();
        this.month = d.getMonth() + 1;
        this.day = d.getDate();
        this.precision = DAY;
      }
    }
  }

  text() {
    switch (this.precision) {
      case MILLENNIUM:
        if (this.year > 0) {
          let millennium = Math.floor((this.year - 1) / 1000 + 1);
          return millennium + ". millennium AD";
        } else {
          let millennium = Math.floor(-((this.year + 1) / 100 - 1));
          return millennium + ". millennium BC";
        }

      case CENTURY:
        if (this.year > 0) {
          let century = Math.floor((this.year - 1) / 100 + 1);
          return century + ". century AD";
        } else {
          let century = Math.floor(-((this.year + 1) / 100 - 1));
          return century + ". century BC";
        }

      case DECADE:
        return this.year + "s";

      case YEAR:
        return this.year.toString();

      case MONTH:
        return month_names[this.month - 1] + " " + this.year;

      case DAY:
        return month_names[this.month - 1] + " " + this.day + ", " + this.year;

      default:
        return "???";
    }
  }

  value() {
    let y = this.year;
    let m = this.month;
    let d = this.day;
    switch (this.precision) {
      case MILLENNIUM:
        let mille = Math.floor(y + (y > 0 ? -1 : 1) / 1000);
        if (y >= 1000) {
          return mille;
        } else {
          return spad(mille, 1) + "***";
        }

      case CENTURY:
        let cent = Math.floor((y + (y > 0 ? -1 : 1)) / 100);
        if (y >= 1000) {
          return cent;
        } else {
          return spad(cent, 2) + "**";
        }

      case DECADE:
        if (y >= 1000) {
          return y / 10;
        } else {
          return spad(y / 10, 3) + "*";
        }

      case YEAR:
        if (y >= 1000) {
          return y;
        } else {
          return spad(y, 4);
        }

      case MONTH:
        if (y >= 1000) {
          return y * 100 + m;
        } else {
          return `${spad(y, 4)}-${pad(m, 2)}`;
        }

      case DAY:
        if (y >= 1000) {
          return y * 10000 + m * 100 + d;
        } else {
          return `${spad(y, 4)}-${pad(m, 2)}-${pad(d, 2)}`;
        }

      default:
        return "???";
    }
  }

  static value(year, month, day, precision) {
    if (!precision) {
      if (!month) {
        precision = YEAR;
      } else if (!day) {
        precision = MONTH;
      } else {
        precision = DAY;
      }
    }

    year = parseInt(year);
    if (month) {
      month = parseInt(month);
      if (month < 1 || month > 12) return null;
    }
    if (day) {
      day = parseInt(day);
      if (day < 1 || day > 31) return null;
    }

    let t = new Time();
    t.year = year;
    t.month = month;
    t.day = day;
    t.precision = precision;
    return t;
  }

  age(time) {
    let years = time.year - this.year;
    if (time.month == this.month) {
      if (time.day < this.day) {
        years -= 1;
      }
    } else if (time.month < this.month) {
      years -= 1;
    }
    return years;
  }
}

export class LabelCollector {
  constructor(store) {
    this.store = store;
    this.items = new Set();
  }

  add(item) {
    // Add all missing values to collector.
    for (let [name, value] of item) {
      if (value instanceof Frame) {
        if (value.isanonymous()) {
          this.add(value);
        } else if (value.isproxy()) {
          this.items.add(value);
        }
      } else if (value instanceof QString) {
        if (value.qual) this.items.add(value.qual);
      }
    }
  }

  async retrieve() {
    // Skip if all labels has already been resolved.
    if (this.items.size == 0) return null;

    // Retrieve stubs from knowledge service.
    let response = await fetch(settings.kbservice + "/kb/stubs", {
      method: 'POST',
      headers: {
        'Content-Type': 'application/sling',
      },
      body: this.store.encode(Array.from(this.items)),
    });
    let stubs = await this.store.parse(response);

    // Mark as stubs.
    for (let stub of stubs) {
      if (stub) stub.markstub();
    }

    return stubs;
  }
}

function add_date_result(results, year, month, day, precision) {
  let t = Time.value(year, month, day, precision);
  if (t) {
    results.push({
      value: t.value(),
      title: t.text(),
      description: "time",
    });
  }
}

export function value_parser(value, results) {
  var m;

  // Parse YYYY-MM-DD.
  m = value.match(/^(\d{4})-(\d{2})-(\d{2})$/);
  if (m) {
    add_date_result(results, m[1], m[2], m[3]);
  }

  // Parse DD.MM.YYYY.
  m = value.match(/^(\d+)[\-\.](\d+)[\-\.](\d+)$/);
  if (m) {
    add_date_result(results, m[3], m[2], m[1]);
  }

  // Parse MM/DD/YYYY.
  m = value.match(/^(\d+)\/(\d+)\/(\d+)$/);
  if (m) {
    add_date_result(results, m[3], m[1], m[2]);
  }

  // Parse YYYY-MM.
  m = value.match(/^(\d+)-(\d+)$/);
  if (m) {
    add_date_result(results, m[1], m[2], null);
  }

  // Parse YYYY.
  m = value.match(/^(\d{4})$/);
  if (m) {
    add_date_result(results, m[1], null, null);
  }

  // Parse decade as YYY0s.
  m = value.match(/^(\d{3}0)s$/);
  if (m) {
    add_date_result(results, m[1], m[2], null, DECADE);
  }

  // Parse Month DD, YYYY.
  m = value.match(/^(\w+) (\d+)(\.|st|nd|rd|th)?[ ,]+(\d+)$/);
  if (m) {
    let month = monthnum(m[1]);
    if (month) add_date_result(results, m[4], month, m[2]);
  }

  // Parse DD. Month, YYYY.
  m = value.match(/^(\d+)(\.|st|nd|rd|th)? (\w+)[ ,]+(\d+)$/);
  if (m) {
    let month = monthnum(m[3]);
    if (month) add_date_result(results, m[4], month, m[1]);
  }

  // Parse Month YYYY.
  m = value.match(/^(\w+)[ ,]+(\d+)$/);
  if (m) {
    let month = monthnum(m[1]);
    if (month) add_date_result(results, m[2], month, null);
  }

  // Number parsing.
  if (value.match(/^[-+]?[0-9.e]+$/)) {
    let num = parseFloat(value);
    if (!isNaN(num) && (num | 0) === num) {
      results.push({
        value: num,
        title: num.toString(),
        description: "integer value",
      });
    }
    if (!isNaN(num)) {
      results.push({
        value: num,
        title: num.toString(),
        description: "numeric value",
      });
    }
  }

  // Quantity parsing.
  m = value.match(/^(\-?\d+\.?\d+) (\w+)$/);
  if (m) {
    let amount = parseFloat(m[1]);
    let unit = units[m[2]];

    if (!isNaN(amount) && unit) {
      let v = store.frame();
      v.add(n_amount, amount);
      v.add(n_unit, unit);
      results.push({
        value: v,
        title: `${amount} ${unit.get(n_name) || m[2]}`,
        description: "quantity",
      });
    }
  }

  // Parse and convert implerial units.
  m = value.match(/^(\d+)'\s*(\d+)"$/);
  if (m) {
    let feet = parseInt(m[1]);
    let inches = parseInt(m[2]);
    let cm = Math.floor(feet * 30.48 + inches * 2.54)
    let v = store.frame();
    v.add(n_amount, cm);
    v.add(n_unit, n_cm);
    results.push({
      value: v,
      title: `${cm} cm`,
      description: "converted quantity",
    });
  }
  m = value.match(/^(\d+)\s*lbs?$/);
  if (m) {
    let pounds = parseInt(m[1]);
    let kg = Math.floor(pounds * 0.45359237);
    let v = store.frame();
    v.add(n_amount, kg);
    v.add(n_unit, n_kg);
    results.push({
      value: v,
      title: `${kg} kg`,
      description: "converted quantity",
    });
  }

  // Parse localized strings.
  m = value.match(/^(.+)@([a-z][a-z])$/);
  if (m) {
    let lang = store.find(`/lang/${m[2]}`);
    if (lang) {
      let text = m[1].trim();
      let qstr = new QString(text, lang);
      results.push({
        value: qstr,
        title: text,
        description: (lang.get(n_name) || m[2]) + " text",
      });
    }
  }

  // Parse geo-locaion.
  m = value.match(/^(\d+\.\d+),(\d+\.\d+)$/);
  if (m) {
    let lat = parseFloat(m[1]);
    let lng = parseFloat(m[2]);
    if (lat >= -90 && lat <= 90 && lng >= -90 && lng <= 90) {
      let v = store.frame();
      v.add(n_isa, n_geo);
      v.add(n_lat, lat);
      v.add(n_lng, lng);
      results.push({
        value: v,
        title: latlong(lat, lng),
        description: "geo-location",
      });
    }
  }
}


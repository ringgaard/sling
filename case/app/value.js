// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Frame, QString} from "/common/lib/frame.js";
import {settings} from "./global.js";

// Convert geo coordinate from decimal to degrees, minutes and seconds.
export function geocoords(coord, latitude) {
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

export class Time {
  constructor(t) {
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


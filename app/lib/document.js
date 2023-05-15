// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Store, Reader} from "./frame.js";

export function detag(html) {
  return html.replace(/<\/?[^>]+(>|$)/g, "");
}

export class Mention {
  constructor(begin, end, annotation) {
    this.begin = begin;
    this.end = end;
    this.annotation = annotation;
  }
}

export class Document {
  constructor(store) {
    this.store = store;
    this.mentions = new Array();
    this.themes = new Array();
  }

  parse(lex) {
    let text = "";
    let stack = new Array();
    let r = new Reader(this.store, lex, true);
    while (!r.end()) {
      switch (r.ch) {
        case 91: { // '['
          stack.push(text.length);
          r.read();
          break;
        }

        case 124: { // '|'
          if (stack.length == 0) {
            text += '|';
          } else {
            r.read();
            r.next();
            let begin = stack.pop();
            let end = text.length;
            for (;;) {
              let obj = r.parse();
              this.mentions.push(new Mention(begin, end, obj));
              if (r.token == 93 || r.end()) break;
            }
          }
          break;
        }

        case 123: { // '{'
          r.next();
          let obj = r.parse();
          this.themes.push(obj);
          break;
        }

        default:
          text += String.fromCharCode(r.ch);
          r.read();
      }
    }
    this.text = text;
  }

  mention(idx) {
    return this.mentions[idx];
  }

  annotation(idx) {
    let mention = this.mentions[idx];
    return mention && mention.annotation;
  }

  phrase(idx, plain) {
    let mention = this.mentions[idx];
    if (!mention) return null;
    let text = this.text.slice(mention.begin, mention.end);
    if (plain) text = detag(text);
    return text;
  }

  plain() {
    return detag(this.text);
  }
}


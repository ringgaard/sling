// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Store, Reader} from "./frame.js";

export function detag(html) {
  return html.replace(/<\/?[^>]+(>|$)/g, "");
}

export class Mention {
  constructor(document, index, begin, end, annotation) {
    this.document = document;
    this.index = index;
    this.begin = begin;
    this.end = end;
    this.annotation = annotation;
  }

  text(plain) {
    let text = this.document.text.slice(this.begin, this.end);
    if (plain) text = detag(text);
    return text;
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
            r.read();
          } else {
            r.read();
            r.next();
            let begin = stack.pop();
            let end = text.length;
            for (;;) {
              let obj = r.parse();
              let index = this.mentions.length;
              this.mentions.push(new Mention(this, index, begin, end, obj));
              if (r.token == 93 || r.end()) break;
            }
          }
          break;
        }

        case 93: { // ']'
          if (stack.length == 0) {
            text += ']';
          } else {
            let begin = stack.pop();
            let end = text.length;
            let index = this.mentions.length;
            this.mentions.push(new Mention(this, index, begin, end));
          }
          r.read();
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


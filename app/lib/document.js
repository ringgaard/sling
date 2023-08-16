// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Store, Frame, Reader} from "./frame.js";

export function detag(html) {
  return html
    .replace(/<\/?[^>]+(>|$)/g, "")
    .replace(/&#(\d+);/g, (match, digits) => {
      return String.fromCharCode(parseInt(digits));
    });
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
  constructor(store, source, context) {
    this.store = store;
    this.source = source;
    this.context = context;

    this.mentions = new Array();
    this.themes = new Array();
    this.mapping = new Map();

    // Build phrase mapping.
    let phrasemap;
    if (this.source instanceof Frame) {
      for (let [k, v] of this.source) {
        if (typeof(k) === 'string') {
          if (!phrasemap) phrasemap = new Map();
          phrasemap.set(k, v);
        }
      }
    }

    // Parse LEX.
    let lex = store.resolve(this.source)
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
              let mention = new Mention(this, index, begin, end, obj);
              this.mentions.push(mention);
              this.mapping.set(obj, mention);
              if (phrasemap && (obj instanceof Frame)) {
                let phrase = text.slice(begin, end);
                let mapping = phrasemap.get(phrase);
                if (mapping) obj.add(store.is, mapping);
              }
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
            let mention = new Mention(this, index, begin, end);
            this.mentions.push(mention);
            if (phrasemap) {
              let phrase = text.slice(begin, end);
              let mapping = phrasemap.get(phrase);
              if (mapping) mention.annotation = mapping;
            }
          }
          r.read();
          break;
        }

        case 123: { // '{'
          r.read();
          r.next();
          let theme = r.parse_frame();
          this.themes.push(theme);
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


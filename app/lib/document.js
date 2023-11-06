// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Frame, Reader} from "./frame.js";

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
    this.sbegin = null;
    this.send = null;
  }

  text(plain) {
    let text = this.document.text.slice(this.begin, this.end);
    if (plain) text = detag(text);
    return text;
  }

  resolved() {
    if (!this.annotation) return null;
    let topic = this.document.store.resolve(this.annotation);
    if (topic.isanonymous()) return null;
    return topic;
  }
}

export class Document {
  constructor(store, source, context) {
    this.store = store;
    this.source = source;
    this.context = context;

    this.mentions = new Array();
    this.themes = new Array();

    // Build phrase mapping.
    let phrasemap;
    if (this.source instanceof Frame) {
      for (let [k, v] of this.source) {
        if (typeof(k) === 'string' && (v instanceof Frame)) {
          if (!phrasemap) phrasemap = new Map();
          phrasemap.set(k, v);
        }
      }
    }

    // Parse LEX.
    let lex = this.store.resolve(this.source)
    let text = "";
    let stack = new Array();
    let source_stack = new Array();
    let r = new Reader(this.store, lex, true);
    while (!r.end()) {
      switch (r.ch) {
        case 91: { // '['
          stack.push(text.length);
          source_stack.push(r.pos - 1);
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
            let sbegin = source_stack.pop();
            for (;;) {
              let obj;
              try {
                obj = r.parse();
                let index = this.mentions.length;
                let mention = new Mention(this, index, begin, end, obj);
                this.mentions.push(mention);
                mention.sbegin = sbegin;
                mention.send = r.pos - 1;
                if (phrasemap && (obj instanceof Frame)) {
                  let phrase = text.slice(begin, end);
                  let mapping = phrasemap.get(phrase);
                  if (mapping) {
                    if (obj.isanonymous()) {
                      obj.set(this.store.is, mapping);
                    } else {
                      mention.annotation = mapping;
                    }
                  }
                }
                if (r.token == 93 || r.end()) break;
              } catch (error) {
                console.log(`parse error: ${error} (${r.context()})`);
                break;
              }
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
            mention.sbegin = source_stack.pop();
            mention.send = r.pos;
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
          try {
            let theme = r.parse_frame();
            this.themes.push(theme);
          } catch (error) {
            console.log(`parse error: ${error} (${r.context()})`);
          }
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

  mention_of(annotation) {
    for (let m of this.mentions) {
      if (annotation === m.annotation) return m;
    }
  }

  first_mention(topic) {
    for (let m of this.mentions) {
      if (topic == this.store.resolve(m.annotation)) return m;
    }
  }

  search(query, partial) {
    let it = function* (mentions, query, partial) {
      for (let m of mentions) {
        let phrase = m.text();
        if (partial) {
          if (phrase.includes(query)) yield m;
        } else {
          if (phrase == query) yield m;
        }
      }
    }
    return it(this.mentions, query, partial);
  }
}


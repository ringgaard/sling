// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Store} from "./frame.js";
import {store, frame} from "./global.js";

function detag(html) {
  return html.replace(/<\/?[^>]+(>|$)/g, "");
}

class Mention {
  constructor(begin, end, annotation) {
    this.begin = begin;
    this.end = end;
    this.annotation = annotation;
  }
}

class Document {
  constructor(store, text) {
    this.store = store;
    this.text = text;
    this.mentions = new Array();
    this.themes = new Array();
  }

  mention(idx) {
    return this.mentions[idx];
  }

  annotation(idx) {
    let mention = this.mentions[idx];
    return mention && mention.annotation;
  }

  phrase(idx) {
    let mention = this.mentions[idx];
    if (!mention) return null;
    return this.text.slice(mention.begin, mention.end);
  }

  text(idx) {
    if (!idx) {
      return delex(this.text);
    } else {
      return delex(phrase(idx));
    }
  }

}

function delex(document, lex) {
  let text = "";
  let stack = new Array();
  let level = 0;
  let pos = 0;
  while (pos < lex.length) {
    let c = lex[pos++];
    switch (c) {
      case "[":
        stack.push(text.length);
        break;

      case "]": {
        let begin = stack.pop();
        let end = pos;
        document.mentions.push(new Mention(begin, end));
        break;
      }

      case "|": {
        if (stack.length == 0) {
          text += c;
        } else {
          let nesting = 0;
          let start = pos;
          while (pos < lex.length) {
            let c = lex[pos++];
            if (c == '{') {
              nesting++;
            } else if (c == '}') {
              nesting--;
            } else if (c == ']') {
              if (nesting == 0) break;
            }
          }
          let reader = new Reader(document.store, lex.slice(start, pos - 1));
          let obj = reader.parseall();
          document.mentions.push(new Mention(stack.pop(), text.length, obj));
        }
        break;
      }

      case "{": {
        let nesting = 0;
        let start = pos;
        while (pos < lex.length) {
          let c = lex[pos++];
          if (c == '{') {
            nesting++;
          } else if (c == '}') {
            if (--nesting == 0) break;
          }
        }
        let reader = new Reader(document.store, lex.slice(start - 1, pos));
        let obj = reader.parseall();
        document.themes.push(obj);
        break;
      }

      default:
        text += c;
    }
  }
  document.text = text;
}


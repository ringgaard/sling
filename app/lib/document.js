// Copyright 2023 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING document.

import {Frame, Reader, Printer} from "./frame.js";

// HTML escapes.
const escapes = {
  '&': "&#38;",
  '<': "&#60;",
  '>': "&#62;",
  '{': "&#123;",
  '|': "&#124;",
  '}': "&#125;",
  '[': "&#91;",
  ']': "&#93;",
}

export function detag(html) {
  return html
    .replace(/<\/?[^>]+(>|$)/g, "")
    .replace(/&#(\d+);/g, (match, digits) => {
      return String.fromCharCode(parseInt(digits));
    });
}

function escape(text) {
  return text.replace(/[&<>{}\[\]|]/g, c => escapes[c]);
}

// Convert DOM fragment into LEX format with text and mentions.
class DOMLexer {
  constructor(document, dom) {
    this.document = document;
    this.text = "";
    this.mentions = new Array();
    this.traverse_children(dom);
  }

  traverse(node) {
    if (node.nodeType == Node.TEXT_NODE) {
      this.text += escape(node.nodeValue);
    } else if (node.nodeType == Node.ELEMENT_NODE) {
      let tagname = node.nodeName.toLowerCase();
      if (tagname == "mention") {
        let begin = this.text.length;
        let mid = node.getAttribute("index");
        let annotation = this.document.mentions[mid]?.annotation;
        this.traverse_children(node);
        let end = this.text.length;
        let index = this.mentions.length;
        let mention = new Mention(this.document, index, begin, end, annotation);
        this.mentions.push(mention);
        node.setAttribute("index", index);
      } else if (tagname == "span") {
        this.traverse_children(node);
      } else if (tagname == "p") {
        if (this.text.slice(-1) != "\n") this.text += "\n";
        this.text += `<${tagname}>`;
        this.traverse_children(node);
        this.text = this.text.trimEnd();
        this.text += `</${tagname}>`;
      } else if (tagname == "div") {
        this.text += "<p>";
        this.traverse_children(node);
        this.text += "</p>\n";
      } else {
        this.text += `<${tagname}>`;
        this.traverse_children(node);
        this.text += `</${tagname}>`;
      }
    }
  }

  traverse_children(elem) {
    for (let child = elem.firstChild; child; child = child.nextSibling) {
      this.traverse(child);
    }
  }
}

export class Mention {
  constructor(document, index, begin, end, annotation) {
    this.document = document;
    this.index = index;
    this.begin = begin;
    this.end = end;
    this.annotation = annotation;
  }

  // (Plain) text for mention.
  text(plain) {
    let text = this.content || this.document.text.slice(this.begin, this.end);
    if (plain) text = detag(text);
    return text;
  }

  // Resolved mention annotation.
  resolved() {
    if (!this.annotation) return null;
    let topic = this.document.store.resolve(this.annotation);
    if (topic.isanonymous()) return null;
    return topic;
  }

  // Check if mention is empty.
  empty() {
    return this.begin == this.end;
  }

  // Return set of mentions depending on this mention.
  dependants() {
    let deps = new Array();
    let r = this.resolved();
    if (r) {
      let closure = new Set();
      closure.add(r);
      for (let m of this.document.mentions) {
        let r = m.resolved();
        if (closure.has(r)) {
          deps.push(m);
          closure.add(m.annotation);
        }
      }
    }
    return deps;
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
              try {
                let obj = r.parse();
                let mention = this.add_mention(begin, end, obj);
                mention.sbegin = sbegin;
                mention.send = r.pos - 1;
                if (phrasemap &&
                    (obj instanceof Frame) &&
                    obj.isanonymous() &&
                    !obj.has(this.store.is)) {
                  let phrase = text.slice(begin, end);
                  let mapping = phrasemap.get(phrase);
                  if (mapping) {
                    obj.set(this.store.is, mapping);
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
            let mention = this.add_mention(begin, end);
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

  readonly() {
    return this.context?.readonly;
  }

  add_mention(begin, end, annotation) {
    let index = this.mentions.length;
    let mention = new Mention(this, index, begin, end, annotation);
    this.mentions.push(mention);
    return mention;
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

  annotated_mentions() {
    let annotated = new Array();
    for (let m of this.mentions) {
      let a = m.annotation;
      if (!a || !(a instanceof Frame)) continue;
      if (a.isanonymous()) {
        let redirect = a.get(this.store.is);
        if (redirect && !redirect.isanonymous()) annotated.push(m);
      } else {
        annotated.push(m);
      }
    }
    return annotated;
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

  regenerate(dom) {
    let lexer = new DOMLexer(this, dom);
    this.mentions = lexer.mentions;
    this.text = lexer.text;
  }

  save() {
    let lex = this.tolex();
    if (this.source instanceof Frame) {
      this.source.set(this.store.is, lex);
    } else {
      this.source = lex;
    }
  }

  tohtml() {
    let h = new Array();

    // Mentions sorted by begin and end positions.
    let starts = this.mentions.slice();
    let ends = this.mentions.slice();
    starts.sort((a, b) => a.begin - b.begin || b.end - a.end);
    ends.sort((a, b) => a.end - b.end || b.begin - a.begin);

    // Generate HTML with mentions.
    let from = 0;
    let text = this.text;
    let n = this.mentions.length;
    let si = 0;
    let ei = 0;
    let level = 0;
    let match = this.context && this.context.match;
    let altmatch = match && match.get(this.store.is);
    let readonly = this.readonly();
    for (let pos = 0; pos < text.length; ++pos) {
      // Output span starts.
      while (si < n && starts[si].begin < pos) si++;
      while (si < n && starts[si].begin == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        level++;
        let cls = `l${level}`;
        let mention = starts[si];
        let topic = mention.resolved();
        if (topic) {
          if (match && (topic == match || topic == altmatch)) {
            cls += " highlight";
          }
        } else if (!readonly) {
          cls += " unknown";
        }
        h.push(`<mention class="${cls}" index=${mention.index}>`);
        si++;
      }

      // Output span ends.
      while (ei < n && ends[ei].end < pos) ei++;
      while (ei < n && ends[ei].end == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        level--;
        h.push("</mention>");
        ei++;
      }
    }
    h.push(text.slice(from));
    while (ei++ < n) h.push("</mention>");

    // TODO: output themes
    return h.join("");
  }

  tolex() {
    let h = new Array();
    let printer = new Printer(this.store);

    // Mentions sorted by begin and end positions.
    let starts = this.mentions.slice();
    let ends = this.mentions.slice();
    starts.sort((a, b) => a.begin - b.begin || b.end - a.end);
    ends.sort((a, b) => a.end - b.end || b.begin - a.begin);

    // Generate LEX with mentions.
    let from = 0;
    let text = this.text;
    let n = this.mentions.length;
    let si = 0;
    let ei = 0;

    function output_annotation(mention) {
      let annotation = mention?.annotation;
      if (annotation) {
        h.push("|");
        if (annotation.isanonymous()) {
          printer.print(annotation);
          h.push(printer.flush_output());
        } else {
          h.push(annotation.id);
        }
      }
    }

    for (let pos = 0; pos < text.length; ++pos) {
      // Output mention starts.
      while (si < n && starts[si].begin < pos) si++;
      while (si < n && starts[si].begin == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        if (!starts[si].empty()) {
          h.push("[");
        }
        si++;
      }

      // Output mention ends.
      while (ei < n && ends[ei].end < pos) ei++;
      while (ei < n && ends[ei].end == pos) {
        if (from < pos) {
          h.push(text.slice(from, pos));
          from = pos;
        }
        if (!ends[ei].empty()) {
          output_annotation(ends[ei]);
          h.push("]");
        }
        ei++;
      }
    }
    h.push(text.slice(from));
    while (ei < n) {
      if (!ends[ei].empty()) {
        output_annotation(ends[ei]);
        h.push("]");
      }
      ei++;
    }

    // TODO: output themes
    return h.join("");
  }
}


// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING topic widget for indexing books.

import {Component} from "/common/lib/component.js";
import {Frame} from "/common/lib/frame.js";
import {store, frame} from "/common/lib/global.js";
import {Document} from "/common/lib/document.js";
import {LabelCollector} from "/common/lib/datatype.js";

const n_lex = frame("lex");
const n_name = frame("name");

export default class BookWidget extends Component {
  onrendered() {
    let topic = this.state;
    if (!topic || !topic.has(n_lex)) return;

    this.attach(this.onalpha, "click", "#alpha");
    this.attach(this.onposition, "click", "#position");
    this.attach(this.onprominence, "click", "#prominence");
  }

  async index() {
    let book = this.state;
    let entrymap = new Map();
    let index = 0;
    let position = 1;
    let collector = new LabelCollector(store);
    for (let source of book.all(n_lex)) {
      let doc = new Document(store, source);

      let chaptermap = new Map();
      for (let m of doc.mentions) {
        if (doc.annotation) {
          let item = store.resolve(m.annotation);
          if (item && item.id) {
            let entity = chaptermap.get(item);
            if (!entity) {
              chaptermap.set(item, {position, count: 1});
              position++;
            } else {
              entity.count++;
            }
          }
        }
      }

      let context = {book, index};
      let name = source instanceof Frame && source.get(n_name);
      if (!name) name = book.get(n_name);
      if (!name) name = `Document ${index + 1}`;

      for (let [item, entity] of chaptermap) {
        let entry = entrymap.get(item);
        let count = entity.count;
        if (entry) {
          entry.count += count;
          entry.entries.push({context, item, name, count});
        } else {
          entry = {
            topic: item,
            position: entity.position,
            count: count,
            entries: [{context, item, name, count}],
          }
          entrymap.set(item, entry);
          collector.add_item(item);
        }
      }
      index++;
    }

    await collector.retrieve();

    let entries = new Array();
    for (let entry of entrymap.values()) {
      entry.name = (entry.topic.get(n_name) || entry.topic.id).toString();
      entries.push(entry);
    }

    return {
      name: book.get(n_name) || book.id,
      topic: book,
      entries: entries,
      open: true,
    };
  }

  async onalpha(e) {
    let index = await this.index(this.state);
    let options = {sensitivity: 'base'};
    index.entries.sort(
      (a, b) => a.name.localeCompare(b.name, undefined, options)
    );
    document.querySelector("drawer-panel").set_index(index);
  }

  async onposition(e) {
    let index = await this.index(this.state);
    index.entries.sort((a, b) => a.position - b.position);
    document.querySelector("drawer-panel").set_index(index);
  }

  async onprominence(e) {
    let index = await this.index(this.state);
    index.entries.sort((a, b) => b.count - a.count);
    document.querySelector("drawer-panel").set_index(index);
  }

  render() {
    let topic = this.state;
    if (!topic || !topic.has(n_lex)) return;

    return `
      <md-spacer></md-spacer>
      <md-menu icon="menu_book">
        <md-menu-item id="alpha">Index (alphabetically)</md-menu-item>
        <md-menu-item id="position">Index in order of appearance</md-menu-item>
        <md-menu-item id="prominence">Index by prominence</md-menu-item>
      </md-menu>
    `;
  }

  static stylesheet() {
    return `
      $ {
        display: flex;
      }
    `;
  }
};

Component.register(BookWidget);


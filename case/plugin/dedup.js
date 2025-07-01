// Copyright 2025 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING case plug-in for photo dedup.

import {Component} from "/common/lib/component.js";
import {store, frame} from "/common/lib/global.js";
import {imageurl} from "/common/lib/gallery.js";
import {MdDialog, inform} from "/common/lib/material.js";

const n_id = frame("id");
const n_media = frame("media");

// Singular/plural.
function plural(n, kind) {
  if (n == 1) return `one ${kind}`;
  if (n == 0) return `no ${kind}`;
  return `${n} ${kind}s`;
}

class TopicPhoto extends Component {
  render() {
    let photo = this.state;
    let url = imageurl(photo.url);
    if (photo.existing) {
      let label = `Existing ${photo.width} x ${photo.height}`
      if (photo.bigger) label += " bigger";
      if (photo.smaller) label += " smaller";
      return `
        <a href="${url}" target="_blank">
          <img src="${url}" referrerpolicy="no-referrer">
        </a>
        <div>${label}</div>
      `;
    } else {
      let label = `Remove ${photo.width} x ${photo.height}`
      if (photo.bigger) label += " bigger";
      if (photo.smaller) label += " smaller";
      return `
        <a href="${url}" target="_blank">
          <img src="${url}" referrerpolicy="no-referrer">
        </a>
        <div>
          <md-checkbox
            id="remove"
            label="${label}"
            checked=${!!photo.remove}>
          </md-checkbox>
        </div>
      `;
    }
  }

  url() {
    return this.state.url;
  }

  remove() {
    let checkbox = this.find("#remove");
    return checkbox && checkbox.checked;
  }

  static stylesheet() {
    return `
      $ {
        padding: 16px;
      }
      $ img {
        height: 250px;
      }
      $ div {
        font-size: 16px;
      }
      $ md-checkbox input {
        user-select: none;
      }
    `;
  }
}

Component.register(TopicPhoto);

class DedupDialog extends MdDialog {
  submit() {
    let selected = new Array();
    let photo = this.find("#photos").firstChild;
    while (photo) {
      if (photo.remove()) selected.push(photo.url());
      photo = photo.nextSibling;
    }

    if (this.find("#missing").checked) {
      selected.push(...this.state.missing)
    }

    this.close(selected);
  }

  render() {
    let missing = this.state.missing;
    return `
      <md-dialog-top>Remove photos</md-dialog-top>
      <div id="photos"></div>
      <div class="${missing.length > 0 ? "miss" : "nomiss"}">
        <md-checkbox
          id="missing"
          label="Remove ${plural(missing.length, "missing photo")}">
        </md-checkbox>
      </div>
      <md-dialog-bottom>
        <button id="cancel">Cancel</button>
        <button id="submit">Remove</button>
      </md-dialog-bottom>
    `;
  }

  onrendered() {
    let photos = this.find("#photos");
    for (let image of this.state.dups) {
      photos.appendChild(new TopicPhoto(image.dup));
      photos.appendChild(new TopicPhoto(image));
    }
  }

  static stylesheet() {
    return `
      $ #photos {
        display: grid;
        max-width: 75vw;
        max-height: 75vh;
        overflow: auto;
        grid-template-columns: 50% 50%;
      }
      $ .miss {
       padding: 16px;
      }
      $ .nomiss {
       display: none;
      }
    `;
  }
}

Component.register(DedupDialog);

export default class DedupPlugin {
  async run(topic, card) {
    // Get list of images.
    let media = [];
    let images = [];
    let seen = new Set();
    for (let m of topic.all(n_media)) {
      let url = store.resolve(m);
      if (url.startsWith('!')) url = url.slice(1);
      if (seen.has(url)) continue;
      media.push(m);
      images.push(url);
      seen.add(url);
    }
    if (images.length == 0) return;

    let existing = [];
    for (let redir of topic.links()) {
      for (let m of redir.all(n_media)) {
        let url = store.resolve(m);
        if (url.startsWith('!')) url = url.slice(1);
        existing.push(url);
      }
    }

    // Find duplicates.
    card.style.cursor = "wait";
    let r = await fetch("/case/service/dups", {
      method: "POST",
      body: JSON.stringify({itemid: topic.get(n_id), images, existing}),
    });
    card.style.cursor = "";
    let response = await r.json();
    for (let image of response.dups) {
      if (image.bigger) {
        image.dup.remove = true;
      } else {
        image.remove = true;
      }
    }

    if (response.dups.length == 0 && response.missing.length == 0) {
      inform("no duplicate photos found");
    } else {
      let dialog = new DedupDialog(response);
      let result = await dialog.show();
      if (result) {
        // Remove selected urls.
        topic.remove(n_media);
        for (let m of media) {
          let url = store.resolve(m);
          if (url.startsWith('!')) url = url.slice(1);
          if (!result.includes(url)) topic.add(n_media, m)
        }

        inform(`${plural(result.length, "photo")} removed`);
        return true;
      }
    }
 }
}

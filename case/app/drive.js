// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {generate_key} from "./crypto.js";

// Write file to drive and return url.
async function write(filename, content) {
  let url = `https://drive.ringgaard.com/${filename}`;
  console.log("write file to drive", url, content.length);
  let r = await fetch(url, {method: "PUT", body: content});
  if (!r.ok) return null;
  return url;
}

// Save file object to drive and return url.
async function save(file) {
  let url = `https://drive.ringgaard.com/d/${generate_key(8)}/${file.name}`;
  console.log("write file to drive", url, file.size);
  let r = await fetch(url, {
    method: "PUT",
    headers: {
      "Last-Modified": new Date(file.lastModified).toUTCString(),
    },
    body: file,
  });
  if (!r.ok) return null;
  return url;
}

// Get image from clipboard and store on drive.
async function paste_image() {
  // Get image from clipboard.
  let clipboard = await navigator.clipboard.read();
  let image = null;
  for (let i = 0; i < clipboard.length; i++) {
    if (clipboard[i].types.includes("image/png")) {
      // Get data from clipboard.
      let blob = await clipboard[i].getType("image/png");
      let data = await blob.arrayBuffer();
      image = new Uint8Array(data);
    }
  }
  if (!image) return null;

  // Generate image url.
  let fn = generate_key(8) + ".png";

  // Write image to drive.
  return write("i/" + fn, image);
}

export const Drive = {
  write: write,
  save: save,
  paste_image: paste_image,
}

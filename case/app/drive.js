// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// Alphabet for encoding serial numbers.
const digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// Encode number with alphabet.
function encode(num) {
  let s = [];
  let n = num;
  let base = digits.length;
  while (n > 0) {
    s.push(digits.charAt(n % base));
    n = (n / base) >> 0;
  }
  return s.reverse().join("");
}

// Return serial number based on millisec timestamp and random bits.
function serial() {
  let ts = Math.trunc(Date.now() * 1000);
  let rnd = new Uint32Array(2);
  window.crypto.getRandomValues(rnd);
  let salt = rnd[0] | (rnd[1] >> 32);
  return Math.abs((ts ^ salt) >> 0);
}

// Write file to drive and return url.
export async function write_to_drive(filename, content) {
  let url = `https://drive.ringgaard.com/${filename}`;
  console.log("write file to drive", url, content.length);
  let r = await fetch(url, {method: "PUT", body: content});
  if (!r.ok) return null;
  return url;
}

// Get image from clipboard and store on drive.
export async function paste_image() {
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
  let fn = encode(serial()) + ".png";

  // Write image to drive.
  return write_to_drive("i/" + fn, image);
}


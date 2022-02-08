// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

import {Encoder} from "/common/lib/frame.js";
import {store} from "./global.js";

const n_caseid = store.lookup("caseid");
const n_modified = store.lookup("modified");
const n_share = store.lookup("share");
const n_topics = store.lookup("topics");
const n_secret = store.lookup("secret");

const n_encryption = store.lookup("encryption");
const n_hashing = store.lookup("hashing");
const n_salt = store.lookup("salt");
const n_digest = store.lookup("digest");
const n_cipher = store.lookup("cipher");

// Default encryption: AES-CBC (NIST SP800-38A) with SHA-256 hashing.
const encryption = "AES-CBC";
const hashing = "SHA-256";
const keysize = 16;

const hexstr = [];
for (let n = 0; n <= 0xff; ++n) {
  hexstr.push(n.toString(16).padStart(2,"0"));
}

function bin2hex(bin) {
  if (!(bin instanceof Uint8Array)) bin = new Uint8Array(bin);
  let str = "";
  for (let n = 0; n < bin.length; ++n) {
    str += hexstr[bin[n]];
  }
  return str;
}

function hex2bin(str) {
  let size = str.length / 2;
  let bin = new Uint8Array(size);
  for (let n = 0; n < size; ++n) {
    bin[n] = parseInt(str.slice(n * 2, n * 2 + 2), 16);
  }
  return bin;
}

function cryptokey(secret, usage) {
  return crypto.subtle.importKey("raw", secret, encryption,  false, [usage]);
}

export function generate_key() {
  let key = crypto.getRandomValues(new Uint8Array(keysize));
  return bin2hex(key);
}

export async function encrypt(casefile) {
  // Construct plaintext from encoded case file.
  let encoder = new Encoder(store);
  for (let topic of casefile.get(n_topics)) {
    encoder.encode(topic);
  }
  encoder.encode(casefile);
  let plaintext = encoder.output();

  // Compute digest for verification.
  let digest = await crypto.subtle.digest(hashing, plaintext);

  // Encrypt case file using secret key and a newly generated salt.
  let secret = hex2bin(casefile.get(n_secret));
  let key = await cryptokey(secret, "encrypt")
  let salt = generate_key();
  let algo = {name: encryption, iv: hex2bin(salt)};
  let cipher = await crypto.subtle.encrypt(algo, key, plaintext);

  // Build encrypted case envelope.
  let encrypted = store.frame();
  encrypted.add(n_caseid, casefile.get(n_caseid));
  encrypted.add(n_modified, casefile.get(n_modified));
  encrypted.add(n_share, casefile.get(n_share));

  encrypted.add(n_encryption, encryption);
  encrypted.add(n_hashing, hashing);
  encrypted.add(n_digest, bin2hex(digest));
  encrypted.add(n_salt, salt);
  encrypted.add(n_cipher, bin2hex(cipher));

  return encrypted;
}

export async function decrypt(encrypted, secret) {
}


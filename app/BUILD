package(default_visibility = ["//visibility:public"])

load("//tools:embed.bzl", "embed_data")

cc_library(
  name = "app",
  deps = [
    ":lib",
    ":roboto-font",
    ":anubis-font",
    ":material-icons",
  ],
)

embed_data(
  name = "lib",
  srcs = [
    "lib/component.js",
    "lib/oneof.js",
    "lib/document.js",
    "lib/docview.js",
    "lib/docviewer.js",
    "lib/gallery.js",
    "lib/material.js",
    "lib/frame.js",
    "lib/global.js",
    "lib/datatype.js",
    "lib/reddit.js",
    "lib/hash.js",

    "image/appicon.ico",
    "image/rr-logo.svg",
  ],
)

embed_data(
  name = "roboto-font",
  srcs = [
    "font/roboto.css",

    "font/roboto-normal-latin.woff2",
    "font/roboto-normal-latin-ext.woff2",
    "font/roboto-normal-cyrillic.woff2",
    "font/roboto-normal-cyrillic-ext.woff2",
    "font/roboto-normal-greek.woff2",
    "font/roboto-normal-greek-ext.woff2",
    "font/roboto-normal-vietnamese.woff2",

    "font/roboto-500-latin.woff2",
    "font/roboto-500-latin-ext.woff2",
    "font/roboto-500-cyrillic.woff2",
    "font/roboto-500-cyrillic-ext.woff2",
    "font/roboto-500-greek.woff2",
    "font/roboto-500-greek-ext.woff2",
    "font/roboto-500-vietnamese.woff2",

    "font/roboto-italic-latin.woff2",
    "font/roboto-italic-latin-ext.woff2",
    "font/roboto-italic-cyrillic.woff2",
    "font/roboto-italic-cyrillic-ext.woff2",
    "font/roboto-italic-greek-ext.woff2",
    "font/roboto-italic-greek.woff2",
    "font/roboto-italic-vietnamese.woff2",
  ],
)

embed_data(
  name = "lato-font",
  srcs = [
    "font/lato.css",

    "font/lato-normal.woff2",
    "font/lato-normal-ext.woff2",
    "font/lato-bold.woff2",
    "font/lato-bold-ext.woff2",
    "font/lato-italic.woff2",
    "font/lato-italic-ext.woff2",
    "font/lato-bold-italic.woff2",
    "font/lato-bold-italic-ext.woff2",
  ],
)

embed_data(
  name = "lora-font",
  srcs = [
    "font/lora.css",

    "font/lora-normal-latin.woff2",
    "font/lora-normal-latin-ext.woff2",
    "font/lora-normal-cyrillic.woff2",
    "font/lora-normal-cyrillic-ext.woff2",
    "font/lora-normal-vietnamese.woff2",

    "font/lora-bold-latin.woff2",
    "font/lora-bold-latin-ext.woff2",
    "font/lora-bold-cyrillic.woff2",
    "font/lora-bold-cyrillic-ext.woff2",
    "font/lora-bold-vietnamese.woff2",

    "font/lora-italic-latin.woff2",
    "font/lora-italic-latin-ext.woff2",
    "font/lora-italic-cyrillic.woff2",
    "font/lora-italic-cyrillic-ext.woff2",
    "font/lora-italic-vietnamese.woff2",

    "font/lora-bold-italic-latin.woff2",
    "font/lora-bold-italic-latin-ext.woff2",
    "font/lora-bold-italic-cyrillic.woff2",
    "font/lora-bold-italic-cyrillic-ext.woff2",
    "font/lora-bold-italic-vietnamese.woff2",
  ],
)

embed_data(
  name = "anubis-font",
  srcs = [
    "font/anubis.css",
    "font/anubis-400.woff2",
    "font/anubis-500.woff2",
    "font/anubis-700.woff2",
    "font/anubis-italic.woff2",
  ],
)

embed_data(
  name = "material-icons",
  srcs = [
    "font/material.woff2",
    "font/material-outline.woff2",
  ],
)

embed_data(
  name = "digital-font",
  srcs = [
    "font/digital-7.mono.ttf",
  ],
)


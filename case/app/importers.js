// Copyright 2022 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING import plug-ins.

// Data importer plug-ins.
var importers = {
  "sling" : {
    name: "SLING frames",
    extension: ".sling",
    mime: "application/sling",
    module: "sling.js",
  },

  "json" : {
    name: "JSON objects",
    extension: ".json",
    mime: "application/json",
    module: "json.js",
  },

  "tsv" : {
    name: "Tab-separated values",
    extension: ".tsv",
    mime: "text/tab-separated-values",
    module: "tsv.js",
  },

  "csv": {
    name: "Comma-separated values",
    extension: ".csv",
    mime: "text/csv",
    module: "csv.js",
  },

  "qs": {
    name: "Quick Statements",
    extension: ".qs",
    mime: "application/quick-statements",
    module: "quickstmt.js",
  },
};


export class Context {
  constructor(casefile, editor) {
    this.casefile = casefile;
    this.editor = editor;
  }

  async new_topic(topic) {
    return await this.editor.new_topic(topic);
  }
};

export async function import_data(casefile, editor) {
  // Ask for file to import.
  let filetypes = new Array();
  for (let importer of Object.values(importers)) {
    filetypes.push({
      description: `${importer.name} (*${importer.extension})`,
      accept: {[importer.mime]: [importer.extension]},
    });
  }
  let [fh] = await window.showOpenFilePicker({
    types: filetypes,
    excludeAcceptAllOption: true,
    multiple: false,
  });

  // Find importer for file.
  let file = await fh.getFile();
  let extension = file.name.split(".").pop();
  let importer = importers[extension];

  // Load importer module if not already done.
  if (!importer.instance) {
    let module_url = `/case/plugin/${importer.module}`;
    console.log(`Load importer from ${module_url}`);
    try {
      const { default: component } = await import(module_url);
      importer.instance = new component();
    } catch (e) {
      console.log(e);
      throw `error loading importer plugin from ${module_url}`;
    }
  }

  // Call importer to import data from file.
  let context = new Context(casefile, editor);
  await importer.instance.process(file, context);
}


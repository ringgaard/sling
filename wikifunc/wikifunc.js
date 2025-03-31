// WikiFuntions keys.
const n_type = "Z1K1";
const n_id = "Z2K1";
const n_contents = "Z2K2";
const n_labels = "Z2K3";
const n_aliases = "Z2K4";
const n_descriptions = "Z2K5";
const n_string_value = "Z6K1";
const n_arguments = "Z8K1";
const n_return_type = "Z8K2";
const n_implementations = "Z8K4";
const n_language = "Z11K1";
const n_text = "Z11K2";
const n_texts = "Z12K1";
const n_code = "Z14K3";
const n_proglang = "Z16K1";
const n_progcode = "Z16K2";
const n_argtype = "Z17K1";
const n_argname = "Z17K2";
const n_arglabel = "Z17K3";
const n_javascript = "Z600";
const n_python = "Z610";
const n_english = "Z1002";

/*
//const wf_base_url = "https://www.wikifunctions.org/";
//const wf_api_url = wf_base_url + "api.php?";
//const wf_fetch_url =
//  wf_api_url + "action=wikilambda_fetch&origin=*&format=json&zids=";

const wf_fetch_url = "https://www.wikifunctions.org/w/api.php?action=wikilambda_fetch&format=json&origin=*&zids=";

// Fetch item from WikiFunctions.
async function fetch_wiki_function_item(zid) {
  let url = wf_fetch_url + zid;
  let r = await fetch(url);
  let data = await r.json();
  if (data.error) throw data.error;
  return new Item(JSON.parse(data[zid].wikilambda_fetch));
}
*/

const wf_fetch_url = "http://dev.ringgaard.com:8080/wikifunc/item?zid=";

// Fetch item from WikiFunctions.
async function fetch_wiki_function_item(zid) {
  let url = wf_fetch_url + zid;
  let r = await fetch(url);
  let data = await r.json();
  return new Item(JSON.parse(data));
}

// Pick English label.
function pick_label(labels) {
  for (let l of labels[n_texts].slice(1)) {
    if (l[n_language] == n_english) return l[n_text];
  }
}

class Argument {
  constructor(json) {
    this.json = json;
  }

  type() {
    return this.json[n_argtype];
  }

  name() {
    return this.json[n_argname];
  }

  label() {
    return pick_label(this.json[n_arglabel]);
  }
}

class Item {
  constructor(json) {
    this.json = json;
  }

  type() {
    return this.json[n_type];
  }

  id() {
    return this.json[n_id][n_string_value];
  }

  contents() {
    return this.json[n_contents];
  }

  label() {
    return pick_label(this.json[n_labels]);
  }

  description() {
    return pick_label(this.json[n_descriptions]);
  }

  arguments() {
    let args = new Array();
    for (let a of this.json[n_contents][n_arguments].slice(1)) {
      args.push(new Argument(a));
    }
    return args;
  }

  return_type() {
    return this.json[n_contents][n_return_type];
  }

  implementations() {
    return this.json[n_contents][n_implementations].slice(1);
  }

  pretty() {
    return JSON.stringify(this.json, null, 2);
  }

  signature() {
    let sig = this.id() + "(";
    let first = true;
    for (let arg of this.arguments()) {
      if (!first) sig += ", ";
      sig += arg.type() + " " + arg.name();
      first = false;
    }
    sig += ") -> " + this.return_type();
    return sig;
  }

  async select_implementation() {
    for (let iid of this.implementations()) {
      let item = await fetch_wiki_function_item(iid);
      let impl = item.contents();
      if (impl) {
        let code = impl[n_code];
        if (code) {
          let lang = code[n_proglang];
          let source = code[n_progcode];
          if (lang == n_javascript) return source;
        }
      }
    }
  }

  async compile() {
    let impl = await this.select_implementation();
    if (!impl) throw "No JS implementation found for " + id();

    let args = this.arguments();
    let argnames = new Array();
    for (let i = 0; i < args.length; ++i) {
      argnames.push(`_a${i}`);
    }

    let code = new Array();
    code.push(impl);
    code.push("\n");

    code.push("return ");
    code.push(this.id());
    code.push("(");
    code.push(argnames.join(", "));
    code.push(");\n");

    let body = code.join("");

    let f = Function(...argnames, body);
    return f;
  }
}

class WikiFunctions {
  constructor() {
    this.items = new Map();
    this.functions = new Map();
  }

  async item(zid) {
    let item = this.items.get(zid);
    if (!item) {
      item = await fetch_wiki_function_item(zid);
      this.items.set(zid, item);
    }
    return item;
  }

  async func(zid) {
    let f = this.functions.get(zid);
    if (f) return f;

    let item = this.items.get(zid);
    if (!item) item = await this.item(zid);

    f = await item.compile();
    this.functions.set(zid, f);
    return f;
  }
}

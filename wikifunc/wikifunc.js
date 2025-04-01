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
const n_to_code = "Z4K7";
const n_from_code = "Z4K8";
const n_to_converter = "Z46K3";
const n_from_converter = "Z64K3";
const n_native_type = "Z46K4";
const n_javascript = "Z600";
const n_python = "Z610";
const n_english = "Z1002";

// Cached WikiFunctions loader.
async function cached_loader(zid) {
  const wf_fetch_url = "https://ringgaard.com/wikifunc/item?zid=";
  let url = wf_fetch_url + zid;
  let r = await fetch(url);
  let data = await r.json();
  return new Item(data);
}

// WikiFunction loader from origin.
async function origin_loader(zid) {
  const wf_fetch_url = "https://www.wikifunctions.org/w/api.php?action=wikilambda_fetch&format=json&origin=*&zids=";
  let url = wf_fetch_url + zid;
  let r = await fetch(url);
  let data = await r.json();
  if (data.error) throw data.error;
  return new Item(JSON.parse(data[zid].wikilambda_fetch));
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

  async select_implementation(wf) {
    for (let iid of this.implementations()) {
      let item = await wf.item(iid);
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

  async compile(wf) {
    let impl = await this.select_implementation(wf);
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

    //console.log(body);

    let f = Function(...argnames, body);
    return f;
  }
}

////////////////////////////////////////////////

const IMPL_JS = 1;
const IMPL_COMPOSITION = 2;

const INSTR_CALL    = 1;
const INSTR_ASSIGN  = 2;
const INSTR_CONVERT = 3;
const INSTR_RETURN  = 4;

class Type {
  constructor(item) {
    this.item = item;
  }
}

class Variable {
  constructor(name, type) {
    this.name = name;
    this.type = type;
  }
}

class Instruction {
  constructor(type, func, dst, src) {
    this.type = type;
    this.func = func;
    this.dst = dst;
    this.src = src;
  }

  generate(code) {
    if (this.type == INSTR_CALL) {
      code.push("let ");
      code.push(this.dst.name);
      code.push(" = ");
      code.push(this.func.name());
      code.push("(");
      let first = true;
      for (let a of this.src) {
        if (!first) code.push(", ");
        code.push(a.name);
        first = false;
      }
      code.push(");\n");
    } else if (this.type == INSTR_ASSIGN) {
      code.push("let ");
      code.push(this.dst.name);
      code.push(" = ");
      code.push(this.src.name);
      code.push(";\n");
    } else if (this.type == INSTR_CONVERT) {
      code.push("let ");
      code.push(this.dst.name);
      code.push(" = ");
      code.push(this.func.name());
      code.push("(");
      code.push(this.src.name);
      code.push(");\n");
    } else if (this.type == INSTR_RETURN) {
      code.push("return ");
      code.push(this.src.name);
      code.push(";\n");
    } else {
      throw "Illegal instruction";
    }
  }
}

class Block {
  constructor() {
    this.instructions = new Array();
  }

  emit(type, func, dst, src) {
    this.instructions.push(new Instruction(type, func, dst, src));
  }

  generate(code) {
    for (let instr of this.instructions) {
      instr.generate(code);
    }
  }
}

class Converter {
  constructor(item) {
    this.item = item;
  }

  name() {
    return this.item.id();
  }

  code() {
    let contents = this.item.contents();
    let converter = contents[n_to_converter] || contents[n_from_converter];
    return converter[n_progcode];
  }

  jstype() {
    let contents = this.item.contents();
    let converter = contents[n_to_converter] || contents[n_from_converter];
    return converter.Z46K4 || converter.Z46K4;
  }
}

class Composition {
  constructor(item) {
    this.item = item;
  }

  kind() { return IMPL_COMPOSiTION; }
}

class Native {
  constructor(item) {
    this.item = item;
  }

  kind() { return IMPL_JS; }
  code() { return this.item.contents()[n_code][n_progcode]; }
}

class Func {
  constructor(item) {
    this.item = item;
    this.impl = null;
    this.args = new Array();
    this.rettype = null;
    this.body = new Block();
  }

  name() {
    return this.item.id();
  }

  implementations() {
    return this.item.contents()[n_implementations].slice(1);
  }
}

class Compiler {
  constructor(wf) {
    this.wf = wf;
    this.args = new Array();
    this.retval = null;
    this.funcs = new Map();
    this.converters = new Map();
    this.types = new Map();
    this.variables = new Array();
    this.body = new Block();
  }

  async compile(zid) {
    let main = await this.get_func(zid);

    let main_args = new Array();
    for (let a in main.args) {
      let v = this.alloc_var(a);
      this.args.push(v);
      main_args.push(v);
    }

    for (let i = 0; i < main.args.length; ++i) {
      let converter = main.args[i].type.tojs;
      if (main.args[i].type.tojs) {
        let native_arg = this.alloc_var(converter.jstype());
        this.emit_convert(converter, native_arg, main_args[i]);
        main_args[i] = native_arg;
      }
    }
    let main_ret = this.alloc_var(main.rettype);
    let native_ret = main_ret;
    if (main.rettype.fromjs) {
      native_ret = this.alloc_var(main.rettype.fromjs.jstype());
    }
    this.emit_call(main, native_ret, main_args);
    if (main_ret != native_ret) {
      this.emit_convert(main.rettype.fromjs, main_ret, native_ret);
    }
    this.emit_return(main_ret);

    // Assign variable names.
    for (let index = 0; index < this.variables.length; ++index) {
      this.variables[index].name = `_v${index}`;
    }

    // Generate code.
    let code = new Array();
    for (let converter of this.converters.values()) {
      code.push(converter.code());
      code.push("\n\n");
    }
    for (let func of this.funcs.values()) {
      code.push(func.impl.code());
      code.push("\n\n");
    }
    this.body.generate(code);

    // Construct function.
    let argnames = new Array();
    for (let a of this.args) argnames.push(a.name);
    //let body = code.join("");
    //console.log(`function ${main.name()}(${argnames.join(", ")})\n${body}}`);
    let f = Function(...argnames, body);
    return f;
  }

  alloc_var(type) {
    let v = new Variable(null, type);
    this.variables.push(v);
    return v;
  }

  emit_call(func, retval, args) {
    this.body.emit(INSTR_CALL, func, retval, args)
  }

  emit_return(v) {
    this.body.emit(INSTR_RETURN, null, null, v)
  }

  emit_convert(converter, dst, src) {
    this.body.emit(INSTR_CONVERT, converter, dst, src)
  }

  async get_func(zid) {
    // Check if function is already known.
    let func = this.funcs.get(zid);
    if (func) return func;

    // Fetch function item.
    let item = await this.wf.item(zid);
    func = new Func(item);

    // Select implementation.
    let implementations = func.item.contents()[n_implementations].slice(1);
    for (let i of implementations) {
      let item = await this.wf.item(i);
      let contents = item.contents();
      if (contents) {
        let code = contents[n_code];
        if (code) {
          let lang = code[n_proglang];
          if (lang == n_javascript) {
            func.impl = new Native(item);
            break;
          }
        }
      }
    }

    // Get arguments.
    for (let a of func.item.contents()[n_arguments].slice(1)) {
      let name = a[n_argname];
      let typeid = a[n_argtype];
      let type = await this.get_type(typeid);
      func.args.push(new Variable(name, type));

      if (func.impl.kind() == IMPL_JS) {
        // Get type converter to JS.
        if (type.tojs === undefined) {
          let converters = type.item.contents()[n_to_code];
          if (converters) {
            for (let c of converters.slice(1)) {
              let item = await this.wf.item(c);
              let lang = item.contents()[n_to_converter][n_proglang];
              if (lang == n_javascript) {
                let converter = new Converter(item);
                this.converters.set(c, converter);
                type.tojs = converter;
              }
            }
          }
          if (!type.tojs) type.tojs = null;
        }
      }
    }

    // Get return type.
    let rettypeid = func.item.contents()[n_return_type];
    func.rettype = await this.get_type(rettypeid);

    // Get type converter from JS.
    if (func.impl.kind() == IMPL_JS) {
      if (func.rettype.fromjs === undefined) {
        let converters = func.rettype.item.contents()[n_from_code];
        if (converters) {
          for (let c of converters.slice(1)) {
            let item = await this.wf.item(c);
            let lang = item.contents()[n_from_converter][n_proglang];
            if (lang == n_javascript) {
              let converter = new Converter(item);
              this.converters.set(c, converter);
              func.rettype.fromjs = converter;
            }
          }
        }
        if (!func.rettype.fromjs) func.rettype.fromjs = null;
      }
    }

    this.funcs.set(zid, func);
    return func;
  }

  async get_type(zid) {
    // Check if function is already known.
    let type = this.types.get(zid);
    if (type) return type;

    // Fetch type item.
    let item = await this.wf.item(zid);
    type = new Type(item);

    this.types.set(zid, type);
    return type;

  }
}

class WikiFunctions {
  constructor() {
    this.load = cached_loader;
    //this.load = origin_loader;
    this.items = new Map();
    this.functions = new Map();
  }

  async item(zid) {
    let item = this.items.get(zid);
    if (!item) {
      item = await this.load(zid);
      this.items.set(zid, item);
    }
    return item;
  }

  async func(zid) {
    let f = this.functions.get(zid);
    if (f) return f;

    let item = this.items.get(zid);
    if (!item) item = await this.item(zid);

    let compiler = new Compiler(this);
    f = await compiler.compile(zid);

    this.functions.set(zid, f);
    return f;
  }
}

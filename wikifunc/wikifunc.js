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

class Item {
  constructor(json) {
    this.json = json;
  }

  type() {
    return this.json.Z2K2?.Z1K1;
  }

  id() {
    return this.json.Z2K1?.Z6K1;
  }

  contents() {
    return this.json.Z2K2;
  }

  label() {
    for (let l of this.json.Z2K3.Z12K1.slice(1)) {
      if (l.Z11K1 == "Z1002") return l.Z11K2;
    }
  }

  description() {
    for (let l of this.json.Z2K5.Z12K1.slice(1)) {
      if (l.Z11K1 == "Z1002") return l.Z11K2;
    }
  }

  pretty() {
    return JSON.stringify(this.json, null, 2);
  }
}

class NameGenerator {
  constructor(prefix) {
    this.prefix = prefix;
    this.index = 0;
  }

  next() {
    let index = this.index++;
    return this.prefix + index;
  }
}

class Type {
  constructor(item) {
    this.item = item;
  }

  name() {
    return this.item.id();
  }
}

class Variable {
  constructor(name, type) {
    this.name = name;
    this.type = type;
  }

  baptize(namegen) {
    if (!this.name) this.name = namegen.next();
  }
}

const INSTR_CALL     = 1;
const INSTR_ASSIGN   = 2;
const INSTR_CONST    = 3;
const INSTR_CONVERT  = 4;
const INSTR_RETURN   = 5;
const INSTR_DECLARE  = 6;


class Instruction {
  constructor(type, func, dst, src) {
    this.type = type;
    this.func = func;
    this.dst = dst;
    this.src = src;
  }

  baptize(namegen) {
    if (this.dst) this.dst.baptize(namegen);
  }

  generate(code) {
    //this.annotate(code);
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
      code.push(this.dst.name);
      code.push(" = ");
      code.push(this.src.name);
      code.push(";\n");
    } else if (this.type == INSTR_CONST) {
      code.push("const ");
      code.push(this.dst.name);
      code.push(" = ");
      code.push(JSON.stringify(this.src));
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
    } else if (this.type == INSTR_DECLARE) {
      code.push("let ");
      code.push(this.src.name);
      code.push(";\n");
    } else {
      throw "Illegal instruction";
    }
  }

  annotate(code) {
    if (this.type == INSTR_CALL) {
      code.push("// Call ");
      code.push(this.func.item.label());
      code.push("\n");
    } else if (this.type == INSTR_CONVERT) {
      code.push("// Convert ");
      code.push(this.src?.type?.item?.label() || this.src?.type || "??");
      code.push(" -> ");
      code.push(this.dst?.type?.item?.label()|| this.dst?.type || "??");
      code.push(" using ");
      code.push(this.func.item.label());
      code.push("\n");
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

  emit_call(func, retval, args) {
    this.emit(INSTR_CALL, func, retval, args);
  }

  emit_return(v) {
    this.emit(INSTR_RETURN, null, null, v);
  }

  emit_declare(v) {
    this.emit(INSTR_DECLARE, null, v, null);
  }

  emit_const(v, s) {
    this.emit(INSTR_CONST, null, v, s);
  }

  emit_convert(converter, dst, src) {
    this.emit(INSTR_CONVERT, converter, dst, src)
  }

  emit_evoke(func, retval, args) {
    let kind = func.impl.kind();
    if (kind == IMPL_JS) {
      // Convert argument to native form.
      let native_args = Array.from(args);
      for (let i = 0; i < native_args.length; ++i) {
        let arg = native_args[i];
        let converter = arg.type.tojs;
        if (converter) {
          let native_arg = new Variable(null, converter.jstype());
          this.emit_convert(converter, native_arg, arg);
          native_args[i] = native_arg;
        }
      }

      if (func.rettype.fromjs) {
        // Call native with output conversion.
        let native_ret = new Variable(null, func.rettype.fromjs.jstype());
        this.emit_call(func, native_ret, native_args);
        this.emit_convert(func.rettype.fromjs, retval, native_ret);
      } else {
        // Call native.
        this.emit_call(func, retval, native_args);
      }
    } else if (kind == IMPL_COMPOSITION) {
      this.emit_call(func, retval, args);
    } else if (kind == IMPL_BUILTIN) {
      this.emit_call(func, retval, args);
    } else {
      throw "Unknown function kind";
    }
  }

  baptize(namegen) {
    for (let instr of this.instructions) {
      instr.baptize(namegen);
    }
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

  jstype() {
    let contents = this.item.contents();
    return contents.Z46K4 || contents.Z64K4;
  }

  generate(code) {
    let contents = this.item.contents();
    let converter = contents.Z46K3 || contents.Z64K3;
    code.push(converter.Z16K2);
  }
}

class Composition {
  constructor(item) {
    this.item = item;
  }

  kind() { return IMPL_COMPOSITION; }

  baptize(namegen) {
    if (this.body) this.body.baptize(namegen);
  }

  generate(code) {
    code.push("function ");
    code.push(this.item.contents().Z14K1);
    code.push("(");
    for (let i = 0; i < this.args.length; ++i) {
      if (i > 0) code.push(", ");
      code.push(this.args[i].name);
    }
    code.push(") {\n");
    this.body?.generate(code);
    code.push("}\n");
  }
}

const IMPL_JS = 1;
const IMPL_COMPOSITION = 2;
const IMPL_BUILTIN = 3;

class Native {
  constructor(item) {
    this.item = item;
  }

  kind() { return IMPL_JS; }
  baptize(namegen) {}
  generate(code) {
    code.push(this.item.contents().Z14K3.Z16K2);
    code.push("\n\n");
  }
}

class BuiltIn {
  constructor(item) {
    this.item = item;
  }

  kind() { return IMPL_BUILTIN; }
  baptize(namegen) {}
  generate(code) {
    code.push("// BUILTIN ");
    code.push(this.item.id());
    code.push("\n\n");
  }
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

  arg(name) {
    for (let a of this.args) {
      if (a.name == name) return a;
    }
  }

  baptize(namegen) {
    this.impl?.baptize(namegen);
  }

  generate(code) {
    this.impl.generate(code);
  }

  implementations() {
    return this.item.contents().Z8K4.slice(1);
  }
}

class Compiler {
  constructor(wf) {
    this.wf = wf;
    this.types = new Map();
    this.funcs = new Map();
    this.converters = new Map();
  }

  async compile(zid) {
    // Fetch main function.
    let func = await this.func(zid);

    // Create variables for main function.
    let args = new Array();
    for (let a of func.args) {
      let v = new Variable(null, a.type);
      args.push(v);
    }
    let retval = new Variable(null, func.rettype);

    // Generate main function.
    let body = new Block();
    body.emit_evoke(func, retval, args);
    body.emit_return(retval);

    // Generate compositions.
    for (;;) {
      // Find remaining functions that have not been generated.
      let remaining = new Array();
      for (let func of this.funcs.values()) {
        let impl = func.impl;
        if (impl.kind() == IMPL_COMPOSITION && !impl.body) {
          remaining.push(func);
        }
      }
      if (remaining.length == 0) break;;

      // Compose remaining functions.
      for (let f of remaining) {
        f.impl.body = new Block();
        let top = f.impl.item.contents().Z14K2;
        let retval = await this.compose(f, top);
        f.impl.body.emit_return(retval);
      }
    }

    // Assign variable names.
    let namegen = new NameGenerator("_v");
    for (let a of args) a.baptize(namegen);
    for (let f of this.funcs.values()) f.baptize(namegen);
    body.baptize(namegen);

    // Generate code.
    let code = new Array();
    for (let converter of this.converters.values()) {
      converter.generate(code);
      code.push("\n");
    }
    for (let func of this.funcs.values()) {
      func.generate(code);
      code.push("\n");
    }
    body.generate(code);

    // Return code.
    let argnames = new Array();
    for (let a of args) argnames.push(a.name);
    return {
      name: zid,
      label: func.item.label(),
      description: func.item.description(),
      args: argnames,
      body: code.join(""),
    };
  }

  async compose(func, expr) {
    if (typeof(expr) === 'string') {
      // Constant.
      let v = new Variable(null, typeof(expr));
      func.impl.body.emit_const(v, expr);
      return v;
    } else if (expr.Z1K1 == "Z7") {
      // Get called function.
      let callee = await this.func(expr.Z7K1);

      // Compose arguments.
      let args = new Array();
      for (let a of callee.args) {
        let subexpr = expr[a.name];
        let v = await this.compose(func, subexpr);
        args.push(v);
      }

      // Call function.
      let retval = new Variable(null, callee.rettype);
      func.impl.body.emit_evoke(callee, retval, args);
      return retval;
    } else if (expr.Z1K1 == "Z18") {
      // Argument reference.
      return func.arg(expr.Z18K1);
    } else {
      throw `Unknown expression type: ${expr.Z1K1}`;
    }
  }

  async func(zid) {
    // Check if function is already known.
    let func = this.funcs.get(zid);
    if (func) return func;

    // Fetch function item.
    let item = await this.wf.item(zid);
    func = new Func(item);

    // Check that item is a function.
    if (item.type() != "Z8") throw `${zid} is not a function`;

    // Select implementation.
    let fallback;
    let builtin;
    for (let i of func.implementations()) {
      let item = await this.wf.item(i);
      let contents = item.contents();
      if (contents) {
        let code = contents.Z14K3;
        if (code) {
          let lang = code.Z16K1;
          if (lang == "Z600") {
            func.impl = new Native(item);
            break;
          }
        }
        if (!builtin && contents.Z14K2) {
          builtin = item;
        }
        if (!fallback && contents.Z14K2) {
          fallback = item;
        }
      }
    }
    if (!func.impl && fallback) {
      func.impl = new Composition(fallback);
    }
    if (!func.impl && builtin) {
      func.impl = new BuiltIn(builtin);
    }
    if (!func.impl) {
      throw `No supported implementation for ${func.name()}`;
    }

    // Get arguments.
    for (let a of func.item.contents().Z8K1.slice(1)) {
      let name = a.Z17K2;
      let typeid = a.Z17K1;
      let type = await this.type(typeid);
      func.args.push(new Variable(name, type));

      if (func.impl.kind() == IMPL_JS) {
        // Get type converter to JS.
        if (type.tojs === undefined) {
          let converters = type.item.contents().Z4K7;
          if (converters) {
            for (let c of converters.slice(1)) {
              let item = await this.wf.item(c);
              let lang = item.contents().Z46K3.Z16K1;
              if (lang == "Z600") {
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

    if (func.impl.kind() == IMPL_COMPOSITION) {
      func.impl.args = func.args;
    }

    // Get return type.
    let rettypeid = func.item.contents().Z8K2;
    func.rettype = await this.type(rettypeid);

    // Get type converter from JS.
    if (func.impl.kind() == IMPL_JS) {
      if (func.rettype.fromjs === undefined) {
        let converters = func.rettype.item.contents().Z4K8;
        if (converters) {
          for (let c of converters.slice(1)) {
            let item = await this.wf.item(c);
            let lang = item.contents().Z64K3.Z16K1;
            if (lang == "Z600") {
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

  async type(zid) {
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

  async compile(zid) {
    let compiler = new Compiler(this);
    return await compiler.compile(zid);
  }

  async func(zid) {
    let f = this.functions.get(zid);
    if (f) return f;

    let code = await this.compile(zid);

    f = Function(...code.args, code.body);

    this.functions.set(zid, f);
    return f;
  }
}

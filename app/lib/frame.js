// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING frame store implementation for JavaScript.

var strdecoder = new TextDecoder("utf8");
var strencoder = new TextEncoder();
var numbuf = new ArrayBuffer(4);
var intbuf = new Int32Array(numbuf);
var fltbuf = new Float32Array(numbuf);
var namepat = /^[A-Za-z_\/][A-Za-z0-9_\-\/]*$/;

// Convert binary 32-bit IEEE-754 float to number.
function bits_to_float(bits) {
  intbuf[0] = bits;
  return fltbuf[0];
}

// Convert number to binary 32-bit IEEE-754 float.
function float_to_bits(num) {
  fltbuf[0] = num;
  return intbuf[0];
}

// Check for binary marker.
function has_binary_marker(data) {
  if (data instanceof ArrayBuffer) {
    let bytes = new Uint8Array(data);
    return bytes[0] == 0;
  } else {
    return false;
  }
}

// Frame states.
const PROXY = 0;      // placeholder object with only the id
const STUB = 1;       // object with id(s) and name(s)
const PUBLIC = 2;     // frame with id(s)
const ANONYMOUS = 3;  // frame with no id(s)

// Frame store.
export class Store {
  // Initialize new frame store.
  constructor(globals) {
    // Initialize store.
    this.globals = globals;
    this.frames = new Map();

    // Initialize standard symbols.
    if (globals) {
      this.id = globals.id;
      this.isa = globals.isa;
      this.is = globals.is;
      this.name = globals.name;
    } else {
      this.id = new Frame(this, []);
      this.id.slots = [this.id, "id"];
      this.isa = new Frame(this, [this.id, "isa"]);
      this.is = new Frame(this, [this.id, "is"]);
      this.name = new Frame(this, [this.id, "name"]);
      this.add(this.id);
      this.add(this.isa);
      this.add(this.is);
      this.add(this.name);
    }
  }

  // Register frame under id in store.
  register(id, frame) {
    this.frames.set(id, frame);
    frame.state = PUBLIC;
  }

  // Unregister public frame from store.
  unregister(frame) {
    if (frame.isanonymous()) return;
    let slots = frame.slots;
    for (let n = 0; n < slots.length; n += 2) {
      if (slots[n] === this.id) this.frames.delete(slots[n + 1]);
    }
    frame.remove(this.id);
    frame.state = ANONYMOUS;
  }

  // Add frame to store.
  add(frame) {
    let slots = frame.slots;
    if (slots) {
      for (let n = 0; n < slots.length; n += 2) {
        if (slots[n] === this.id) this.register(slots[n + 1], frame);
      }
    }
  }

  // Create frame and add it to the store.
  frame(slots) {
    if (typeof slots === 'string') slots = [this.id, slots];
    let f = new Frame(this, slots);
    this.add(f);
    return f;
  }

  // Create proxy frame and add it to the store.
  proxy(id) {
    let proxy = new Frame(this);
    proxy.state = PROXY;
    proxy.slots = [this.id, id];
    this.frames.set(id, proxy);
    return proxy;
  }

  // Try to find frame in store. Return undefined if frame is not found.
  find(id) {
    return this.frames.get(id);
  }

  // Look up frame in store. This also searches the global store. Returns a new
  // proxy if frame is not found.
  lookup(id) {
    let frame = this.frames.get(id);
    if (!frame && this.globals) {
      frame = this.globals.lookup(id);
    }
    if (frame) return frame;
    return this.proxy(id);
  }

  // Resolve value by following is: chain.
  resolve(obj) {
    while (true) {
      if (obj instanceof Frame) {
        if (!obj.isanonymous()) return obj;
        let qua = obj.get(this.is);
        if (qua === undefined) return obj;
        obj = qua;
      } else {
        return obj;
      }
    }
  }

  // Check if object is a qualifier frame.
  isqualifier(obj) {
    if (obj instanceof Frame) {
      return obj.has(this.is) && !obj.has(this.id);
    } else {
      return false;
    }
  }

  // Import object from other store into this store. Returns new object.
  transfer(obj) {
    if (obj instanceof Frame) {
      if (obj.store != this) {
        if (obj.isanonymous()) {
          let size = obj.slots.length;
          let frame = new Frame(this);
          frame.slots = new Array(size);
          for (let i = 0; i < size; ++i) {
            frame.slots[i] = this.transfer(obj.slots[i]);
          }
          obj = frame;
        } else {
          obj = this.lookup(obj.id);
        }
      }
    } else if (obj instanceof Array) {
      let size = obj.length;
      let array = new Array(size);
      for (let i = 0; i < size; ++i) {
        array[i] = this.transfer(obj[i]);
      }
      obj = array;
    }
    return obj;
  }

  // Parse input data and return first object.
  parse(data) {
    if (data instanceof Response) {
      if (!data.ok) {
        throw `Error retrieving data from ${data.url}: ${data.statusText}`;
      }
      return data.arrayBuffer().then(data => this.parse(data));
    }
    if (data instanceof Blob) {
      return data.arrayBuffer().then(data => this.parse(data));
    }

    if (has_binary_marker(data)) {
      let decoder = new Decoder(this, data);
      return decoder.readall();
    } else {
      let reader = new Reader(this, data);
      let obj = reader.parseall();
      return obj;
    }
  }

  // Encode object and return binary representation.
  encode(obj) {
    let encoder = new Encoder(this);
    encoder.encode(obj);
    return encoder.output();
  }

  // Return localized string.
  localized(s, lang) {
    if (lang) {
      return new QString(s, this.lookup(`/lang/${lang}`));
    } else {
      return s;
    }
  }
}

// A frame has a list of (name,value) slots.
export class Frame {
  // Create new frame with slots.
  constructor(store, slots) {
    this.store = store;
    this.slots = slots ? Array.from(slots) : null;
    this.state = ANONYMOUS;
  }

  // Return (first) value for frame slot.
  get(name) {
    if (typeof name === 'string') name = this.store.lookup(name);
    if (this.slots) {
      for (let n = 0; n < this.slots.length; n += 2) {
        if (this.slots[n] === name) return this.slots[n + 1];
      }
    }
    return undefined;
  }

  // Return (first) resolved value for frame slot.
  resolved(name) {
    let v = this.get(name);
    if (v instanceof Frame) v = this.store.resolve(v);
    return v;
  }

  // Check if frame has slot with name (and value).
  has(name, value) {
    if (typeof name === 'string') name = this.store.lookup(name);
    if (!this.slots) return false;
    for (let n = 0; n < this.slots.length; n += 2) {
      if (this.slots[n] === name) {
        if (value === undefined) return true;
        if (this.store.resolve(this.slots[n + 1]) == value) return true;
      }
    }
    return false;
  }

  // Check if frame contains value.
  contains(value) {
    if (!this.slots) return false;
    for (let n = 0; n < this.slots.length; ++n) {
      if (this.store.resolve(this.slots[n]) === value) return true;
    }
    return false;
  }

  // Return id for frame.
  get id() {
    if (this.state == ANONYMOUS) return undefined;
    return this.get(this.store.id);
  }

  // Check if frame is a proxy.
  isproxy() {
    return this.state == PROXY;
  }

  // Check if frame is a stub.
  isstub() {
    return this.state == STUB;
  }

  // Check if frame is public.
  ispublic() {
    return this.state == PUBLIC;
  }

  // Check if frame is anonymous.
  isanonymous() {
    return this.state == ANONYMOUS;
  }

  // Check if frame is a qualifier frame.
  isqualifier() {
    return this.has(this.store.is) && !this.has(this.store.id);
  }

  // Mark frame as a stub.
  markstub() {
    this.state = STUB;
  }

  // Add slot to frame.
  add(name, value) {
    if (!this.slots) this.slots = new Array();
    this.slots.push(name);
    this.slots.push(value);
  }

  // Insert slot at position.
  insert(pos, name, value) {
    if (!this.slots) this.slots = new Array();
    this.slots.splice(pos * 2, 0, name, value);
  }

  // Add slot if it is not already in frame. Return true if a new slot was
  // added. If there is already a slot with the same name, the new slot is
  // inserted right after the last slot with this name.
  put(name, value) {
    if (value === undefined) return false;
    if (this.slots) {
      let pos = 0;
      for (let n = 0; n < this.slots.length; n += 2) {
        if (this.slots[n] === name) {
          if (value === undefined) return true;
          if (this.store.resolve(this.slots[n + 1]) == value) return false;
          pos = n + 2;
        }
      }
      if (pos) {
        this.slots.splice(pos, 0, name, value);
        return true;
      }
    }
    this.add(name, value);
    return true;
  }

  // Set (or add/remove) slot value.
  set(name, value) {
    if (value === undefined) {
      this.remove(name);
    } else {
      if (this.slots) {
        for (let n = 0; n < this.slots.length; n += 2) {
          if (this.slots[n] === name) {
            this.slots[n + 1] = value;
            return;
          }
        }
      }
      this.add(name, value);
    }
  }

  // Return number of slots for frame.
  get length() {
    return this.slots ? this.slots.length / 2 : 0;
  }

  // Return name for nth slot.
  name(n) {
    if (n === undefined) return get(this.store.name);
    return this.slots[n * 2];
  }

  // Return value for nth slot.
  value(n) {
    return this.slots[n * 2 + 1];
  }

  // Set name for nth slot.
  set_name(n, name) {
    this.slots[n * 2] = name;
  }

  // Set value for nth slot.
  set_value(n, value) {
    this.slots[n * 2 + 1] = value;
  }

  // Return slot index of nth slot with name in frame.
  slot(name, nth = 0) {
    if (!this.slots) return;
    let i = 0;
    for (let n = 0; n < this.slots.length; ++n) {
      if (this.slots[n * 2] === name) {
        if (i == nth) return n;
        i++;
      }
    }
  }

  // Remove all slots with name or nth slot.
  remove(n) {
    if (!this.slots) return;
    if (n instanceof Frame) {
      let slots = this.slots;
      let i = 0;
      let l = slots.length;
      while (i < l && slots[i] != n) i += 2;
      if (i != l) {
        let j = i;
        while (i < l) {
          if (slots[i] == n) {
            i += 2;
          } else {
            slots[j++] = slots[i++];
            slots[j++] = slots[i++];
          }
        }
        slots.length = j;
      }
    } else {
      this.slots.splice(n * 2, 2);
    }
  }

  // Rename slots.
  rename(from, to) {
    if (this.slots) {
      for (let n = 0; n < this.slots.length; n += 2) {
        if (this.slots[n] === from) {
          this.slots[n] = to;
        }
      }
    }
  }

  // Replace slot values.
  replace(name, from, to) {
    let count = 0;
    if (this.slots) {
      for (let n = 0; n < this.slots.length; n += 2) {
        if (this.slots[n] === name && this.slots[n + 1] == from) {
          this.slots[n + 1] = to;
          count += 1
        }
      }
    }
    return count;
  }

  // Apply function to all slots in frame.
  apply(func) {
    let slots = this.slots;
    if (slots) {
      for (let n = 0; n < slots.length; n += 2) {
        let ret = func(slots[n], slots[n + 1]);
        if (ret) {
          slots[n] = ret[0];
          slots[n + 1] = ret[1];
        }
      }
    }
  }

  // Remove slots from frame based on criteria.
  purge(func) {
    let slots = this.slots;
    if (!slots) return
    let i = 0;
    let j = 0;
    let l = slots.length;
    while (i < l) {
      if (func(slots[i], slots[i + 1])) {
        i += 2;
      } else {
        slots[j++] = slots[i++];
        slots[j++] = slots[i++];
      }
    }
    slots.length = j;
  }

  // Convert frame to human-readable representation.
  text(pretty, anon) {
    let printer = new Printer(this.store);
    if (pretty) printer.indent = "  ";
    if (anon) printer.refs = null;
    printer.print(this);
    return printer.output;
  }

  // Convert frame to string.
  toString() {
    return this.text();
  }

  // Binary encoding of frame.
  encode() {
    let encoder = new Encoder(this.store);
    encoder.encode(this);
    return encoder.output();
  }

  // Iterator over all slots.
  [Symbol.iterator]() {
    let it = function* (slots, name) {
      if (slots) {
        for (let pos = 0; pos < slots.length; pos += 2) {
          yield [slots[pos], slots[pos + 1]];
        }
      }
    };
    return it(this.slots);
  }

  // Iterator over all slots with name.
  all(name) {
    if (typeof name === 'string') name = this.store.lookup(name);
    let it = function* (slots, name) {
      if (slots) {
        for (let pos = 0; pos < slots.length; pos += 2) {
          if (slots[pos] === name) yield slots[pos + 1];
        }
      }
    };
    return it(this.slots, name);
  }

  // Resolve frame by following is: chain.
  resolve() {
    return this.store.resolve(this);
  }

  // Get resolved link.
  link() {
    let link = this.get(this.store.is);
    if (typeof link === 'string') link = this.store.lookup(link);
    return link;
  }

  // Get all resolved links.
  links(self = false) {
    let it = function* (store, self, slots) {
      if (self) yield self;
      if (slots) {
        for (let pos = 0; pos < slots.length; pos += 2) {
          if (slots[pos] === store.is) {
            let link = slots[pos + 1];
            if (typeof link === 'string') link = store.lookup(link);
            yield link;
          }
        }
      }
    };
    return it(this.store, self ? this : null, this.slots);
  }

  // Assign id to frame.
  assign(id) {
    this.slots.unshift(this.store.id, id);
    this.store.register(id, this);
  }
}

// Qualified string.
export class QString {
  // Create string with qualifier.
  constructor(text, qual) {
    this.text = text;
    this.qual = qual;
  }

  // Use text part of qstring as string representation.
  toString() {
    return this.text;
  }

  // Convert qstring to SLING format.
  stringify(store) {
    let printer = new Printer(store);
    printer.print(this);
    return printer.output;
  }
};

// Binary SLING decoder.
export class Decoder {
  // Initialize decoder.
  constructor(store, data, marker = true) {
    this.store = store ? store : new Store();
    this.input = new Uint8Array(data);
    this.pos = 0;
    this.refs = [];

    // Skip binary marker.
    if (marker && this.input.length > 0 && this.input[0] == 0) this.pos = 1;
  }

  // Read 32-bit varint from input.
  read_varint32() {
    var result = 0, shift = 0, b;
    do {
      b = this.input[this.pos++];
      result += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80);
    if (shift > 32) throw "Invalid varint32";
    return result;
  }

  // Read 64-bit varint from input (only 53-bits can be represented accurately).
  read_varint64() {
    // Low bits (0-27).
    var lo = 0, shift = 0, b;
    do {
      b = this.input[this.pos++];
      lo += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80 && shift < 28);
    if (b < 0x80) return lo;

    // High bits (28-63).
    var hi = 0;
    shift = 0;
    do {
      b = this.input[this.pos++];
      hi += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80);
    return lo + hi * 268435456;
  }

  // Read UTF-8 encoded string from input.
  read_string(size) {
    let buffer = this.input.slice(this.pos, this.pos + size);
    this.pos += size;
    return strdecoder.decode(buffer);
  }

  // Read variable-length string from input.
  read_varstring() {
    let size = this.read_varint32();
    return this.read_string(size);
  }

  done() {
    return this.pos == this.input.length;
  }

  // Read all the objects from the input.
  readall() {
    let obj = this.read();
    while (!this.done()) obj = this.read();
    return obj;
  }

  // Read one object from the input.
  read() {
    // Read next tag.
    let tag = this.read_varint64();
    let op = (tag & 7) | 0;
    let arg = (tag / 8) | 0;
    let object;
    switch (op) {
      case 0:
        // REF.
        object = this.refs[arg];
        if (object == undefined) throw "Invalid reference";
        break;

      case 1:
        // FRAME.
        object = this.read_frame(arg, -1);
        break;

      case 2:
        // STRING.
        object = this.read_string(arg);
        this.refs.push(object);
        break;

      case 3:
        // SYMBOL.
        object = this.read_symbol(arg);
        this.refs.push(object);
        break;

      case 4:
        // LINK.
        object = this.read_link(arg);
        this.refs.push(object);
        break;

      case 5:
        // INTEGER.
        object = (arg << 2) >> 2;  // "sign-extend" 30-bit integer
        break;

      case 6:
        // FLOAT.
        object = bits_to_float(arg << 2);
        break;

      case 7:
        // SPECIAL.
        switch (arg) {
          case 1: object = null; break;
          case 2: object = this.store.id; break;
          case 3: object = this.store.isa; break;
          case 4: object = this.store.is; break;
          case 9: object = this.store.name; break;

          case 5:
            // ARRAY.
            object = this.read_array();
            break;

          case 6:
            // INDEX.
            object = bits_to_float((this.read_varint32() << 2) | 0xffc00003)
            break;

          case 7:
            // RESOLVE.
            let slots = this.read_varint32();
            let replace = this.read_varint32();
            object = this.read_frame(slots, replace);
            break;

          case 8:
            // QSTRING.
            object = this.read_qstring(arg);
            break;
        }
        break;
    }

    return object;
  }

  // Read frame from from input.
  read_frame(size, replace) {
    // Make new frame or replace existing frame.
    let frame;
    let refidx = this.refs.length;
    if (replace == -1) {
      // Create new frame.
      frame = new Frame(this.store);
      this.refs.push(frame);
    } else {
      // Replace exising frame.
      frame = this.refs[replace];
      if (frame === undefined) throw "Invalid replacement reference";
    }

    // Read all the frame slots.
    let slots = new Array(size * 2);
    for (let n = 0; n < size; ++n) {
      // Read key and value.
      let name = this.read();
      let value = this.read();

      // Fill frame slot.
      slots[n * 2] = name;
      slots[n * 2 + 1] = value;

      // Register frame for id: slots.
      if (name === this.store.id) {
        let existing = this.store.find(value);
        if (existing) {
          // Replace existing proxy/stub.
          existing.state = PUBLIC;
          this.refs[refidx] = existing;
          frame = existing;
        } else {
          // Register new frame.
          this.store.register(value, frame);
        }
      }
    }

    // Assign slots to frame.
    frame.slots = slots;

    return frame;
  }

  // Read array from from input.
  read_array() {
    // Get array size.
    let size = this.read_varint32();

    // Allocate array.
    let array = new Array(size);
    this.refs.push(array);

    // Read array elements.
    for (let i = 0; i < size; ++i) {
      array[i] = this.read();
    }

    return array;
  }

  // Read qstring from input.
  read_qstring() {
    // Get string size.
    let size = this.read_varint32();

    // Read string.
    let text = this.read_string(size);
    let qstr = new QString(text);
    this.refs.push(qstr);

    // Read qualifier.
    qstr.qual = this.read();
    return qstr;
  }

  // Read symbol from from input.
  read_symbol(size) {
    return this.read_string(size);
  }

  // Read link from input.
  read_link(size) {
    return this.store.lookup(this.read_symbol(size));
  }
}

// Object encoding status.
const Status = Object.freeze({
  // Object has not been encoded in the output.
  UNRESOLVED: Symbol("UNRESOLVED"),
  // Only a link to the object has been encoded in the output.
  LINKED: Symbol("LINKED"),
  // Object has been encoded in the output.
  ENCODED: Symbol("ENCODED"),
  // Object has been encoded as a string in the output.
  STRING: Symbol("STRING"),
});

// Binary SLING decoder.
export class Encoder {
  constructor(store, marker = true) {
    // Allocate output buffer.
    this.buffer = new Uint8Array(4096);
    this.capacity = this.buffer.byteLength;
    this.pos = 0;

    // Insert special values in reference mapping.
    this.refs = new Map();
    this.next = 0;
    this.id = store.id;
    this.refs.set(null, {status: Status.ENCODED, index: -1});
    this.refs.set(store.id, {status: Status.ENCODED, index: -2});
    this.refs.set(store.isa, {status: Status.ENCODED, index: -3});
    this.refs.set(store.is, {status: Status.ENCODED, index: -4});
    this.refs.set(store.name, {status: Status.ENCODED, index: -9});

    // Output binary encoding mark.
    if (marker) this.write_byte(0);
  }

  // Encode object.
  encode(obj) {
    if (typeof obj === 'number') {
      if (Number.isInteger(obj)) {
        this.write_tag(5, obj & 0x3FFFFFFF);
      } else {
        this.write_tag(6, float_to_bits(obj) >>> 2);
      }
    } else if (typeof obj === 'boolean') {
      this.write_tag(5, obj ? 1 : 0);
    } else if (obj === null) {
      this.write_tag(7, 1);
    } else {
      let ref = this.refs.get(obj);
      if (!ref) {
        ref = {status: Status.UNRESOLVED, index: 0};
        this.refs.set(obj, ref);
      }

      if (typeof obj === 'string') {
        if (ref.status == Status.STRING) {
          this.encode_ref(ref);
        } else {
          ref.status == Status.STRING
          ref.index = this.next++;
          this.encode_string(obj, 2);
        }
      } else if (obj instanceof Frame) {
        if (ref.status == Status.ENCODED) {
          this.encode_ref(ref);
        } else if (ref.status == Status.LINKED) {
          // A link to this frame has already been encoded.
          if (obj.state == PROXY) {
            this.encode_ref(ref);
          } else {
            // Encode a resolved frame which points back to the link reference.
            ref.status = Status.ENCODED;
            this.write_tag(7, 7);
            this.write_varint(obj.length);
            this.write_varint(ref.index);
            this.encode_slots(obj.slots);
          }
        } else if (obj.state == PROXY) {
            // Output SYMBOL for the proxy.
            ref.status = Status.LINKED;
            ref.index = this.next++;
            this.encode_string(obj.id, 4);
        } else {
          // Output frame slots.
          ref.status = Status.ENCODED;
          ref.index = this.next++;
          this.write_tag(1, obj.length);
          this.encode_slots(obj.slots);
        }
      } else if (obj instanceof Array) {
        // Output array tag followed by array size and the elements.
        ref.status = Status.ENCODED;
        ref.index = this.next++;
        this.write_tag(7, 5);
        this.write_varint(obj.length);
        for (let n = 0; n < obj.length; ++n) {
          this.encode_link(obj[n]);
        }
      } else if (obj instanceof QString) {
        if (ref.status == Status.STRING) {
          this.encode_ref(ref);
        } else {
          ref.status == Status.STRING
          ref.index = this.next++;
          this.encode_qstring(obj);
        }
      } else {
        throw "Object type cannot be encoded";
      }
    }
  }

  // Encode frame slots.
  encode_slots(slots) {
    for (let n = 0; n < slots.length; n += 2) {
      let name = slots[n];
      let value = slots[n + 1];
      this.encode_link(name);
      if (name === this.id) {
        // Skip symbol reference since the id value is just a string and not a
        // separate symbol object in this implementation.
        this.next++;
        this.encode_string(value, 3);
      } else {
        this.encode_link(value);
      }
    }
  }

  // Encode link.
  encode_link(link) {
    // Only output link to public frames.
    if (link instanceof Frame) {
      if (link.state != ANONYMOUS) {
        // Just output link to public frame.
        let ref = this.refs.get(link);
        if (ref) {
          this.encode_ref(ref);
        } else {
          // Encode LINK.
          ref = {status: Status.LINKED, index: this.next++};
          this.refs.set(link, ref);
          this.encode_string(link.id, 4);
        }
        return;
      }
    }
    this.encode(link);
  }

  // Encode string with length.
  encode_string(str, type) {
    let utf8 = strencoder.encode(str);
    let len = utf8.byteLength;
    this.write_tag(type, len);
    this.ensure(len);
    this.buffer.subarray(this.pos, this.pos + len).set(utf8);
    this.pos += len;
  }

  // Encode qualified string.
  encode_qstring(str) {
    let utf8 = strencoder.encode(str.text);
    let len = utf8.byteLength;
    this.write_tag(7, 8);
    this.write_varint(len);
    this.ensure(len);
    this.buffer.subarray(this.pos, this.pos + len).set(utf8);
    this.pos += len;
    this.encode_link(str.qual);
  }

  // Encode reference to previous object.
  encode_ref(ref) {
    if (ref.index < 0) {
      // Special handles are stored with negative reference numbers.
      this.write_tag(7, -ref.index);
    } else {
      // Output reference to previous object.
      this.write_tag(0, ref.index);
    }
  }

  // Ensure that there is room for n more bytes in the output buffer.
  ensure(n) {
    let minsize = this.pos + n;
    if (minsize > this.capacity) {
      let cap = this.capacity * 2;
      while (cap < minsize) cap *= 2;
      let newbuf = new Uint8Array(cap);
      newbuf.set(this.buffer);
      this.buffer = newbuf;
      this.capacity = cap;
    }
  }

  // Return output buffer.
  output() {
    return this.buffer.subarray(0, this.pos);
  }

  // Write a single byte to output.
  write_byte(byte) {
    this.ensure(1);
    this.buffer[this.pos++] = byte;
  }

  // Write varint-encoded integer to output.
  write_varint(num) {
    this.ensure(8);
    while (num >= 0x80000000) {
      this.buffer[this.pos++] = (num & 0x7F) | 0x80;
      num /= 128;
    }
    while (num & ~0x7F) {
      this.buffer[this.pos++] = (num & 0x7F) | 0x80;
      num >>>= 7;
    }
    this.buffer[this.pos++] = num | 0;
  }

  // Write variable-length string to output.
  write_varstring(str) {
    let utf8 = strencoder.encode(str);
    let len = utf8.byteLength;
    this.write_varint(len);
    this.ensure(len);
    this.buffer.subarray(this.pos, this.pos + len).set(utf8);
    this.pos += len;
  }

  // Write varint-encoded tag and argument to output.
  write_tag(tag, arg) {
    if (arg > 0x0FFFFFFF) {
      this.write_varint(tag + (arg * 8));
    } else {
      this.write_varint(tag | (arg << 3));
    }
  }

  // Write binary blob to output.
  write_blob(data) {
    let len = data.byteLength;
    this.ensure(len);
    this.buffer.subarray(this.pos, this.pos + len).set(data);
    this.pos += len;
  }
}

// Keywords.
const keywords = new Map([
  ["null",  -2],
  ["nil",   -2],
  ["false", -3],
  ["true",  -4],
]);

// Token types:
//  >0: ASCII code for character token
//  -1: end of input
//  -2: nil
//  -3: false
//  -4: true
//  -5: string
//  -6: number
//  -7: index
//  -8: symbol

// Read objects in text format and convert these to the internal object format.
export class Reader {
  // Initialize reader.
  constructor(store, data, bare) {
    this.store = store ? store : new Store();
    if (data instanceof ArrayBuffer) data = strdecoder.decode(data);
    this.input = data.toString();
    this.size = this.input.length;
    this.pos = 0;
    this.ch = -1;
    this.token = 0;
    this.value = null;
    this.refs = [];
    this.read();
    if (!bare) this.next();
  }

  // Get next character in input.
  read() {
    if (this.pos < this.size) {
      this.ch = this.input.charCodeAt(this.pos++);
    } else {
      this.ch = -1;
    }
  }

  // Check that all input has been read.
  done() {
    return this.token == -1;
  }

  // Check if all input has been read.
  end() {
    return this.ch == -1;
  }

  // Return context around current position.
  context(size = 20) {
    let start = Math.max(this.pos - size, 0);
    let end = Math.min(this.pos + size, this.size);
    return this.input.slice(start, end);
  }

  // Read next token from input.
  next() {
    // Keep reading until we either read a token or reach the end of the input.
    this.value = null;
    for (;;) {
      // Skip whitespace.
      while (this.ch == 32 ||  // space
             this.ch == 9 ||   // tab
             this.ch == 13 ||  // return
             this.ch == 10) {  // newline
        this.read();
      }

      // Check for end of input.
      if (this.end()) {
        this.token = -1;
        return;
      }

      // Parse next token (or comment).
      switch (this.ch) {
        case 58: case 43:    // ':' '+'
        case 44: case 61:    // ',' '='
        case 91: case 93:    // '[' ']'
        case 123: case 125:  // '{' '}'
        case 64:             // @
          this.token = this.ch;
          this.read();
          return;

        case 34:  // '"'
          // Parse string.
          this.parse_string();
          return;

        case 48: case 49: case 50: case 51: case 52:  // '0' '1' '2' '3' '4'
        case 53: case 54: case 55: case 56: case 57:  // '5' '6' '7' '8' '9'
        case 46: case 45:  // '.' '-'
          // Parse number.
          this.parse_number();
          return;

        case 35:  // '#'
          // Parse index reference.
          this.read();
          this.parse_index();
          return;

        case 59:  // ';'
          // Skip comment.
          this.read();
          while (this.ch != -1 && this.ch != 10) this.read();
          break;

        case 36:  // '$'
          // Quoted symbol.
          this.read();
          this.parse_string();
          this.token = -8;
          return;

        default:
          // Parse keyword or symbol.
          this.parse_name();
          return;
      }
    }
  }

  // Parse string literal.
  parse_string() {
    let start = this.pos;
    this.read();
    while (this.ch != -1 && this.ch != 34) {  // '"'
      if (this.ch == 92) this.read();  // '\'
      this.read();
    }
    if (this.end()) throw "Unterminated string";
    this.read();
    this.value = JSON.parse(this.input.slice(start - 1, this.pos - 1));
    this.token = -5;
  }

  // Parse number literal.
  parse_number() {
    let start = this.pos - 1;
    let integer = true;

    // Parse sign.
    if (this.ch == 45) this.read();  // '-'

    // Parse integer part.
    while (this.ch >= 48 && this.ch <= 57) this.read();

    // Parse decimal part.
    if (this.ch == 46) {  // '.'
      integer = false;
      this.read();
      while (this.ch >= 48 && this.ch <= 57) this.read();
    }

    // Parse exponent part.
    if (this.ch == 69 || this.ch == 101) {  // 'e' 'E'
      integer = false;
      this.read();
      if (this.ch == 43 || this.ch == 45) this.read();  // '+' '-'
      while (this.ch >= 48 && this.ch <= 57) this.read();
    }

    let end = this.end() ? this.pos : this.pos - 1;
    let str = this.input.slice(start, end);
    this.value = integer ? parseInt(str) : parseFloat(str);
    this.token = -6;
  }

  // Parse symbol index.
  parse_index() {
    let start = this.pos - 1;
    while (this.ch >= 48 && this.ch <= 57) this.read();
    let end = this.end() ? this.pos : this.pos - 1;
    this.value = parseInt(this.input.slice(start, end));
    this.token = -7;
  }

  // Parse symbol or keyword.
  parse_name() {
    let start = this.pos - 1;
    let end = this.pos;
    let done = false;
    while (!done) {
      switch (this.ch) {
        case -1:
          done = true;
          end = this.pos;
          break;

        case 95:  // '_'
        case 47:  // '/'
        case 45:  // '-'
          end = this.pos;
          this.read();
          break;

        default:
          if ((this.ch >= 48 && this.ch <= 57) ||   // 0-9
              (this.ch >= 65 && this.ch <= 90) ||   // A-Z
              (this.ch >= 97 && this.ch <= 122) ||  // a-z
              this.ch >= 128) {                     // unicode
            end = this.pos;
            this.read();
          } else {
            done = true;
          }
      }
    }
    this.value = this.input.slice(start, end);
    let kw = keywords.get(this.value);
    if (kw) {
      this.token = kw;
    } else {
      this.token = -8;
    }
  }

  // Parse all input and return last object.
  parseall() {
    let obj = this.parse();
    while (!this.done()) obj = this.parse();
    return obj;
  }

  // Parse next object.
  parse() {
    switch (this.token) {
      case -1:  // end of input
        return undefined;

      case -2:  // nil
        this.next();
        return null;

      case -3:  // false
        this.next();
        return false;

      case -4:  // true
        this.next();
        return true;

      case -5:  // string
        let str = this.value;
        this.next();
        if (this.token == 64) {
          this.next();
          let qual = this.parse();
          str = new QString(str, qual);
        }
        return str;

      case -6:  // number
        let num = this.value;
        this.next();
        return num;

      case -7:  // index
        let obj = this.refs[this.value];
        if (!obj) {
          obj = new Frame(this.store);
          this.refs[this.value] = obj;
        }
        this.next();
        return obj;

      case -8:  // symbol
        let sym = this.store.lookup(this.value);
        this.next();
        return sym;

      case 123: // '{'
        this.next();  // skip open bracket
        let frame = this.parse_frame();
        this.next();  // skip close bracket
        return frame;

      case 91: // '['
        return this.parse_array();

      default:
        throw "syntax error";
    }
  }

  // Parse frame.
  parse_frame() {
    // Parse slots.
    let slots = new Array();
    let frame = null;
    let index = -1;
    while (this.token != 125) {  // '}'
      switch (this.token) {
        case -1:
          throw "syntax error";

        case 61:  // '='
          this.next();
          if (this.token == -7) {
            if (!frame) frame = this.refs[this.value];
            index = this.value;
          } else if (this.token == -8) {
            if (!frame) frame = this.store.frames.get(this.value);
            slots.push(this.store.id);
            slots.push(this.value);
          } else {
            throw "frame id expected";
          }
          this.next();
          break;

        case 58:  // ':'
          this.next();
          slots.push(this.store.isa);
          slots.push(this.parse());
          break;

        case 43:  // '+'
          this.next();
          slots.push(this.store.is);
          slots.push(this.parse());
          break;

        default:
          slots.push(this.parse());
          if (this.token != 58) throw "missing colon";
          this.next();
          slots.push(this.parse());
      }

      // Skip commas between slots.
      if (this.token == 44) this.next();
    }

    // Add frame to store.
    if (frame == null) {
      frame = this.store.frame(slots);
      if (index != -1) this.refs[index] = frame;
    } else {
      frame.slots = slots;
      this.store.add(frame);
    }

    // Return parsed frame.
    return frame;
  }

  // Parse array.
  parse_array() {
    // Skip open bracket.
    this.next();

    // Allocate array.
    let array = new Array();

    // Read array elements.
    while (this.token != 93) {  // ']'
      array.push(this.parse());
      if (this.token == 44) this.next();
    }

    // Skip close bracket.
    this.next();

    return array;
  }
}

// Output objects in human-readable text format which can be read by a reader.
export class Printer {
  constructor(store) {
    this.store = store;
    this.output = "";
    this.refs = new Map();
    this.indent = null;
    this.level = 0;
    this.nextidx = 1;
  }

  // Write to output.
  write(str) {
    this.output += str;
  }

  // Print object to output.
  print(obj) {
    if (obj == null) {
      this.write("nil");
    } else if (obj instanceof Frame) {
      this.print_frame(obj);
    } else if (typeof obj === 'string') {
      this.write(JSON.stringify(obj));
    } else if (Array.isArray(obj)) {
      this.print_array(obj);
    } else if (obj instanceof QString) {
      this.write(JSON.stringify(obj.text));
      if (obj.qual) {
        this.write("@");
        this.level++;
        this.print(obj.qual);
        this.level--;
      }
    } else {
      // Number expected.
      this.write(obj.toString());
    }
  }

  // Print frame or reference.
  print_frame(frame) {
    // Output reference for nested frames.
    if (this.level > 0 && frame.state != ANONYMOUS) {
      this.print_symbol(frame.id);
      return;
    }

    // If frame has already been printed, only print a reference.
    let ref = this.refs && this.refs.get(frame);
    if (ref) {
      if (typeof ref === 'string') {
        // Public reference.
        this.print_symbol(ref);
      } else {
        // Local reference.
        this.write("#");
        this.write(ref.toString());
      }
      return;
    }

    // Increase indentation for nested frames.
    this.write("{");
    this.level++;

    // Add frame to set of printed references.
    if (this.refs) {
      if (frame.state == ANONYMOUS) {
        // Assign next local id for anonymous frame.
        let id = this.nextidx++;
        this.write("=#");
        this.write(id.toString());
        this.write(" ");
        this.refs.set(frame, id);
      } else {
        // Update reference table with frame id.
        this.refs.set(frame, frame.id);
      }
    }

    // Print slots.
    let slots = frame.slots;
    if (slots) {
      for (let n = 0; n < slots.length; n += 2) {
        // Indent.
        if (this.indent) {
          this.write("\n");
          for (let l = 0; l < this.level; ++l) this.write(this.indent);
        } else if (n > 0) {
          this.write(" ");
        }

        // Output slot.
        let name = slots[n];
        let value = slots[n + 1];
        if (name == this.store.id) {
          this.write("=");
          this.print_symbol(value);
          if (this.refs) this.refs.set(frame, value);
        } else if (name === this.store.isa) {
          this.write(":");
          this.print(value);
        } else if (name === this.store.is) {
          this.write("+");
          this.print(value);
        } else {
          this.print(name);
          this.write(this.indent ? ": " : ":");
          this.print(value);
        }
      }
    }

    this.level--;
    if (this.indent) {
      // Restore indentation.
      this.write("\n");
      for (let l = 0; l < this.level; ++l) this.write(this.indent);
    }
    this.write("}");
  }

  // Print array.
  print_array(array) {
    this.write("[");
    for (let i = 0; i < array.length; ++i) {
      if (i != 0) this.write(this.indent ? ", " : ",");
      this.print(array[i]);
    }
    this.write("]");
  }

  // Print symbol.
  print_symbol(symbol) {
    if (namepat.test(symbol)) {
      this.write(symbol);
    } else {
      this.write("$");
      this.write(JSON.stringify(symbol));
    }
  }

  // Return output and reset output buffer.
  flush_output() {
    let out = this.output;
    this.output = "";
    return out;
  }
}

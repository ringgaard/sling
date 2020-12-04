// Copyright 2020 Ringgaard Research ApS
// Licensed under the Apache License, Version 2

// SLING frame store implementation for JavaScript.

var strdecoder = new TextDecoder("utf8");
var strencoder = new TextEncoder();
var numbuf = new ArrayBuffer(4);
var intbuf = new Int32Array(numbuf);
var fltbuf = new Float32Array(numbuf);

// Convert binary 32-bit IEEE-754 float to number.
function bitsToFloat(bits) {
  intbuf[0] = bits;
  return fltbuf[0];
}

// Convert number to binary 32-bit IEEE-754 float.
function FloatToBits(num) {
  fltbuf[0] = num;
  return intbuf[0];
}

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
    } else {
      this.id = new Frame(this, []);
      this.id.slots = [this.id, "id"];
      this.isa = new Frame(this, [this.id, "isa"]);
      this.is = new Frame(this, [this.id, "is"]);
      this.add(this.id);
      this.add(this.isa);
      this.add(this.is);
    }
  }

  // Register frame under id in store.
  register(id, frame) {
    this.frames.set(id, frame);
    frame.anonymous = false;
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
    let f = new Frame(this, slots);
    this.add(f);
    return f;
  }

  // Create proxy frame and add it to the store.
  proxy(id) {
    let proxy = new Frame(this);
    proxy.proxy = true;
    proxy.slots = [this.id, id];
    this.register(id, proxy);
    return proxy;
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

  // Parse input data and return first object.
  parse(data) {
    if (data instanceof Response) {
      return data.arrayBuffer().then(data => this.parse(data));
    }

    let decoder = new Decoder(this, data);
    return decoder.readAll();
  }
}

// A frame has a list of (name,value) slots.
export class Frame {
  // Create new frame with slots.
  constructor(store, slots) {
    this.store = store;
    this.slots = slots ? Array.from(slots) : null;
    this.proxy = false;
    this.anonymous = true;
  }

  // Return (first) value for frame slot.
  get(name) {
    for (let n = 0; n < this.slots.length; n += 2) {
      if (this.slots[n] === name) return this.slots[n + 1];
    }
    return undefined;
  }

  // Return id for frame.
  get id() {
    if (this.anonymous) return undefined;
    return this.get(this.store.id);
  }

  // Add slot to frame.
  add(name, value) {
    this.slots.push(name);
    this.slots.push(value);
  }

  // Return number of slots for frame.
  get length() {
    return this.slots.length / 2;
  }

  // Return name for nth slot.
  name(n) {
    return this.slots[n * 2];
  }

  // Return value for nth slot.
  value(n) {
    return this.slots[n * 2 + 1];
  }

  // Convert frame to human-readable representation.
  text(pretty) {
    let printer = new Printer(this.store);
    if (pretty) printer.indent = "  ";
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
    let pos = 0;
    let slots = this.slots;
    return {
      next() {
        if (pos == slots.length) return { value: undefined, done: true};
        let name = slots[pos++];
        let value = slots[pos++];
        return { value: [name, value], done: false };
      }
    };
  }

  // Iterator over all slots with name.
  all(name) {
    if (typeof name === 'string') name = this.store.lookup(name);
    let slots = this.slots;
    let it = function* () {
      for (let pos = 0; pos < slots.length; pos += 2) {
        if (slots[pos] === name) yield slots[pos + 1];
        pos += 2;
      }
    };
    return it();
  }

  // Resolve frame by following is: chain.
  resolve() {
    const is = this.store.is;
    let that = this;
    while (true) {
      let next = this.get(is);
      if (!(next instanceof Frame)) return that;
      that = next;
    }
  }
}

// Binary SLING decoder.
class Decoder {
  // Initialize decoder.
  constructor(store, data) {
    this.store = store ? store : new Store();
    this.input = new Uint8Array(data);
    this.pos = 0;
    this.refs = [];

    // Skip binary marker.
    if (this.input.length > 0 && this.input[0] == 0) this.pos = 1;
  }

  // Read tag from input. The tag is encoded as a varint64 where the lower three
  // bits are the opcode and the upper bits are the argument.
  readTag() {
    // Low bits (0-27).
    var lo = 0, shift = 0, b;
    do {
      b = this.input[this.pos++];
      lo += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80 && shift < 28);
    if (b < 0x80) return [lo & 7, lo >>> 3];

    // High bits (28-63).
    var hi = 0;
    shift = 0;
    do {
      b = this.input[this.pos++];
      hi += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80);
    return [lo & 7, (lo >>> 3) | (hi << 25)];
  }

  // Read 32-bit varint from input.
  readVarint32() {
    var result = 0, shift = 0, b;
    do {
      b = this.input[this.pos++];
      result += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80);
    if (shift > 32) throw "Invalid varint32";
    return result;
  }

  // Read UTF-8 encoded string from input.
  readString(size) {
    let buffer = this.input.slice(this.pos, this.pos + size);
    this.pos += size;
    return strdecoder.decode(buffer);
  }

  // Read all the objects from the input.
  readAll() {
    let obj = this.read();
    while (this.pos < this.input.length) this.read();
    return obj;
  }

  // Read one object from the input.
  read() {
    // Read next tag.
    var [op, arg] = this.readTag();
    var object;
    switch (op) {
      case 0:
        // REF.
        object = this.refs[arg];
        if (object == undefined) throw "Invalid reference";
        break;

      case 1:
        // FRAME.
        object = this.readFrame(arg, -1);
        break;

      case 2:
        // STRING.
        object = this.readString(arg);
        this.refs.push(object);
        break;

      case 3:
        // SYMBOL.
        object = this.readSymbol(arg);
        this.refs.push(object);
        break;

      case 4:
        // LINK.
        object = this.readLink(arg);
        this.refs.push(object);
        break;

      case 5:
        // INTEGER.
        object = arg;
        break;

      case 6:
        // FLOAT.
        object = bitsToFloat(arg << 2);
        break;

      case 7:
        // SPECIAL.
        switch (arg) {
          case 1: object = null; break;
          case 2: object = this.store.id; break;
          case 3: object = this.store.isa; break;
          case 4: object = this.store.is; break;

          case 5:
            // ARRAY.
            object = this.readArray();
            break;

          case 6:
            // INDEX.
            object = bitsToFloat((this.readVarint32() << 2) | 0xffc00003)
            break;

          case 7:
            // RESOLVE.
            var slots = this.readVarint32();
            var replace = this.readVarint32();
            object = this.readFrame(slots, replace);
            break;
        }
        break;
    }

    return object;
  }

  // Read frame from from input.
  readFrame = function(size, replace) {
    // Make new frame or replace existing frame.
    var frame;
    if (replace == -1) {
      // Create new frame.
      frame = new Frame(this.store);
      this.refs.push(frame);
    } else {
      // Replace exising frame.
      frame = this.refs[replace];
      if (frame == undefined) throw "Invalid replacement reference";
    }

    // Read all the frame slots.
    let slots = new Array(size * 2);
    for (var n = 0; n < size; ++n) {
      // Read key and value.
      let name = this.read();
      let value = this.read();

      // Fill frame slot.
      slots[n * 2] = name;
      slots[n * 2 + 1] = value;

      // Register frame for id: slots.
      if (name === this.store.id) {
        this.store.register(value, frame);
      }
    }

    // Assign slots to frame.
    frame.slots = slots;
    frame.proxy = false;

    return frame;
  }

  // Read array from from input.
  readArray() {
    // Get array size.
    var size = this.readVarint32();

    // Allocate array.
    var array = new Array(size);
    this.refs.push(array);

    // Read array elements.
    for (var i = 0; i < size; ++i) {
      array[i] = this.read();
    }

    return array;
  }

  // Read symbol from from input.
  readSymbol(size) {
    return this.readString(size);
  }

  // Read link from input.
  readLink(size) {
    // Read symbol name.
    let symbol = this.readSymbol(size);

    // Lookup symbol in store.
    let link = this.store.lookup(symbol);

    // Create proxy for symbol if not found.
    if (link == undefined) link = this.store.proxy(symbol);

    return link;
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
class Encoder {
  constructor(store) {
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

    // Output binary encoding mark.
    this.writeByte(0);
  }

  // Encode object.
  encode(obj) {
    if (typeof obj === 'number') {
      // TODO: handle integer overlow.
      if (Number.isInteger(obj)) {
        this.writeTag(5, obj);
      } else {
        this.writeTag(6, FloatToBits(obj) >> 2);
      }
    } else {
      let ref = this.refs.get(obj);
      if (!ref) {
        ref = {status: Status.UNRESOLVED, index: 0};
        this.refs.set(obj, ref);
      }

      if (typeof obj === 'string') {
        if (ref.status == Status.STRING) {
          this.encodeRef(ref);
        } else {
          ref.status == Status.STRING
          ref.index = this.next++;
          this.encodeString(obj, 2);
        }
      } else if (obj instanceof Frame) {
        if (ref.status == Status.ENCODED) {
          this.encodeRef(ref);
        } else if (ref.status == Status.LINKED) {
          // A link to this frame has already been encoded.
          if (obj.proxy) {
            this.encodeRef(ref);
          } else {
            // Encode a resolved frame which points back to the link reference.
            ref.status = Status.ENCODED;
            this.writeTag(7, 7);
            this.writeVarInt(obj.length);
            this.writeVarInt(ref.index);
            this.encodeSlots(obj.slots);
          }
        } else if (obj.proxy) {
            // Output SYMBOL for the proxy.
            ref.status = Status.LINKED;
            ref.index = this.next++;
            this.encodeString(obj.id, 4);
        } else {
          // Output frame slots.
          ref.status = Status.ENCODED;
          ref.index = this.next++;
          this.writeTag(1, obj.length);
          this.encodeSlots(obj.slots);
        }
      } else if (obj instanceof Array) {
        // Output array tag followed by array size and the elements.
        ref.status = ENCODED;
        ref.index = this.next++;
        this.writeTag(7, 5);
        this.writeVarInt(obj.length);
        for (let n = 0; n < obj.length; ++n) {
          this.encodeLink(obj[n]);
        }
      } else {
        throw "Object type cannot be encoded";
      }
    }
  }

  // Encode frame slots.
  encodeSlots(slots) {
    for (let n = 0; n < slots.length; n += 2) {
      let name = slots[n];
      let value = slots[n + 1];
      this.encodeLink(name);
      if (name === this.id) {
        let ref = this.refs.get(value);
        if (ref && ref.status != Status.ENCODED) {
          this.encodeRef(ref);
        } else {
          // Encode SYMBOL.
          ref = {status: Status.ENCODED, index: this.next++};
          this.refs.set(value, ref);
          this.encodeString(value, 3);
        }
      } else {
        this.encodeLink(value);
      }
    }
  }

  // Encode link.
  encodeLink(link) {
    // Only output link to public frames.
    if (link instanceof Frame) {
      if (!link.anonymous) {
        // Just output link to public frame.
        let ref = this.refs.get(link);
        if (ref) {
          this.encodeRef(ref);
        } else {
          // Encode LINK.
          ref = {status: Status.LINKED, index: this.next++};
          this.refs.set(link, ref);
          this.encodeString(link.id, 4);
        }
        return;
      }
    }
    this.encode(link);
  }

  // Encode string with length.
  encodeString(str, type) {
    let utf8 = strencoder.encode(str);
    let len = utf8.byteLength;
    this.writeTag(type, len);
    this.ensure(len);
    this.buffer.subarray(this.pos, this.pos + len).set(utf8);
    this.pos += len;
  }

  // Encode reference to previous object.
  encodeRef(ref) {
    if (ref.index < 0) {
      // Special handles are stored with negative reference numbers.
      this.writeTag(7, -ref.index);
    } else {
      // Output reference to previous object.
      this.writeTag(0, ref.index);
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
  writeByte(byte) {
    this.ensure(1);
    this.buffer[this.pos++] = byte;
  }

  // Write varint-encoded integer to output.
  writeVarInt(num) {
    if (num > Number.MAX_SAFE_INTEGER) {
      throw new RangeError("Could not encode varint");
    }
    this.ensure(8);
    while(num >= 0x80000000) {
      this.buffer[this.pos++] = (num & 0xFF) | 0x80;
      num /= 128;
    }
    while(num & ~0x7F) {
      this.buffer[this.pos++] = (num & 0xFF) | 0x80;
      num >>>= 7;
    }
    this.buffer[this.pos++] = num | 0;
  }

  // Write varint-encoded tag and argument to output.
  writeTag(tag, arg) {
    this.writeVarInt(tag | (arg << 3));
  }
}

// Read objects in text format and convert these to the internal object format.
class Reader {
  // Initialize reader.
  constructor(store, data) {
    this.store = store ? store : new Store();
    this.input = data.toString();
    this.pos = 0;
    this.refs = [];
  }
}

// Output objects in human-readable text format which can be read by a reader.
class Printer {
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
      this.printFrame(obj);
    } else if (typeof obj === 'string') {
      this.write(JSON.stringify(obj));
    } else if (Array.isArray(obj)) {
      this.printArray(obj);
    } else {
      // Number expected.
      this.write(obj.toString());
    }
  }

  // Print frame or reference.
  printFrame(frame) {
    // Output reference for nested frames.
    if (this.level > 0 && !frame.anonymous) {
      this.printSymbol(frame.id);
      return;
    }

    // If frame has already been printed, only print a reference.
    let ref = this.refs.get(frame);
    if (ref) {
      if (typeof ref === 'string') {
        // Public reference.
        this.printSymbol(ref);
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
    if (frame.anonymous) {
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

    // Print slots.
    let slots = frame.slots;
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
        this.printSymbol(value);
        this.refs.set(frame, value);
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

    if (this.indent) {
      // Restore indentation.
      this.level--;
      this.write("\n");
      for (let l = 0; l < this.level; ++l) this.write(this.indent);
    }
    this.write("}");
  }

  // Print array.
  printArray(array) {
    this.write("[");
    for (let i = 0; i < array.length; ++i) {
      if (i != 0) this.write(this.indent ? ", " : ",");
      this.print(array[i]);
    }
    this.write("]");
  }

  // Print symbol.
  printSymbol(symbol) {
    // TODO: escape symbol names.
    this.write(symbol);
  }
}


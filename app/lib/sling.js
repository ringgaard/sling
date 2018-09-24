// SLING store implementation for Javascript.

var sling = sling || {};

// Convert binary 32-bit IEEE float to number.
sling.binaryToFloat = function(bin) {
  var buffer = new ArrayBuffer(4);
  var intbuf = new Int32Array(buffer);
  var fltbuf = new Float32Array(buffer);
  intbuf[0] = bin;
  return fltbuf[0];
}

// SLING store with symbol table and an optional link to the globals.
sling.Store = function(globals) {
  // Initialize store.
  this.globals = globals;
  this.symbols = {};
  this.nextid = globals ? globals.nextid : 1;

  // Initialize standard symbols.
  if (globals) {
    this.id = globals.id;
    this.isa = globals.isa;
    this.is = globals.is;
  } else {
    this.id = {id: "id"};
    this.isa = {id: "isa"};
    this.is = {id: "is"};
    this.symbols["id"] = this.id;
    this.symbols["isa"] = this.isa;
    this.symbols["is"] = this.is;
  }
}

// Look up symbol in store.
sling.Store.prototype.lookup = function(symbol) {
  return this.symbols[symbol];
}

// Find symbol in store. This also search the global store.
sling.Store.prototype.find = function(symbol) {
  var value = this.symbols[symbol];
  if (!value && this.globals) {
    value = this.globals.find(symbol);
  }
  return value;
}

// Add symbol to store.
sling.Store.prototype.add = function(symbol, value) {
  this.symbols[symbol] = value;
}

// Binary SLING decoder. Reads input from an array buffer into the store.
sling.Decoder = function(data, store) {
  // Initialize decoder.
  this.store = store ? store : new sling.Store();
  this.input = new Uint8Array(data);
  this.pos = 0;
  this.refs = [];
  this.strdecoder = new TextDecoder("utf8");
}

// Read tag from input. The tag is encoded as a varint64 where the lower three
// bits are the opcode and the upper bits are the argument.
sling.Decoder.prototype.readTag = function() {
  var lo = 0, hi = 0, shift  = 0, b;

  do {
    b = this.input[this.pos++];
    lo += (b & 0x7f) << shift;
    shift += 7;
  } while (b >= 0x80 && shift < 28);

  if (b >= 0x80) {
    lo += ((b & 0x0f) << 28);
    hi = (b & 0x7f) >> 3;
    shift = 3;
    do {
      b = this.input[this.pos++];
      hi += (b & 0x7f) << shift;
      shift += 7;
    } while (b >= 0x80);
    if (shift > 32) throw "Invalid tag";
  }

  return [lo & 7, (lo >>> 3) | (hi << 29)];
}

// Read 32-bit varint from input.
sling.Decoder.prototype.readVarint32 = function() {
  var result = 0, shift  = 0, b;

  do {
    b = this.input[this.pos++];
    result += (b & 0x7f) << shift;
    shift += 7;
  } while (b >= 0x80);
  if (shift > 32) throw "Invalid tag";

  return result;
}

// Read UTF-8 encoded string from input.
sling.Decoder.prototype.readString = function(size) {
  var buffer =  this.input.slice(this.pos, this.pos + size);
  this.pos += size;
  return this.strdecoder.decode(buffer);
}

// Read all the objects from the input and return the symbol table.
sling.Decoder.prototype.readAll = function() {
  while (this.pos < this.input.length) this.read();
  return this.store.symbols;
}

// Read one object from the input.
sling.Decoder.prototype.read = function() {
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
      object = binaryToFloat(arg << 2);
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
          object = binaryToFloat((this.readVarint32() << 2) | 0xffc00003)
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
sling.Decoder.prototype.readFrame = function(slots, replace) {
  // Make new frame or replace existing frame.
  var frame;
  if (replace == -1) {
    frame = {}
    this.refs.push(frame);
  } else {
    frame = this.refs[replace];
    if (frame == undefined) throw "Invalid replacement reference";
  }

  // Read all the frame slots.
  for (var i = 0; i < slots; ++i) {
    // Read key and value.
    var key = this.read();
    var value = this.read();

    // Add id to store symbol table.
    if (key == this.store.id) {
      // If symbol is already defined, it should be a proxy.
      var existing = this.store.find(value);
      if (existing && existing.id == value) {
        continue;
      } else {
        this.store.add(value, frame);
      }
    }

    // Add slot to frame.
    var v = frame[key.id];
    if (v == undefined) {
      // New role.
      frame[key.id] = value;
    } else if (v instanceof Array) {
      // Existing multi-value role.
      v.push(value);
    } else {
      // Create multi-value role.
      frame[key.id] = [v, value];
    }
  }

  return frame;
}

// Read array from from input.
sling.Decoder.prototype.readArray = function() {
  // Get array size.
  var size = this.readVarint32();

  // Allocate array.
  var array = [];
  array.length = size;
  this.refs.push(array);

  // Read array elements.
  for (var i = 0; i < size; ++i) array[i] = this.read();

  return array;
}

// Read symbol from from input.
sling.Decoder.prototype.readSymbol = function(size) {
  if (size == 0) {
    // Create new local id.
    var id = this.store.nextid++;
    return "#" + id.toString();
  } else {
    // Read symbol name.
    return this.readString(size);
  }
}

// Read link from input.
sling.Decoder.prototype.readLink = function(size) {
  // Read symbol name.
  var symbol = this.readSymbol(size);

  // Lookup symbol in symbol table. Create proxy for new symbol.
  var link = this.store.find(symbol);
  if (link == undefined) {
    link = {id: symbol};
    this.store.add(symbol, link);
  }

  return link;
}

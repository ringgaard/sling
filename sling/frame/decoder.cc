// Copyright 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sling/frame/decoder.h"

#include <string>

#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/frame/wire.h"
#include "sling/stream/input.h"

namespace sling {

Decoder::Decoder(Store *store, Input *input, bool marker)
    : store_(store), input_(input), references_(store), stack_(store) {
  // Skip binary encoding mark.
  if (marker && input->Peek() == WIRE_BINARY_MARKER) input->Skip(1);
}

Object Decoder::Decode() {
  return Object(store_, DecodeObject());
}

Object Decoder::DecodeAll() {
  Handle handle = Handle::nil();
  while (!done()) {
    handle = DecodeObject();
    if (handle.IsError()) break;
  }
  return Object(store_, handle);
}

Handle Decoder::DecodeObject() {
  // Decode next tag from input. The tag is a 64-bit varint where the lower
  // three bits are the tag and the upper bits are the argument.
  Handle handle;
  uint64 tag;
  if (!input_->ReadVarint64(&tag)) return Handle::error();
  uint64 arg = tag >> 3;

  // Decode different tag types.
  switch (tag & 7) {
    case WIRE_REF:
      // Return handle for reference to previous value.
      handle = Reference(arg);
      break;

    case WIRE_FRAME:
      handle = DecodeFrame(arg, -1);
      break;

    case WIRE_STRING:
      handle = DecodeString(arg);
      *references_.push() = handle;
      break;

    case WIRE_SYMBOL:
      handle = DecodeSymbol(arg);
      *references_.push() = handle;
      break;

    case WIRE_LINK:
      handle = DecodeLink(arg);
      *references_.push() = handle;
      break;

    case WIRE_INTEGER:
      // Integer value is encoded in the argument.
      handle = Handle::Integer(arg);
      break;

    case WIRE_FLOAT:
      // Float value is encoded in the argument as an bit pattern.
      handle = Handle::FromFloatBits(arg);
      break;

    case WIRE_SPECIAL:
      switch (arg) {
        case WIRE_NIL: handle = Handle::nil(); break;
        case WIRE_ID: handle = Handle::id(); break;
        case WIRE_ISA: handle = Handle::isa(); break;
        case WIRE_IS: handle = Handle::is(); break;
        case WIRE_ARRAY:
          handle = DecodeArray();
          break;
        case WIRE_INDEX: {
          uint32 index;
          if (!input_->ReadVarint32(&index)) return Handle::error();
          handle = Handle::Index(index);
          break;
        }
        case WIRE_RESOLVE: {
          uint32 slots;
          uint32 replace;
          if (!input_->ReadVarint32(&slots)) return Handle::error();
          if (!input_->ReadVarint32(&replace)) return Handle::error();
          if (replace >= references_.length()) return Handle::error();
          handle = DecodeFrame(slots, replace);
          break;
        }
        case WIRE_QSTRING:
          handle = DecodeQString();
          break;
        default:
          handle = Handle::error();
      }
  }

  // Return handle for object.
  return handle;
}

Handle Decoder::DecodeFrame(int slots, int replace) {
  // Pre-allocate frame unless we are resolving a link.
  Handle handle;
  int index;
  if (replace == -1) {
    handle = store_->AllocateFrame(slots);
    index = references_.length();
    *references_.push() = handle;
  } else {
    handle = Reference(replace);
    if (handle.IsError()) return Handle::error();
    index = replace;
  }

  // Decode slots for frame and store them temporarily on the stack.
  Word mark = Mark();
  for (int i = 0; i < slots; ++i) {
    // Read slot name and value.
    Handle name = DecodeObject();
    if (name.IsError()) return Handle::error();
    Push(name);
    Handle value = DecodeObject();
    if (value.IsError()) return Handle::error();
    Push(value);

    if (name.IsId() && replace == -1) {
      // The value of the id slot must be a symbol.
      if (value.IsNil()) return Handle::error();
      if (!value.IsRef()) return Handle::error();
      Datum *id = store_->Deref(value);
      if (!id->IsSymbol()) return Handle::error();
      SymbolDatum *symbol = id->AsSymbol();

      if (!store_->Owned(value)) {
        // The symbol is not owned, so we replace it by a local symbol.
        SymbolDatum *local = store_->LocalSymbol(symbol);
        ReplaceTop(local->self);
      } else if (symbol->bound()) {
        // Check if there is already a proxy for the id. In that case we have to
        // replace the proxy with the new frame.
        Datum *existing = store_->Deref(symbol->value);
        if (existing->IsProxy()) {
          // Swap the handle for the existing proxy and the new frame.
          FrameDatum *frame = store_->Deref(handle)->AsFrame();
          store_->ReplaceProxy(existing->AsProxy(), frame);
          handle = frame->self;

          // Update the handle in the reference table.
          *(references_.base() + index) = handle;

          // Unbind the symbol. It will be bound to the frame later.
          symbol->value = Handle::nil();
        }
      }
    }
  }

  // Check if frame is already known.
  Slot *begin =  reinterpret_cast<Slot *>(stack_.address(mark));
  Slot *end =  reinterpret_cast<Slot *>(stack_.end());
  if (skip_known_frames_) {
    for (Slot *s = begin; s < end; ++s) {
      // Find id slot where value is a symbol for an existing frame.
      if (s->name != Handle::id()) continue;
      if (s->value.IsNil()) continue;
      if (!s->value.IsRef()) continue;
      Datum *datum = store_->Deref(s->value);
      if (!datum->IsSymbol()) continue;
      SymbolDatum *symbol = datum->AsSymbol();
      if (symbol->unbound()) continue;
      FrameDatum *frame = store_->Deref(symbol->value)->AsFrame();
      if (frame->IsProxy()) continue;

      // Frame already exists.
      Release(mark);
      references_.base()[index] = frame->self;

      return frame->self;
    }
  }

  // Update or create frame.
  if (replace == -1) {
    store_->UpdateFrame(handle, begin, end);
  } else {
    handle = store_->AllocateFrame(begin, end, handle);
  }

  // Remove slots from stack.
  Release(mark);

  return handle;
}

Handle Decoder::DecodeString(int size) {
  // Allocate string object; argument is the size of the string.
  Handle handle = store_->AllocateString(size);

  // Read string from input.
  if (!input_->Read(store_->GetString(handle)->data(), size)) {
    return Handle::error();
  }

  return handle;
}

Handle Decoder::DecodeQString() {
  // Get string length.
  Word mark = Mark();
  uint32 length;
  if (!input_->ReadVarint32(&length)) return Handle::error();

  // Allocate string object.
  Handle str = store_->AllocateString(length, Handle::nil());
  Push(str);

  // Read string from input.
  if (!input_->Read(store_->GetString(str)->data(), length)) {
    return Handle::error();
  }

  // Add reference.
  *references_.push() = str;

  // Read qualifier from input.
  Handle qual = DecodeObject();
  if (qual.IsError()) return Handle::error();
  store_->GetString(str)->set_qualifier(qual);

  Release(mark);
  return str;
}

Handle Decoder::DecodeArray() {
  // Get array size.
  uint32 size;
  if (!input_->ReadVarint32(&size)) return Handle::error();

  // Allocate array.
  Handle handle = store_->AllocateArray(size);
  *references_.push() = handle;

  // Decode array elements and store them temporarily on the stack.
  Word mark = Mark();
  for (int i = 0; i < size; ++i) {
    Handle elem = DecodeObject();
    if (elem.IsError()) return Handle::error();
    Push(elem);
  }

  // Copy elements from stack to array.
  Handle *source =  stack_.address(mark);
  Handle *end =  stack_.end();
  ArrayDatum *array = store_->Deref(handle)->AsArray();
  Handle *dest = array->begin();
  while (source < end) *dest++ = *source++;

  // Remove elements from stack.
  Release(mark);

  return handle;
}

Handle Decoder::DecodeSymbol(int name_size) {
  // Read symbol name and resolve unbound symbol reference.
  const char *data;
  if (input_->TryRead(name_size, &data)) {
    // Fast case.
    return store_->Symbol(Text(data, name_size));
  } else {
    // Slow case.
    string name;
    if (!input_->ReadString(name_size, &name)) return Handle::error();
    return store_->Symbol(name);
  }
}

Handle Decoder::DecodeLink(int name_size) {
  // Read symbol name and resolve bound symbol reference.
  const char *data;
  if (input_->TryRead(name_size, &data)) {
    // Fast case.
    return store_->Lookup(Text(data, name_size));
  } else {
    // Slow case.
    string name;
    if (!input_->ReadString(name_size, &name)) return Handle::error();
    return store_->Lookup(name);
  }
}

}  // namespace sling


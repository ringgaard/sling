// Copyright 2019 Google Inc.
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

#include "sling/nlp/parser/transition-generator.h"

#include <vector>

#include "sling/frame/object.h"
#include "sling/frame/store.h"

namespace sling {
namespace nlp {

namespace {

// Edge in frame graph.
struct Edge {
  Edge(Handle s, Handle r, Handle t) : source(s), role(r), target(t) {}
  Handle source;
  Handle role;
  Handle target;
};

// Attention buffer for transition state.
class Attention {
 public:
  Attention(Store *store) : slots_(store) {}

  // Return the number of slots in attention buffer.
  int size() const { return slots_.size(); }

  // Return slot in attention buffer. The center of attention has index 0.
  Handle slot(int index) const {
    return slots_[size() - index - 1];
  }

  // Return the position in the attention buffer of a frame or -1 if the
  // frame is not in the attention buffer.
  int index(Handle frame) const {
    for (int i = 0; i < size(); ++i) {
      if (slot(i) == frame) return i;
    }
    return -1;
  }

  // Add frame to attention buffer.
  void add(Handle frame) {
    slots_.push_back(frame);
  }

  // Move frame to new position in attention buffer. Frames are always moved
  // towards the center of attention.
  void move(int index, int position) {
    if (index != position) {
      int start = size() - index - 1;
      int end = size() - position - 1;
      DCHECK_LE(index, position);
      Handle frame = slots_[start];
      for (int i = start; i < end; ++i) {
        slots_[i] = slots_[i + 1];
      }
      slots_[end] = frame;
    }
  }

 private:
  // Attention slots. This contains evoked frames in order of attention. The
  // last element is the center of attention.
  Handles slots_;
};

// Check for anonymous frame.
bool IsAnonymousFrame(Store *store, Handle handle) {
  if (!handle.IsRef() || handle.IsNil()) return false;
  Datum *datum = store->Deref(handle);
  if (!datum->IsFrame()) return false;
  return datum->AsFrame()->IsAnonymous();
}

}  // namespace

void Generate(const Document &document,
              int begin, int end,
              std::function<void(const ParserAction &)> callback) {
  Store *store = document.store();
  Handles evoked(store);
  Attention attention(store);
  std::vector<Edge> deferred;

  for (int t = begin; t < end; ++t) {
    // Emit MARK actions for all multi-token spans starting on this token.
    Span *span = document.GetSpanAt(t);
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->begin() == t && s->length() > 1) {
        s->AllEvoked(&evoked);
        for (int n = 0; n < evoked.size(); ++n) {
          callback(ParserAction::Mark());
        }
      }
    }

    // Emit EVOKE/REFER actions for all spans ending on this token.
    for (Span *s = span; s != nullptr; s = s->parent()) {
      if (s->end() != t + 1) continue;
      s->AllEvoked(&evoked);
      for (Handle h : evoked) {
        // Try to find frame in attention buffer.
        Frame frame(store, h);
        int index = attention.index(h);

        // A zero-length EVOKE/REFER uses the mark stack.
        int length = s->length();
        if (length > 1) length = 0;

        if (index != -1) {
          // Reference existing frame.
          callback(ParserAction::Refer(length, index));
          attention.move(index, 0);
        } else {
          // Evoke new frame.
          Handle type = frame.GetHandle(Handle::isa());
          callback(ParserAction::Evoke(length, type));
          attention.add(h);

          // Emit deferred CONNECTs.
          for (auto it = deferred.begin(); it != deferred.end(); ++it) {
            if (it->target == h) {
              int source = attention.index(it->source);
              DCHECK(source != -1);
              callback(ParserAction::Connect(source, it->role, 0));
              deferred.erase(it--);
            }
          }

          // Emit ASSIGNs and (deferred) CONNECTs for evoked frame.
          for (const Slot &slot : frame) {
            Handle role = slot.name;
            Handle value = slot.value;

            // Skip id:/is:/isa: slots.
            if (role.IsId() || role.IsIs() || role.IsIsA()) continue;

            int target = attention.index(value);
            if (target != -1) {
              // Emit CONNECT for value already in the attention buffer.
              callback(ParserAction::Connect(0, role, target));
            } else if (IsAnonymousFrame(store, value)) {
              // Defer CONNECT for anonymous frame that is not in the
              // attention buffer.
              deferred.emplace_back(h, role, value);
            } else {
              // Emit ASSIGN for other values.
              callback(ParserAction::Assign(0, role, value));
            }
          }
        }
      }
    }

    // Shift to next token.
    callback(ParserAction::Shift());
  }
}

void Generate(const Document &document,
              std::function<void(const ParserAction &)> callback) {
  Generate(document, 0, document.length(), callback);
}

}  // namespace nlp
}  // namespace sling


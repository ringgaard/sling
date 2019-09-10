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

#include "sling/nlp/parser/parser-state.h"

#include "sling/base/logging.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"
#include "sling/nlp/document/document.h"
#include "sling/string/strcat.h"

namespace sling {
namespace nlp {

ParserState::ParserState(Document *document, int begin, int end)
    : document_(document),
      begin_(begin),
      end_(end),
      current_(begin),
      step_(0),
      done_(false),
      attention_(document_->store()) {}

string ParserState::DebugString() const {
  static const int MAX_ATTENTION = 10;
  string s =
      StrCat("Begin:", begin_, " End:", end_, " Current:", current_,
             " Done: ", (done_ ? "Y" : "N"), " AttentionSize: ",
             attention_.size(), "\n");
  for (int i = 0; i < attention_.size(); ++i) {
    if (i == MAX_ATTENTION) {
      StrAppend(&s, "..and ", (attention_.size() - MAX_ATTENTION), " more.\n");
      break;
    }
    StrAppend(&s, "AttentionIndex: ", i,
              " FrameType:", store()->DebugString(type(i)), "\n");
  }

  return s;
}

void ParserState::Apply(const ParserAction &action) {
  switch (action.type) {
    case ParserAction::SHIFT:
      Shift();
      break;

    case ParserAction::STOP:
      Stop();
      break;

    case ParserAction::MARK:
      Mark();
      break;

    case ParserAction::EVOKE:
      Evoke(action.length, action.label);
      break;

    case ParserAction::REFER:
      Refer(action.length, action.target);
      break;

    case ParserAction::CONNECT:
      Connect(action.source, action.role, action.target);
      break;

    case ParserAction::ASSIGN:
      Assign(action.source, action.role, action.label);
      break;

    case ParserAction::EMBED:
      Embed(action.target, action.role, action.label);
      break;

    case ParserAction::ELABORATE:
      Elaborate(action.source, action.role, action.label);
      break;

    case ParserAction::CASCADE:
      LOG(FATAL) << "Cannot apply CASCADE action";
      break;
  }
  step_++;
}

// Returns if 'frame' has a 'role' slot whose value is 'value'.
static bool SlotPresent(const Frame &frame, Handle role, Handle value) {
  for (const auto &slot : frame) {
    if (slot.name == role && slot.value == value) return true;
  }

  return false;
}

bool ParserState::CanApply(const ParserAction &action) const {
  if (done_) return false;
  switch (action.type) {
    case ParserAction::CASCADE:
      // Do not allow cacading back to the main cascade.
      return action.delegate > 0;

    case ParserAction::SHIFT:
      // Do not allow shifting past the end of the input buffer.
      return current_ < end_;

    case ParserAction::STOP:
      // Only allow stop if we are at the end of the input buffer.
      return current_ == end_;

    case ParserAction::MARK:
      return current_ < end_ && marks_.size() < MAX_MARK_DEPTH;

    case ParserAction::EVOKE: {
      int begin, end;
      if (action.length == 0) {
        // EVOKE paired with MARK.
        if (marks_.empty()) return false;
        begin = marks_.back().token;
        end = current_ + 1;
      } else {
        // EVOKE with explicit length.
        begin = current_;
        end = current_ + action.length;
      }

      // Check that phrase is inside the input buffer.
      if (end > end_) return false;

      bool crossing = false;
      auto *enclosing = document_->EnclosingSpan(begin, end, &crossing);
      if (crossing) return false;

      // Check for duplicates.
      if (enclosing != nullptr &&
          enclosing->begin() == begin &&
          enclosing->end() == end) {
        return !enclosing->Evokes(action.label);
      }
      return true;
    }

    case ParserAction::REFER: {
      int length = action.length;
      int index = action.target;

      // Check that phrase is inside input buffer.
      int end = current_ + length;
      if (end > end_) return false;

      // Check that 'index' is valid.
      if (index >= attention_.size()) return false;

      bool crossing = false;
      auto *enclosing = document_->EnclosingSpan(current_, end, &crossing);
      if (crossing) return false;

      // Check for duplicates.
      if (enclosing != nullptr && enclosing->begin() == current_ &&
          enclosing->end() == end) {
        Handle proposed = Attention(index);
        Handles evoked(store());
        enclosing->AllEvoked(&evoked);
        for (Handle h : evoked) {
          if (h == proposed) return false;
        }
      }

      return true;
    }

    case ParserAction::ASSIGN: {
      // Check that source is a valid frame.
      int source = action.source;
      if (source >= attention_.size()) return false;

      // Check that we haven't output this assignment in the past.
      Frame frame(store(), Attention(source));
      return !SlotPresent(frame, action.role, action.label);
    }

    case ParserAction::CONNECT: {
      // Check that source and target are valid indices.
      int source = action.source;
      int target = action.target;
      if (source >= attention_.size()) return false;
      if (target >= attention_.size()) return false;

      // Check that we haven't output this connection before.
      Frame frame(store(), Attention(source));
      return !SlotPresent(frame, action.role, Attention(target));
    }

    case ParserAction::EMBED: {
      // Check that target is a valid index into the attention buffer.
      if (action.target >= attention_.size()) return false;

      // Check that we haven't embedded the same frame the same way.
      Handle target = Attention(action.target);
      for (const auto &e : embed_) {
        if (e.first == target && e.second == action.label) return false;
      }

      return true;
    }

    case ParserAction::ELABORATE: {
      // Check that source is a valid index into the attention buffer.
      if (action.source >= attention_.size()) return false;

      // Check that we haven't elaborated the same frame the same way.
      Handle source = Attention(action.source);
      for (const auto &e : elaborate_) {
        if (e.first == source && e.second == action.label) return false;
      }

      return true;
    }
  }

  return false;
}

void ParserState::Shift() {
  // Move to the next token in the input buffer.
  current_++;

  // Clear the states for EMBED and ELABORATE.
  embed_.clear();
  elaborate_.clear();
}

void ParserState::Stop() {
  done_ = true;
}

void ParserState::Evoke(int length, Handle type) {
  // Create new frame.
  Handle frame;
  if (type.IsNil()) {
    // Allocate empty frame.
    frame = store()->AllocateFrame(nullptr, nullptr);
  } else {
    // Allocate frame with type.
    Slot slot(Handle::isa(), type);
    frame = store()->AllocateFrame(&slot, &slot + 1);
  }

  // Get or create a new mention.
  int begin, end;
  if (length == 0) {
    begin = marks_.back().token;
    end = current_ + 1;
    marks_.pop_back();
  } else {
    begin = current_;
    end = current_ + length;
  }
  Span *span = document_->AddSpan(begin, end);
  DCHECK(span != nullptr) << begin << " " << end;
  span->Evoke(frame);

  // Add new frame to the attention buffer.
  Add(frame);
}

void ParserState::Refer(int length, int index) {
  // Create new mention.
  auto *span = document_->AddSpan(current_, current_ + length);

  // Refer to an existing frame.
  Handle frame = Attention(index);
  span->Evoke(frame);
  Center(index);
}

void ParserState::Mark() {
  marks_.emplace_back(current_, step_);
}

void ParserState::Connect(int source, Handle role, int target) {
  // Create new slot with the specified role linking source to target.
  Handle subject = Attention(source);
  Handle object = Attention(target);
  store()->Add(subject, role, object);

  // Move the source frame to the center of attention.
  Center(source);
}

void ParserState::Assign(int frame, Handle role, Handle value) {
  // Create new slot in the source frame.
  Handle subject = Attention(frame);
  store()->Add(subject, role, value);

  // Move the frame to the center of attention.
  Center(frame);
}

void ParserState::Embed(int frame, Handle role, Handle type) {
  // Create new frame with the specified type and add link to target frame.
  Handle target = Attention(frame);
  Slot slots[2];
  slots[0].name = Handle::isa();
  slots[0].value = type;
  slots[1].name = role;
  slots[1].value = target;
  Handle h = store()->AllocateFrame(slots, slots + 2);
  embed_.emplace_back(target, type);

  // Add new frame to the attention buffer.
  Add(h);

  // Add new frame as a thematic frame to the document.
  document_->AddTheme(h);
}

void ParserState::Elaborate(int frame, Handle role, Handle type) {
  // Create new frame with the specified type.
  Handle source = Attention(frame);
  Slot slot(Handle::isa(), type);
  Handle target = store()->AllocateFrame(&slot, &slot + 1);

  // Add new frame to the attention buffer.
  Add(target);

  // Add link to new frame from source frame.
  store()->Add(source, role, target);
  elaborate_.emplace_back(Attention(frame), type);

  // Add new frame as a thematic frame to the document.
  document_->AddTheme(target);
}

void ParserState::Add(Handle frame) {
  attention_.push_back(frame);
  create_step_[frame] = step_;
  focus_step_[frame] = step_;
}

void ParserState::Center(int index) {
  Handle frame = Attention(index);
  if (index != 0) {
    attention_.erase(attention_.end() - 1 - index);
    attention_.push_back(frame);
  }
  focus_step_[frame] = step_;
}

void ParserState::GetFocus(int k, Handles *center) const {
  center->clear();
  for (int i = attention_.size() - 1; i >= 0 && center->size() < k; --i) {
    center->push_back(attention_[i]);
  }
}

int ParserState::AttentionIndex(Handle handle, int k) const {
  if (k < 0 || k > attention_.size()) k = attention_.size();
  for (int i = 0; i < k; ++i) {
    if (Attention(i) == handle) return i;
  }
  return -1;
}

Handle ParserState::type(int index) const {
  return store()->GetFrame(Attention(index))->get(Handle::isa());
}

int ParserState::FrameEvokeBegin(int attention_index) const {
  if (attention_index >= attention_.size()) return -1;

  Handle handle = Attention(attention_index);
  for (const auto &it : document_->EvokingSpans(handle)) {
    return it.second->begin();
  }
  return -1;
}

int ParserState::FrameEvokeEnd(int attention_index) const {
  if (attention_index >= attention_.size()) return -1;

  Handle handle = Attention(attention_index);
  for (const auto &it : document_->EvokingSpans(handle)) {
    return it.second->end();
  }
  return -1;
}

}  // namespace nlp
}  // namespace sling


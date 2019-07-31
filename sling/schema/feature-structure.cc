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

#include "sling/schema/feature-structure.h"

#include <algorithm>

#include "sling/base/logging.h"
#include "sling/frame/object.h"
#include "sling/frame/store.h"

namespace sling {

FeatureStructure::FeatureStructure(Store *store)
    : graph_(store), store_(store) {}

FeatureStructure::FeatureStructure(Store *store, Handle tmpl)
    : FeatureStructure(store) {
  // Get template frame from store.
  FrameDatum *frame = store_->GetFrame(tmpl);

  // Add template to the graph.
  graph_.insert(graph_.end(), frame->begin(), frame->end());

  // Update directory.
  RebuildDirectory();
}

int FeatureStructure::AllocateNode(int num_slots) {
  // Node index is the next free element in the graph buffer.
  int node = graph_.size();

  // Reserve space in graph buffer for node.
  graph_.resize(graph_.size() + kHeaderSlots + num_slots);

  // Return index into graph buffer for the node.
  return node;
}

int FeatureStructure::AllocateContentNode(int num_slots) {
  int node = AllocateNode(num_slots);
  Node &hdr = NodeHeader(node);
  hdr.type = Handle::Integer(CONTENT);
  hdr.size = Handle::zero();
  return node;
}

int FeatureStructure::AllocateValueNode(Handle value) {
  int node = AllocateNode(0);
  Node &hdr = NodeHeader(node);
  hdr.type = Handle::Integer(VALUE);
  hdr.value = value;

  // Return index of value node.
  return node;
}

void FeatureStructure::AddSlot(int node, Handle name, Handle value) {
  Node &hdr = NodeHeader(node);
  NodeSlot(node, hdr.size.AsInt()).assign(name, value);
  hdr.size.Increment();
}

int FeatureStructure::AddSlot(int node, Handle name) {
  Node &hdr = NodeHeader(node);
  int pos = hdr.size.AsInt();
  NodeSlot(node, pos).assign(name, Handle::nil());
  hdr.size.Increment();
  return node + kHeaderSlots + pos;
}

void FeatureStructure::AddSlot(int node, const Slot &slot) {
  Node &hdr = NodeHeader(node);
  NodeSlot(node, hdr.size.AsInt()) = slot;
  hdr.size.Increment();
}

bool FeatureStructure::Atomic(Handle handle) {
  // Numbers and nil values are atomic.
  if (!handle.IsRef() || handle.IsNil()) return true;

  // Only anonymous frames are non-atomic.
  Datum *datum = store_->GetObject(handle);
  if (!datum->IsFrame()) return true;
  if (datum->AsFrame()->IsPublic()) return true;
  return false;
}

bool FeatureStructure::Empty(int node) {
  Node &hdr = NodeHeader(node);
  switch (hdr.type.AsInt()) {
    case FORWARD:
      return true;

    case REFERENCE: {
      // Check if frame has any non-id slots.
      Datum *datum = store_->GetObject(hdr.ref);
      if (!datum->IsFrame()) return false;
      FrameDatum *frame = datum->AsFrame();
      if (frame->IsProxy()) return true;
      if (frame->IsPublic()) return false;
      for (Slot *s = frame->begin(); s < frame->end(); ++s) {
        if (s->name != Handle::id()) return false;
      }
      return true;
    }

    case VALUE:
      return hdr.value.IsNil();

    case CONTENT:
    case UNIFYING:
      return hdr.size.IsZero();
  }

  return false;
}

int FeatureStructure::Follow(int node) {
  // Traverse forwarding pointers until end of chain is reached.
  for (;;) {
    Node &hdr = NodeHeader(node);
    if (hdr.type != Handle::Integer(FORWARD)) return node;
    node = hdr.forward.AsInt();
  }
}

bool FeatureStructure::SortByRole(const Slot &a, const Slot &b) {
  if (a.name != b.name) {
    return HandleRank(a.name) < HandleRank(b.name);
  } else {
    return HandleRank(a.value) < HandleRank(b.value);
  }
}

void FeatureStructure::SortNodes(int node) {
  Slot *slots = &graph_[node + kHeaderSlots];
  std::sort(slots, slots + NodeHeader(node).size.AsInt(), SortByRole);
}

int FeatureStructure::CopyFrame(Handle handle) {
  // Get frame.
  FrameDatum *frame = store_->GetFrame(handle);

  // Allocate new content node.
  int node = AllocateContentNode(frame->slots());

  // Copy slots from frame to the node.
  for (Slot *s = frame->begin(); s < frame->end(); ++s) {
    if (s->name == Handle::id()) continue;
    if (Atomic(s->value)) {
      AddSlot(node, s->name, s->value);
    } else {
      AddSlot(node, s->name, Handle::Index(Reference(s->value)));
    }
  }

  // Sort slots according to slot name and value handle rank.
  SortNodes(node);

  return node;
}

int FeatureStructure::EnsureCopy(int node) {
  // Assume that node is not already forwarded or simple value.
  Node &hdr = NodeHeader(node);
  DCHECK(hdr.type != Handle::Integer(FORWARD));
  DCHECK(hdr.type != Handle::Integer(VALUE));

  // If node is already a copy, we are done.
  if (hdr.type == Handle::Integer(UNIFYING) ||
      hdr.type == Handle::Integer(CONTENT)) {
    return node;
  }

  // Make a new node with a copy of the frame.
  DCHECK(hdr.type == Handle::Integer(REFERENCE));
  int copy = CopyFrame(hdr.ref);

  // Forward original node to the new copy.
  Forward(node, copy);

  // Return index of copy.
  return copy;
}

int FeatureStructure::Reference(Handle handle) {
  // Check if we already has this handle in the directory.
  auto f = directory_.find(handle);
  if (f != directory_.end()) return f->second;

  // Create new reference node and insert it in dictionary.
  int node = AllocateNode(0);
  Node &hdr = NodeHeader(node);
  hdr.type = Handle::Integer(REFERENCE);
  hdr.ref = handle;

  // Add reference to dictionary.
  directory_[handle] = node;

  // Return index of reference node.
  return node;
}

int FeatureStructure::AddFrame(Handle frame) {
  // You cannot add atomic values as reference nodes.
  CHECK(!Atomic(frame));

  // Return node index for reference frame.
  return Reference(frame);
}

int FeatureStructure::Unify(int n1, int n2) {
  // Resolve input nodes by following the forwarding pointers.
  n1 = Follow(n1);
  n2 = Follow(n2);

  // If nodes are equal then they trivially unify.
  if (n1 == n2) return n1;

  // If n2 is empty, then return n1.
  if (Empty(n2)) {
    Forward(n2, n1);
    return n1;
  }

  // If n1 is empty, then return n2.
  if (Empty(n1)) {
    Forward(n1, n2);
    return n2;
  }

  // Unify value nodes. A non-empty value node can only be unified with another
  // value node and they must have the same value.
  Node &node1 = NodeHeader(n1);
  Node &node2 = NodeHeader(n2);
  if (node1.type == Handle::Integer(VALUE)) {
    if (node2.type != Handle::Integer(VALUE)) return -1;
    if (node1.value != node2.value) return -1;
    Forward(n2, n1);
    return n1;
  } else if (node2.type == Handle::Integer(VALUE)) {
    return -1;
  }

  // If the feature structure is recursive, we can end up trying to unify nodes
  // that we are currently in the process of unifying. This algorithm cannot
  // handle this case in general, so we use the heuristic of just returning
  // the node without unifying it with the other node. This is not strictly
  // correct, but avoids infinite recursion and handles then common case of
  // inverse roles.
  if (node1.type == Handle::Integer(UNIFYING)) {
    LOG(WARNING) << "Partial unification of recursive node " << n1;
    Forward(n2, n1);
    return n1;
  }
  if (node2.type == Handle::Integer(UNIFYING)) {
    LOG(WARNING) << "Partial unification of recursive node " << n2;
    Forward(n1, n2);
    return n2;
  }

  // We need copies of both n1 and n2 in order to unify their slots.
  int c1 = EnsureCopy(n1);
  int c2 = EnsureCopy(n2);
  NodeHeader(c1).type = Handle::Integer(UNIFYING);
  NodeHeader(c2).type = Handle::Integer(UNIFYING);
  int num1 = NodeHeader(c1).size.AsInt();
  int num2 = NodeHeader(c2).size.AsInt();

  // Create new node for the unification. The maximum number of slots in the
  // unified node is the sum of the number of slots for n1 and n2.
  int node = AllocateContentNode(num1 + num2);

  // The slots are sorted by handle rank so we can merge the two sets of slots.
  int s1 = 0;    // current slot in first node
  int s2 = 0;    // current slot in second node
  while (s1 < num1 && s2 < num2) {
    Word rank1 = HandleRank(NodeSlot(c1, s1).name);
    Word rank2 = HandleRank(NodeSlot(c2, s2).name);
    if (rank1 < rank2) {
      // Copy slot from the first node.
      AddSlot(node, NodeSlot(c1, s1++));
    } else if (rank2 < rank1) {
      // Copy slot from the second node.
      AddSlot(node, NodeSlot(c2, s2++));
    } else if (rank1 == kIsARank) {
      // Both nodes have types that need to be unified. First find the set of
      // types (i.e. isa: slots) for each node.
      int t1 = s1;
      int t2 = s2;
      while (s1 < num1 && NodeSlot(c1, s1).name.IsIsA()) s1++;
      while (s2 < num2 && NodeSlot(c2, s2).name.IsIsA()) s2++;

      // Unify the types of the two nodes.
      UnifyTypes(&NodeSlot(c1, t1), &NodeSlot(c1, s1),
                 &NodeSlot(c2, t2), &NodeSlot(c2, s2),
                 node);
    } else {
      // Slot name is shared between first and second node, so the slot values
      // have to be unified.
      Slot slot1 = NodeSlot(c1, s1++);
      Slot slot2 = NodeSlot(c2, s2++);
      DCHECK(slot1.name == slot2.name);

      // If the value is a non-atomic (i.e. complex) value it will have an index
      // value because of the copying operation.
      bool complex1 = slot1.value.IsIndex();
      bool complex2 = slot2.value.IsIndex();

      Handle name = slot1.name;
      Handle value;
      if (complex1 && complex2) {
        // Values are complex. Try to unify them.
        int result = Unify(slot1.value.AsIndex(), slot2.value.AsIndex());

        // Fail unification if unification of values failed.
        if (result == -1) return -1;

        // We use a special index handle to refer to the unified graph node.
        value = Handle::Index(result);
      } else if (complex1) {
        // First node is complex and the second is simple. Create a value node
        // for the simple node and try to unify it with the complex node.
        int simple = AllocateValueNode(slot2.value);
        int result = Unify(slot1.value.AsIndex(), simple);
        if (result == -1) return -1;
        value =  Handle::Index(result);
      } else if (complex2) {
        // First node is simple and the second is complex. Create a value node
        // for the simple node and try to unify it with the complex node.
        int simple = AllocateValueNode(slot1.value);
        int result = Unify(simple, slot2.value.AsIndex());
        if (result == -1) return -1;
        value =  Handle::Index(result);
      } else {
        // Atomic values unify if they are equal. If one of them is nil, it
        // unifies to the other value.
        if (slot1.value == slot2.value) {
          value = slot1.value;
        } else if (slot1.value.IsNil()) {
          value = slot2.value;
        } else if (slot2.value.IsNil()) {
          value = slot1.value;
        } else {
          // Atomic values cannot be unified.
          return -1;
        }
      }

      // Add slot with unified value.
      AddSlot(node, name, value);
    }
  }

  // Copy remaining slots from the first node.
  while (s1 < num1) AddSlot(node, NodeSlot(c1, s1++));

  // Copy remaining slots from the second node.
  while (s2 < num2) AddSlot(node, NodeSlot(c2, s2++));

  // Forward both n1 and n2 to the unified node.
  Forward(c1, node);
  Forward(c2, node);

  // Return index of unified node.
  return node;
}

bool FeatureStructure::SubsumedBy(Handle type, Slot *begin, Slot *end) {
  if (types_ != nullptr) {
    for (Slot *t = begin; t < end; ++t) {
      if (types_->Subsumes(type, t->value)) return true;
    }
  }
  return false;
}

void FeatureStructure::UnifyTypes(Slot *types1, Slot *end1,
                                  Slot *types2, Slot *end2,
                                  int result) {
  // It is assumed that the types for each argument are maximally general, i.e.
  // no type is subsumed by any other type.
  Slot *s1 = types1;
  Slot *s2 = types2;
  while (s1 < end1 && s2 < end2) {
    Handle t1 = s1->value;
    Handle t2 = s2->value;
    if (t1.raw() < t2.raw()) {
      // Type t1 only in the first type set. Add it to the result unless it is
      // subsumed by a type in the second type set.
      if (!SubsumedBy(t1, types2, end2)) {
        AddSlot(result, Handle::isa(), t1);
      }
      s1++;
    } else if (t2.raw() < t1.raw()) {
      // Type t2 only in the second type set. Add it to the result unless it is
      // subsumed by a type in the first type set.
      if (!SubsumedBy(t2, types1, end1)) {
        AddSlot(result, Handle::isa(), t2);
      }
      s2++;
    } else {
      // Type in both sets. Add it to the result.
      AddSlot(result, Handle::isa(), t1);
      s1++;
      s2++;
    }
  }

  // Unify remaining types in the first type set.
  while (s1 < end1) {
    Handle t1 = s1->value;
    if (!SubsumedBy(t1, types2, end2)) {
      AddSlot(result, Handle::isa(), t1);
    }
    s1++;
  }

  // Unify remaining types in the second type set.
  while (s2 < end2) {
    Handle t2 = s2->value;
    if (!SubsumedBy(t2, types1, end1)) {
      AddSlot(result, Handle::isa(), t2);
    }
    s2++;
  }
}

void FeatureStructure::PruneRoles(int node) {
  // We can only unify types if we have a type system.
  if (types_ == nullptr) return;

  // Get slot range for node.
  Node &hdr = NodeHeader(node);
  Slot *begin = &NodeSlot(node, 0);
  Slot *end = begin + hdr.size.AsInt();

  // Run through all node slots. We keep track of the first deleted node to
  // make it faster to run though the slots and delete the nodes at the end.
  Slot *s = begin;
  Slot *first = end;
  while (s < end) {
    // For each type in the node, we prune the roles based on the role maps for
    // the types. Nodes are sorted in handle rank order so we can stop when we
    // get past the isa slots.
    Word rank = HandleRank(s->name);
    if (rank < kIsARank) {
      s++;
    } else if (rank > kIsARank) {
      break;
    } else {
      // Get role map for the type from the type system.
      Handle type = s++->value;
      Handle rolemap = types_->GetRoleMap(type);
      if (rolemap.IsNil()) continue;
      FrameDatum *types = store_->GetFrame(rolemap);
      Slot *r = types->begin();
      Slot *rend = types->end();
      if (r == rend) continue;

      // Run through the remaining slots and remove inherited roles. The role
      // map is sorted in handle rank order, so we can do a merge run over the
      // slots.
      Slot *t = s;
      while (t < end && r < rend) {
        Word trank = HandleRank(t->name);
        Word rrank = HandleRank(r->name);
        if (trank < rrank) {
          t++;
        } else if (trank > rrank) {
          r++;
        } else {
          // Slot name is in role map. Clear the role so we can remove it later.
          t->name = Handle::nil();
          t->value = Handle::nil();
          if (t < first) first = t;
          t++;
        }
      }
    }
  }

  // Remove all the nil:nil slots.
  if (first < end) {
    // Delete nil nodes and move the rest up.
    Slot *s = first;
    Slot *t = s;
    int deleted = 0;
    while (s < end) {
      if (s->name.IsNil() && s->value.IsNil()) {
        deleted++;
      } else {
        t->name = s->name;
        t->value = s->value;
        t++;
      }
      s++;
    }
    hdr.size.Subtract(deleted);
  }
}

Handle FeatureStructure::Template() {
  Slot *begin = &*graph_.begin();
  Slot *end = &*graph_.end();
  return store_->AllocateFrame(begin, end);
}

Handle FeatureStructure::Construct(int node, bool destructive) {
  if (destructive) {
    // Invert the frame import directory.
    std::unordered_map<int, Handle> origin;
    for (const auto &it : directory_) {
      origin[Follow(it.second)] = it.first;
    }
    return ConstructNode(node, &origin);
  } else {
    return ConstructNode(node, nullptr);
  }
}

Handle FeatureStructure::ConstructNode(
    int node,
    std::unordered_map<int, Handle> *origin) {
  // Follow forwarding pointers.
  node = Follow(node);

  // If this is a reference or value node, just return the reference or value.
  Node &hdr = NodeHeader(node);
  if (hdr.type == Handle::Integer(REFERENCE)) return hdr.ref;
  if (hdr.type == Handle::Integer(VALUE)) return hdr.value;

  // Prune aliased roles.
  DCHECK(hdr.type == Handle::Integer(CONTENT));
  PruneRoles(node);

  // Check if this frame should replace an original frame. This causes the
  // construction to be destructive and updates the original frames instead of
  // creating new ones.
  Handle original = Handle::nil();
  if (origin != nullptr) {
    auto f = origin->find(node);
    if (f != origin->end()) original = f->second;
  }

  // Allocate frame unless we replace an existing frame.
  Slot *begin = &NodeSlot(node, 0);
  Slot *end = begin + hdr.size.AsInt();
  int num_slots = end - begin;
  hdr.type = Handle::Integer(REFERENCE);
  if (original.IsNil()) {
    hdr.ref = store_->AllocateFrame(num_slots);
  } else {
    hdr.ref = original;
  }

  // Externalize all slots.
  for (Slot *s = begin; s < end; ++s) {
    if (s->value.IsIndex()) {
      s->value = ConstructNode(s->value.AsIndex(), origin);
    }
  }

  // Update or replace frame with externalized slot values.
  if (original.IsNil()) {
    store_->UpdateFrame(hdr.ref, begin, end);
  } else {
    store_->AllocateFrame(begin, end, original);
  }

  // Return new frame.
  return hdr.ref;
}

int FeatureStructure::Transfer(int node, Slots *target) {
  // Follow forwarding pointers.
  node = Follow(node);

  Node &hdr = NodeHeader(node);
  int dest = target->size();
  if (hdr.type == Handle::Integer(REFERENCE)) {
    // Check if node has already been transfered.
    if (hdr.ref.IsIndex()) {
      // Already transferred.
      return hdr.ref.AsIndex();
    } else {
      // Add reference node to target.
      target->emplace_back(Handle::Integer(REFERENCE), hdr.ref);
      hdr.type = Handle::Integer(REFERENCE);
      hdr.ref = Handle::Index(dest);
    }
  } else if (hdr.type == Handle::Integer(VALUE)) {
    // Add value node to target.
    DCHECK(!hdr.value.IsIndex());
    target->emplace_back(Handle::Integer(VALUE), hdr.value);
    hdr.type = Handle::Integer(REFERENCE);
    hdr.ref = Handle::Index(dest);
  } else {
    // Copy contents node to target.
    DCHECK(hdr.type == Handle::Integer(CONTENT));
    int num_slots = hdr.size.AsInt();
    target->emplace_back(Handle::Integer(CONTENT), hdr.size);

    // Make room for slots in target.
    int t = target->size();
    target->resize(t + num_slots);

    // Mark node as transferred.
    hdr.type = Handle::Integer(REFERENCE);
    hdr.ref = Handle::Index(dest);

    // Copy slots.
    Slot *begin = &NodeSlot(node, 0);
    Slot *end = begin + num_slots;
    for (Slot *s = begin; s < end; ++s) {
      if (s->value.IsIndex()) {
        int value = Transfer(s->value.AsIndex(), target);
        (*target)[t++].assign(s->name, Handle::Index(value));
      } else {
        (*target)[t++].assign(s->name, s->value);
      }
    }
  }

  // Return index of transferred node in target.
  return dest;
}

void FeatureStructure::RebuildDirectory() {
  // Clear the directory.
  directory_.clear();

  // Run through all the nodes and update the directory.
  int node = 0;
  while (node < graph_.size()) {
    Node &hdr = NodeHeader(node);
    if (hdr.type == Handle::Integer(REFERENCE)) {
      // Update directory and go to next node.
      directory_[hdr.ref] = node;
      node += kHeaderSlots;
    } else if (hdr.type == Handle::Integer(CONTENT)) {
      // Skip slots.
      node += hdr.size.AsInt() + kHeaderSlots;
    } else {
      // Go to next node.
      node += kHeaderSlots;
    }
  }
}

int FeatureStructure::Compact(int root) {
  // Allocate array for new graph.
  Slots target(store_);

  // Transfer root node and all dependents to the new graph.
  root = Transfer(root, &target);

  // Replace the graph with the new graph.
  graph_.swap(target);

  // Rebuild directory.
  RebuildDirectory();

  // Return node index of the root node in the new graph (actually always zero).
  return root;
}

bool FeatureStructure::Trim(int node) {
  // Follow forwarding pointers.
  node = Follow(node);

  // Only trim content nodes. This will skip nodes that are already being
  // trimmed.
  Node &hdr = NodeHeader(node);
  if (hdr.type != Handle::Integer(CONTENT)) return false;

  // Mark slot as being trimmed.
  hdr.type = Handle::Integer(TRIMMING);

  // Trim slots.
  int num_slots = hdr.size.AsInt();
  Slot *begin = &NodeSlot(node, 0);
  Slot *end = begin + num_slots;
  Slot *next = begin;
  bool empty = true;
  for (Slot *s = begin; s < end; ++s) {
    bool prune = false;
    if (!s->name.IsIsA()) {
      if (s->value.IsIndex()) {
        prune = Trim(s->value.AsIndex());
      }
      if (!prune) empty = false;
    }
    if (!prune) {
      if (s != next) {
        next->name = s->name;
        next->value = s->value;
      }
      next++;
    }
  }

  // Trimming completed.
  hdr.type = Handle::Integer(CONTENT);
  if (next != end) hdr.size = Handle::Integer(next - begin);
  return empty;
}

}  // namespace sling


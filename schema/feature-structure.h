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

#ifndef SCHEMA_FEATURE_STRUCTURE_H_
#define SCHEMA_FEATURE_STRUCTURE_H_

#include <unordered_map>

#include "frame/object.h"
#include "frame/store.h"

namespace sling {

// Abstract interface to type system. This is used for type unification in
// typed feature structures.
class TypeSystem {
 public:
  virtual ~TypeSystem() {}

  // Checks if supertype subsumes the subtype.
  virtual bool Subsumes(Handle supertype, Handle subtype) = 0;

  // Returns the role mapping for the type. A role map is a frame where each
  // slot represents an aliased role. The slot name is the role in the parent
  // type and the slot value is the alias for the role in the subtype.
  virtual Handle GetRoleMap(Handle type) = 0;
};

// A feature structure is a directed graph, where each node represents a frame,
// and the edges between nodes represents frame slots. This class represents a
// whole graph as an array of slots, where special index handles are used for
// encoding references between nodes (see below for details on the
// representation of nodes). A feature structure can be initialized from
// a frame in the object store, or a pre-compiled template containing a complete
// graph. A feature structure can also be converted to a set of frames
// representing the same graph. A feature structure can be assigned a type (or
// types) by adding isa: slots to the node.
//
// A feature structure can either be atomic or complex. An atomic feature
// structure is regarded as a simple value, e.g. an integer or string. A frame
// is also regarded as atomic if it has (non-local) identity. All other frames
// are considered complex feature structures.
//
// The primary operation on feature structures is unification. Unification is a
// binary operation over two features structures, used for comparing and
// combining the information in the two feature structures. Unification either
// returns a merged feature structure with the information from both feature
// structures, or fails if they are incompatible. Unification preserves and
// possibly adds information to the resulting feature structure (monotonicity).
// Two atomic feature structures can be unified if they have the same value, or
// if one or both are nil or empty. The result of the unification is the value
// itself.
//
// Two complex feature structures can be unified if the values of all the common
// slots can be unified. The result of the unification is then the unified
// values of all the common slots plus the slots from the each that are not in
// common.
//
// Feature structure types are unified according to a type system that defines
// the subsumption relationship between types.
class FeatureStructure {
 public:
  // Initializes empty feature structure.
  explicit FeatureStructure(Store *store);

  // Initializes feature structure from template frame.
  FeatureStructure(Store *store, Handle tmpl);

  // Allocates new node in feature structure, reserving space for a number of
  // slots. Returns a node index for the newly allocated node.
  int AllocateNode(int num_slots);

  // Allocates content node.
  int AllocateContentNode(int num_slots);

  // Allocates value node.
  int AllocateValueNode(Handle value);

  // Adds slots to node. This assumes that there is room for another slot in the
  // node.
  void AddSlot(int node, Handle name, Handle value);
  void AddSlot(int node, const Slot &slot);

  // Adds empty slot. This sets the value to nil and returns the node index of
  // the slot.
  int AddSlot(int node, Handle name);

  // Adds frame to feature structure by adding a reference node and returns the
  // node index for the frame. This does not perform any deep copying.
  int AddFrame(Handle frame);

  // Unifies two nodes in the feature structure, n1 and n2 identified by their
  // node indices,  and returns the node index of the result or -1 if the
  // unification fails. Neither n1 nor n2 can be atomic. This operation is
  // non-destructive and will not change any objects in the store.
  int Unify(int n1, int n2);

  // Constructs feature structure in store by creating frames in the store for
  // the feature structure. Returns the handle for the root frame for the
  // feature structure. Destructive construction replaces the original frames
  // with the unified ones. Otherwise, the original frames are not modified.
  Handle Construct(int node, bool destructive);
  Handle Construct(int node) { return Construct(node, false); }

  // Performs graph compaction by removing any nodes that are not referenced by
  // the root node. Returns the new node index of the root node.
  int Compact(int root);

  // Trims feature structure by removing nodes that are empty in the sense that
  // they only have isa slots. This is applied recursively so slots pointing
  // to trimmed nodes are also removed. Returns true if the node was trimmed.
  bool Trim(int node);

  // Produces a template frame in the store and returns a handle to it.
  Handle Template();

  // Sets a type system for the feature structure that should be used for type
  // unification.
  void SetTypeSystem(TypeSystem *types) { types_ = types; }

  // Sort predicate for sorting slots according to name and value handle rank.
  static bool SortByRole(const Slot &a, const Slot &b);

 protected:
  // Node types.
  enum NodeType {
    FORWARD      = 0,
    REFERENCE    = 1,
    VALUE        = 2,
    CONTENT      = 3,
    UNIFYING     = 4,
    TRIMMING     = 5,
  };

  // Header slot for node. This has the same format as a slot.
  struct Node {
    Handle type;       // node type encoded as an integer handle
    union {
      Handle forward;  // node index of forwarding node (FORWARD)
      Handle ref;      // reference to external frame (REFERENCE)
      Handle value;    // simple node value (VALUE)
      Handle size;     // number of slots in node (CONTENT)
    };
  };

  // Number of header slots in node.
  static const int kHeaderSlots = 1;

  // The handle rank rotates the tag field of the handle to the top so special
  // slots have the lowest rank. Used for sorting slots.
  static Word HandleRank(Handle h) { return (h.bits >> 2) | (h.bits << 30); }
  static const Word kIsARank = Handle::kIsA >> 2;

  // Returns reference to header in node.
  Node &NodeHeader(int node) {
    return reinterpret_cast<Node &>(graph_[node]);
  }

  // Returns reference to slot in node.
  Slot &NodeSlot(int node, int index) {
    return graph_[node + kHeaderSlots + index];
  }

  // Returns reference to slot.
  Slot &SlotAt(int index) { return graph_[index]; }

  // Forwards one node to another.
  void Forward(int n1, int n2) {
    Node &hdr = NodeHeader(n1);
    hdr.type = Handle::Integer(FORWARD);
    hdr.forward = Handle::Integer(n2);
  }

  // Checks if a handle value is atomic. All non-frame handles are atomic, and
  // bound frames are also atomic.
  bool Atomic(Handle handle);

  // Checks if node is empty, i.e. if it does not have any slots.
  bool Empty(int node);

  // Resolves node index by following forwarding pointers.
  int Follow(int node);

  // Sort nodes in handle rank order.
  void SortNodes(int node);

  // Copies frame into feature structure and returns index for node.
  int CopyFrame(Handle handle);

  // Returns an index to a reference node for a non-atomic value. If the value
  // is not in the dictionary a new reference node is created.
  int Reference(Handle handle);

  // Makes a copy of a node if not already done.
  int EnsureCopy(int node);

  // Checks if type is subsumed by any type in the list.
  bool SubsumedBy(Handle type, Slot *begin, Slot *end);

  // Unifies two sets of types adding the unified types to the result node.
  void UnifyTypes(Slot *types1, Slot *end1,
                  Slot *types2, Slot *end2,
                  int result);

  // Prunes aliased roles in a node. This will only keep the most specific role
  // of the aliased roles. The role values are assumed to be unified through
  // constraints for inherited roles.
  void PruneRoles(int node);

  // Constructs frames in the store for node. The origin map can be used for
  // destructive construction where the original nodes are replaced by the
  // unified nodes.
  Handle ConstructNode(int node, std::unordered_map<int, Handle> *origin);

  // Rebuilds the directory for the graph. This assumes that the graph is
  // compacted so there is no unused space between the nodes.
  void RebuildDirectory();

  // Transfers node from this graph to another graph recursively. This updates
  // the reference pointers in the nodes, so this operation is destructive.
  // Returns the node index in the target graph.
  int Transfer(int node, Slots *target);

  // Array encoding of directed graph. It is arranged as an array of slots which
  // are tracked by the GC in the store. Logically, the graph buffer consists of
  // a sequence of graph nodes. Each node has a header slot optionally followed
  // by a list of content slots. The type field in the header node is used for
  // distinguishing between different node types.
  //
  // If the node type is CONTENT, the size field in the header contains the
  // number of (currently used) content slots in the node. There can be extra
  // unused slots after a node for adding extra slots to a node. The slots are
  // sorted by handle rank so that two content nodes can be unified using a
  // merge operation over the slots.
  //
  // If the node type is REFERENCE, the ref field in the header contains a
  // handle to the original frame in the store. During compaction, the ref field
  // is also used to encode that the node has been transferred using an index
  // handle.
  //
  // If the node type is VALUE, the value field in the header contains the
  // handle for the value. Value nodes are only used for simple values that
  // cannot be copied into a content node.
  //
  // If the node type is FORWARD, the forward field in the header contains the
  // index of the node that this node is forwarded to. This forwarding chain is
  // used for dereferencing node indices and the node at the end of this chain
  // is the actual node for the node index.
  //
  // The UNIFYING and TRIMMING node types are only used temporarily when
  // unifying and trimming frames. This is done to support recursive structures
  // with cycles in the graph.
  Slots graph_;

  // Store for frames.
  Store *store_;

  // Mapping from handles of the original frames to node indices in the graph.
  HandleMap<int> directory_;

  // Optional type system used for type unification. If no type system has been
  // provided, types are unified using a simple merge algorithm assuming no
  // subsumption between types.
  TypeSystem *types_ = nullptr;
};

}  // namespace sling

#endif  // SCHEMA_FEATURE_STRUCTURE_H_

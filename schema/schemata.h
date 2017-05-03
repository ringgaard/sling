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

// Schema compilation and frame construction. A schema is a descriptor for a
// frame type. It can contain parent types, role definitions including role
// types, and binding constraints.

#ifndef SCHEMA_SCHEMATA_H_
#define SCHEMA_SCHEMATA_H_

#include <vector>

#include "schema/feature-structure.h"
#include "frame/object.h"
#include "frame/store.h"
#include "string/text.h"

namespace sling {

// Schemata for frame construction.
class Schemata : public TypeSystem {
 public:
  // Initializes schemata for store.
  explicit Schemata(Store *store);

  // Constructs frame from schema and input. Returns nil if the unification
  // fails or there is no pre-compiled template for the schema.
  Handle Construct(Handle schema, Handle input);
  Object Construct(const Object &schema, const Object &input) {
    return Object(schema.store(), Construct(schema.handle(), input.handle()));
  }

  // Projects the input frame through a mapping and returns the output frame.
  // Returns nil if the projection failed. In non-destructive mode, no existing
  // frames are overwritte, where as in destructive mode, frames are replaced by
  // the output of the projection.
  Handle Project(Handle mapping, Handle input, bool destructive);

  // Checks if supertype subsumes the subtype.
  bool Subsumes(Handle supertype, Handle subtype) override;

  // Looks up named role for schema. This will also search the parent types for
  // a role with a matching name and return the most specific role with this
  // name. Returns nil if no role is found.
  Handle ResolveNamedRole(Handle schema, Text name);

  // Finds named role in schema. Returns nil if no role is found. This does not
  // seach the parent schemas for matches.
  Handle GetNamedRole(Handle schema, Text name);

  // Returns the role mapping for the type.
  Handle GetRoleMap(Handle type) override;

 private:
  // Gets pre-compiled schema template for schema.
  Handle GetTemplate(Handle schema) const {
    return store_->GetFrame(schema)->get(template_);
  }

  // Gets pre-compiled ancestors list for schema.
  Handle GetAncestors(Handle schema) const {
    return store_->GetFrame(schema)->get(ancestors_);
  }

  // Store for looking up schemata and constructing frames.
  Store *store_;

  // Symbols.
  Handle ancestors_;
  Handle template_;
  Handle rolemap_;
  Handle projections_;
  Handle input_;
  Handle output_;
  Handle name_;
  Handle role_;
};

// Feature structure for generating templates for schemata.
class SchemaFeatureStructure : public FeatureStructure {
 public:
  // Initializes feature structure.
  SchemaFeatureStructure(Store *store, TypeSystem *types);

  // Creates a node that represents a schema and returns its node or -1 if the
  // construction fails. This also applies all constraints from parent schemata,
  // role typing, and bindings.
  int ConstructSchema(Handle schema);

 private:
  // Creates a node that represents a typed role and returns its node.
  int ConstructRole(Handle role, Handle schema);

  // Creates a node that aliases the two roles.
  int ConstructAlias(Handle role1, Handle role2);

  // Creates a node that represents the binding and returns its node index or -1
  // if the construction fails. The binding is represented as an array. The
  // following types of binding are supported:
  //   [ <path> equals <path> ]
  //   [ <path> equals self ]
  //   [ <path> assign <value> ]
  //   [ <path> hastype <type> ]
  int ConstructBinding(Handle binding);

  // Constructs path. This returns the node for the head of the path. If the
  // path is 'self', this returns kSelf.
  int ConstructPath(Handle *begin, Handle *end, int *role);

  // Artificial self node.
  static const int kSelf = -2;

  // List of schemas under construction. These are tracked to avoid infinite
  // expansion of recursive definitions.
  std::vector<Handle> active_;

  // Symbols.
  Handle role_;
  Handle simple_;
  Handle target_;
  Handle binding_;
  Handle equals_;
  Handle assign_;
  Handle hastype_;
  Handle self_;
};

// Schema compiler.
class SchemaCompiler : public TypeSystem {
 public:
  // Initializes schema compiler.
  explicit SchemaCompiler(Store *store);

  // Pre-computes schema information for schema families in catalog. It uses
  // the precompute roles in the schema familes to determine what type of
  // pre-computation to do for each schema family.
  void PreCompute();
  void PreCompute(Handle catalog);

  // Compiles schema and stores compiled schema template in schema.
  Handle Compile(Handle schema);

  // Checks if supertype subsumes the subtype.
  bool Subsumes(Handle supertype, Handle subtype) override;

  // Returns the role mapping for the type. This will construct the role map
  // if it is not already in the schema.
  Handle GetRoleMap(Handle type) override;

  // Finds all ancestor schemata for schema. Returns a handle for an array that
  // contains all the ancestor schemata (including the schema itself) in sorted
  // order. If the schema does not have a pre-compiled ancestor list it will be
  // computed on the fly and the schema will be updated.
  Handle FindAncestors(Handle schema);

 private:
  // A role map is a frame where the slots are mappings from direct and indirect
  // parent roles to inherited roles.
  class RoleMap {
   public:
    explicit RoleMap(Store *store) : mapping_(store) {}

    // Adds role mapping.
    void Add(Handle parent, Handle role);

    // Appends another role map to this role map.
    void Append(const Frame &other);

    Slot *begin() { return mapping_.data(); }
    Slot *end() { return mapping_.data() + mapping_.size(); }

   private:
    // Mapping from parent role to inherited role.
    Slots mapping_;
  };

  // Predicate for sorting ancestors in handle order.
  static bool SortByHandle(const Handle &a, const Handle &b);

  // Store for schemata.
  Store *store_;

  // Symbols.
  Handle role_;
  Handle simple_;
  Handle ancestors_;
  Handle template_;
  Handle rolemap_;
};

}  // namespace sling

#endif  // SCHEMA_SCHEMATA_H_

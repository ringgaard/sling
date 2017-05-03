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

#include "schema/schemata.h"

#include <vector>

#include "frame/object.h"
#include "frame/store.h"
#include "string/text.h"

namespace sling {

SchemaFeatureStructure::SchemaFeatureStructure(Store *store, TypeSystem *types)
    : FeatureStructure(store) {
  // Set the type system for the schema template.
  SetTypeSystem(types);

  // Lookup symbols.
  role_ = store_->Lookup("role");
  target_ = store_->Lookup("target");
  simple_ = store_->Lookup("simple");
  binding_ = store_->Lookup("binding");
  equals_ = store_->Lookup("equals");
  assign_ = store_->Lookup("assign");
  hastype_ = store_->Lookup("hastype");
  self_ = store_->Lookup("self");
}

int SchemaFeatureStructure::ConstructSchema(Handle schema) {
  // Schema is represented as a frame.
  Frame type(store_, schema);

  // If this schema is currently under construction we do not apply the
  // schema constraints in order to avoid infinite expansion.
  for (Handle h : active_) {
    if (h == schema) {
      int node = AllocateContentNode(0);
      return node;
    }
  }
  active_.push_back(schema);

  // Create type node for schema.
  int node = AllocateContentNode(1);
  AddSlot(node, Handle::isa(), schema);

  // Unify with all the constraints.
  for (int i = 0; i < type.size(); ++i) {
    Handle name = type.name(i);
    Handle value = type.value(i);
    int constraint;
    if (name.IsIs()) {
      // Parent type constraint.
      constraint = ConstructSchema(value);
    } else if (name == role_) {
      FrameDatum *role = store_->GetFrame(value);

      // Apply role inheritance constraints.
      for (Slot *s = role->begin(); s < role->end(); ++s) {
        if (s->name.IsIs()) {
          // Create constraint that aliases the inherited role with its parent.
          int alias = ConstructAlias(role->self, s->value);
          if (alias == -1) return -1;

          // Unify alias constraint with node.
          node = Unify(node, alias);
          if (node == -1) return -1;
        }
      }

      // Check if role has type.
      Handle target = role->get(target_);
      if (target.IsNil()) continue;

      // Ignore simple type constraints.
      Handle simple = role->get(simple_);
      if (!simple.IsNil() && simple.IsTrue()) continue;

      // Typed role constraint.
      constraint = ConstructRole(value, target);
    } else if (name == binding_) {
      // Binding constraint.
      constraint = ConstructBinding(value);
    } else {
      // Ignore slots that are not constraints.
      continue;
    }

    // Check if constraint failed.
    if (constraint == -1) return -1;

    // Unify constraint with schema node.
    node = Unify(node, constraint);
    if (node == -1) return -1;
  }

  // Return schema node that has been unified with all the constraints.
  active_.pop_back();
  return node;
}

int SchemaFeatureStructure::ConstructRole(Handle role, Handle schema) {
  // Construct typed value node.
  int type = ConstructSchema(schema);
  if (type == -1) return -1;

  // Allocate node and add typed role.
  int node = AllocateContentNode(1);
  AddSlot(node, role, Handle::Index(type));
  return node;
}

int SchemaFeatureStructure::ConstructAlias(Handle role1, Handle role2) {
  // Create common node.
  int common = AllocateContentNode(0);

  // Create nodes for the two aliased roles.
  int node1 = AllocateContentNode(1);
  AddSlot(node1, role1, Handle::Index(common));
  int node2 = AllocateContentNode(1);
  AddSlot(node2, role2, Handle::Index(common));

  // Return unified node for alias.
  return Unify(node1, node2);
}

int SchemaFeatureStructure::ConstructBinding(Handle binding) {
  // Get the array for the binding.
  ArrayDatum *array = store_->GetArray(binding);

  // Find the operator. The operator cannot be the first or last element in the
  // array.
  int split = -1;
  for (int i = 1; i < array->length() - 1; ++i) {
    Handle e = array->get(i);
    if (e == equals_ || e == assign_ || e == hastype_) {
      split = i;
      break;
    }
  }
  if (split == -1) return -1;

  // Get operator and left and right argument.
  Handle op = array->get(split);
  Handle *left = array->begin();
  Handle *left_end = array->at(split);
  Handle *right = array->at(split + 1);
  Handle *right_end = array->end();

  // Make path for left argument.
  int left_tail;
  int left_head = ConstructPath(left, left_end, &left_tail);
  if (left_head == -1) return -1;

  // Construct binding based on the operator.
  if (op == equals_) {
    // Make path for right argument.
    int right_tail;
    int right_head = ConstructPath(right, right_end, &right_tail);
    if (right_head == -1) return -1;

    if (left_head == kSelf) {
      // Bind right path to self.
      if (right_head == kSelf) return -1;
      SlotAt(right_tail).value = Handle::Index(right_head);
      return right_head;
    } else if (right_head == kSelf) {
      // Bind left path to self.
      if (left_head == kSelf) return -1;
      SlotAt(left_tail).value = Handle::Index(left_head);
      return left_head;
    } else {
      // Make path for right argument.
      int right_tail;
      int right_head = ConstructPath(right, right_end, &right_tail);
      if (right_head == -1) return -1;

      // Create common node that both paths end up in.
      int common = AllocateContentNode(0);
      SlotAt(left_tail).value = Handle::Index(common);
      SlotAt(right_tail).value = Handle::Index(common);

      // Unify the two paths.
      return Unify(left_head, right_head);
    }
  } else if (op == assign_) {
    // There can only be one element in the right argument for assignment.
    if (right_end - right != 1) return -1;

    // Assign value to the tail slot.
    SlotAt(left_tail).value = *right;
    return left_head;
  } else if (op == hastype_) {
    // There can only be one element in the right argument for typing.
    if (right_end - right != 1) return -1;

    // Construct typed node.
    int type = ConstructSchema(*right);
    if (type == -1) return -1;

    // Assign typed node to tail slot.
    SlotAt(left_tail).value = Handle::Index(type);
    return left_head;
  } else {
    return -1;
  }
}

int SchemaFeatureStructure::ConstructPath(Handle *begin, Handle *end,
                                          int *role) {
  // There must be at least one element in the path;
  DCHECK(begin != end);

  // Check for self path.
  if (end - begin == 1 && *begin == self_) {
    *role = kSelf;
    return kSelf;
  }

  // Allocate chain of nodes.
  int head = AllocateContentNode(1);
  int tail = head;
  Handle *last = end - 1;
  for (Handle *e = begin; e < last; ++e) {
    int next = AllocateContentNode(1);
    AddSlot(tail, *e, Handle::Index(next));
    tail = next;
  }

  // Add empty role to the last node in the chain.
  *role = AddSlot(tail, *last);

  return head;
}

Schemata::Schemata(Store *store) : store_(store) {
  ancestors_ = store_->Lookup("ancestors");
  template_ = store_->Lookup("template");
  rolemap_ = store_->Lookup("rolemap");
  projections_ = store_->Lookup("projections");
  input_ = store_->Lookup("input");
  output_ = store_->Lookup("output");
  name_ = store_->Lookup("name");
  role_ = store_->Lookup("role");
}

Handle Schemata::Construct(Handle schema, Handle input) {
  // Get pre-compiled schema template.
  Handle tmpl = GetTemplate(schema);
  if (tmpl.IsNil()) return Handle::nil();

  // Initialize feature structure using the schemata type system.
  FeatureStructure fs(store_, tmpl);
  fs.SetTypeSystem(this);

  // Add input node to feature structure.
  int node = fs.AddFrame(input);

  // Unify input frame with schema template (node 0).
  int result = fs.Unify(node, 0);
  if (result == -1) return Handle::nil();

  // Trim result.
  fs.Trim(result);

  // Create frame(s) for the construction.
  return fs.Construct(result);
}

Handle Schemata::Project(Handle mapping, Handle input, bool destructive) {
  // Get pre-compiled mapping template.
  Handle tmpl = GetTemplate(mapping);
  if (tmpl.IsNil()) return Handle::nil();

  // Initialize feature structure using the schemata type system.
  FeatureStructure fs(store_, tmpl);
  fs.SetTypeSystem(this);

  // Add frame node to feature structure with a reference to the input frame and
  // and empty output node.
  int input_node = fs.AddFrame(input);
  int output_node = fs.AllocateContentNode(0);
  int node = fs.AllocateContentNode(2);
  fs.AddSlot(node, input_, Handle::Index(input_node));
  fs.AddSlot(node, output_, Handle::Index(output_node));

  // Unify with mapping template (node 0).
  int result = fs.Unify(node, 0);
  if (result == -1) return Handle::nil();

  // Trim result.
  fs.Trim(result);

  // Create frame(s) for the mapping.
  Handle handle = fs.Construct(result, destructive);
  if (handle.IsNil()) return Handle::nil();

  // Return the output of the mapping.
  return store_->GetFrame(handle)->get(output_);
}

bool Schemata::Subsumes(Handle supertype, Handle subtype) {
  // Check trivial case.
  if (supertype == subtype) return true;

  // Get ancestors for subtype.
  Handle h = GetAncestors(subtype);
  if (h.IsNil()) return false;
  ArrayDatum *array = store_->GetArray(h);

  // Check if supertype is in ancestors(subtype).
  for (const Handle *t = array->begin(); t < array->end(); t++) {
    if (*t == supertype) return true;
  }
  return false;
}

Handle Schemata::ResolveNamedRole(Handle schema, Text name) {
  // Get parents for schema. This includes the schema itself.
  Handle h = GetAncestors(schema);
  if (h.IsNil()) return GetNamedRole(schema, name);
  ArrayDatum *array = store_->GetArray(h);

  // Try to find match role in each of the parents.
  Handle matching_role = Handle::nil();
  Handle defining_schema = Handle::nil();
  for (const Handle *t = array->begin(); t < array->end(); t++) {
    // Look up named role in parent schema.
    Handle parent = *t;
    Handle role = GetNamedRole(parent, name);
    if (role.IsNil()) continue;

    // If we already have a match, pick the most specific role.
    if (defining_schema.IsNil() || Subsumes(defining_schema, parent)) {
      matching_role = role;
      defining_schema = parent;
    }
  }
  return matching_role;
}

Handle Schemata::GetNamedRole(Handle schema, Text name) {
  const FrameDatum *frame = store_->GetFrame(schema);
  for (const Slot *s = frame->begin(); s < frame->end(); ++s) {
    if (s->name != role_) continue;
    const FrameDatum *role = store_->GetFrame(s->value);
    Handle role_name = role->get(name_);
    if (role_name.IsNil()) continue;
    if (store_->GetString(role_name)->equals(name)) return s->value;
  }
  return Handle::nil();
}

Handle Schemata::GetRoleMap(Handle type) {
  return store_->GetFrame(type)->get(rolemap_);
}

SchemaCompiler::SchemaCompiler(Store *store) : store_(store) {
  simple_ = store_->Lookup("simple");
  role_ = store_->Lookup("role");
  ancestors_ = store_->Lookup("ancestors");
  template_ = store_->Lookup("template");
  rolemap_ = store_->Lookup("rolemap");
}

void SchemaCompiler::PreCompute() {
  PreCompute(store_->Lookup("global"));
}

void SchemaCompiler::PreCompute(Handle catalog) {
  // Lookup symbols.
  Handle s_catalog_schema_family = store_->Lookup("catalog_schema_family");
  Handle s_member_schema = store_->Lookup("member_schema");
  Handle s_precompute_templates = store_->Lookup("precompute_templates");
  Handle s_precompute_projections = store_->Lookup("precompute_projections");
  Handle s_precompute_rolemaps = store_->Lookup("precompute_rolemaps");
  Handle s_precompute_ancestors = store_->Lookup("precompute_ancestors");
  Handle s_projection = store_->Lookup("projection");
  Handle s_projections = store_->Lookup("projections");
  Handle s_input_schema = store_->Lookup("input_schema");

  // Run though catalog and determine which type of pre-processing to perform
  // on each schema.
  HandleMap<std::vector<Handle>> projections;
  for (auto &cs : Frame(store_, catalog)) {
    if (cs.name == s_catalog_schema_family) {
      Frame family(store_, cs.value);
      if (family.IsProxy()) continue;

      // Determine pre-processing for schema family.
      bool compute_templates = family.GetBool(s_precompute_templates);
      bool compute_projections = family.GetBool(s_precompute_projections);
      bool compute_rolemaps = family.GetBool(s_precompute_rolemaps);
      bool compute_ancestors = family.GetBool(s_precompute_ancestors);

      // Run though all schemas in the schema family.
      for (auto &fs : family) {
        if (fs.name != s_member_schema) continue;
        Frame schema(store_, fs.value);

        // Compute templates.
        if (compute_templates) Compile(schema.handle());

        // Compute ancestors.
        if (compute_ancestors) FindAncestors(schema.handle());

        // Compute role map.
        if (compute_rolemaps) GetRoleMap(schema.handle());

        // Compute projections.
        if (compute_projections) {
          // Find all projections in schema.
          for (auto &ss : schema) {
            if (ss.name == s_projection) {
              Frame projection(store_, ss.value);

              // Get the input schema for the projection.
              Handle source = projection.GetHandle(s_input_schema);
              if (source.IsNil()) continue;

              // Compile projection.
              Compile(projection.handle());

              // Add projection to the projection list for the input schema.
              auto &projection_list = projections[source];
              projection_list.push_back(projection.handle());
            }
          }
        }
      }
    }
  }

  // Update projection list for schemas that are inputs to mappings.
  for (const auto &it : projections) {
    Array array(store_, it.second);
    store_->Set(it.first, s_projections, array.handle());
  }
}

Handle SchemaCompiler::Compile(Handle schema) {
  // Pre-compute ancestor types for schema.
  FindAncestors(schema);

  // Pre-compute role map for schema.
  GetRoleMap(schema);

  // Do not pre-compile simple schemata.
  Handle simple = store_->GetFrame(schema)->get(simple_);
  if (!simple.IsNil() && simple.IsTrue()) return Handle::nil();

  // Create a schema feature structure for the schema. This uses the schema
  // compiler type system that does not assume that ancestor types have been
  // pre-compiled.
  SchemaFeatureStructure fs(store_, this);

  // Construct schema template.
  int node = fs.ConstructSchema(schema);
  if (node == -1) return Handle::nil();

  // Compact schema template.
  node = fs.Compact(node);

  // Create schema template in the object store.
  Frame tmpl(store_, fs.Template());

  // Add compiled template to schema.
  store_->Set(schema, template_, tmpl.handle());

  return tmpl.handle();
}

bool SchemaCompiler::SortByHandle(const Handle &a, const Handle &b) {
  return a.raw() < b.raw();
}

Handle SchemaCompiler::FindAncestors(Handle schema) {
  // Check if we have already computed the ancestors.
  Frame type(store_, schema);
  Handle ancestors = type.GetHandle(ancestors_);
  if (!ancestors.IsNil()) return ancestors;

  // Find all ancestor types.
  Handles types(store_);
  types.push_back(schema);
  for (int i = 0; i < types.size(); ++i) {
    // Find parent types.
    const FrameDatum *frame = store_->GetFrame(types[i]);
    for (const Slot *s = frame->begin(); s < frame->end(); ++s) {
      if (s->name.IsIs()) {
        // Check if parent type is already in the type set.
        Handle parent = s->value;
        bool found = false;
        for (Handle t : types) {
          if (t == parent) {
            found = true;
            break;
          }
        }
        if (!found) {
          // Add parent type to type set.
          types.push_back(parent);
        }
      }
    }
  }

  // Sort types by handle value.
  std::sort(types.begin(), types.end(), SortByHandle);

  // Create array with types.
  Array array(store_, types);

  // Add pre-computed ancestors to schema.
  store_->Set(schema, ancestors_, array.handle());

  // Return handle to ancestors array.
  return array.handle();
}

bool SchemaCompiler::Subsumes(Handle supertype, Handle subtype) {
  // Check trivial case.
  if (supertype == subtype) return true;

  // Get ancestors for subtype.
  Handle h = FindAncestors(subtype);
  if (h.IsNil()) return false;
  ArrayDatum *array = store_->GetArray(h);

  // Check if supertype is in ancestors(subtype).
  for (const Handle *t = array->begin(); t < array->end(); t++) {
    if (*t == supertype) return true;
  }
  return false;
}

Handle SchemaCompiler::GetRoleMap(Handle type) {
  // Check if we have already computed the role map.
  Frame schema(store_, type);
  Handle rolemap = schema.GetHandle(rolemap_);
  if (!rolemap.IsNil()) return rolemap;

  // Merge role maps from parent types.
  RoleMap mapping(store_);
  for (int i = 0;  i < schema.size(); ++i) {
    if (schema.name(i).IsIs()) {
      Handle parent = schema.value(i);
      Frame inherited(store_,  GetRoleMap(parent));
      mapping.Append(inherited);
    }
  }

  // Find all roles for schema.
  for (int i = 0;  i < schema.size(); ++i) {
    if (schema.name(i) == role_) {
      // Find all parent roles for role.
      Frame role(store_, schema.value(i));
      for (int j = 0; j < role.size(); ++j) {
        if (role.name(j).IsIs()) {
          // Add mapping from parent role to role in this schema.
          Handle parent = role.value(j);
          mapping.Add(parent, role.handle());
        }
      }
    }
  }

  // Sort role map by handle rank.
  std::sort(mapping.begin(), mapping.end(), FeatureStructure::SortByRole);

  // Create frame for role mapping.
  Frame frame(store_, mapping.begin(), mapping.end());

  // Add pre-computed role map to schema.
  store_->Set(type, rolemap_, frame.handle());

  // Return handle to ancestors array.
  return frame.handle();
}

void SchemaCompiler::RoleMap::Add(Handle parent, Handle role) {
  for (Slot &s : mapping_) {
    if (s.name == parent) {
      s.value = role;
      return;
    }
  }
  mapping_.emplace_back(parent, role);
}

void SchemaCompiler::RoleMap::Append(const Frame &other) {
  for (int i = 0; i < other.size(); ++i) {
    Add(other.name(i), other.value(i));
  }
}

}  // namespace sling


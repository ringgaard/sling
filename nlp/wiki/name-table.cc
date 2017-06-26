#include "nlp/wiki/name-table.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/unicode.h"

namespace sling {
namespace nlp {

void NameTable::Load(const string &filename) {
  // Load name repository from file.
  repository_.Read(filename);

  // Initialize name table.
  name_index_.Initialize(repository_);

  // Initialize entity table.
  repository_.FetchBlock("Entities", &entity_table_);
}

void NameTable::LookupPrefix(Text prefix,
                             int limit, int boost,
                             std::vector<Text> *matches) const {
  // Normalize prefix.
  string normalized;
  UTF8::Normalize(prefix.data(), prefix.size(), &normalized);
  Text normalized_prefix(normalized);

  // Find first name that is greater than or equal to the prefix.
  int lo = 0;
  int hi = name_index_.size() - 1;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    const NameItem *item = name_index_.GetName(mid);
    if (item->name() < normalized_prefix) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  // Find all names matching the prefix. Stop if we hit the limit.
  std::unordered_map<const EntityItem *, int> entities;
  int index = lo;
  while (index < name_index_.size()) {
    // Check if we have reached the limit.
    if (entities.size() > limit) break;

    // Stop if the current name does not match the prefix.
    const NameItem *item = name_index_.GetName(index);
    if (!item->name().starts_with(normalized_prefix)) break;

    // Add boost for exact match.
    int extra = 0;
    if (item->name().size() == normalized_prefix.size()) extra = boost;

    // Add matching entities.
    const EntityName *entity_names =  item->entities();
    for (int i = 0; i < item->num_entities(); ++i) {
      const EntityItem *entity = GetEntity(entity_names[i].offset);
      entities[entity] += entity_names[i].count + extra;
    }

    index++;
  }

  // Sort matching entities by decreasing frequency.
  std::vector<std::pair<uint32, const EntityItem *>> matching_entities;
  for (auto it : entities) {
    matching_entities.emplace_back(it.second, it.first);
  }
  std::sort(matching_entities.rbegin(), matching_entities.rend());

  // Copy matching entity ids to output.
  matches->clear();
  for (const auto &item : matching_entities) {
    if (matches->size() >= limit) break;
    matches->push_back(item.second->id());
  }
}

}  // namespace nlp
}  // namespace sling


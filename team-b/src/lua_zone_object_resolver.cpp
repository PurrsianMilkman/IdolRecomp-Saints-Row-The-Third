#include "sr3luahost/zone_object_resolver.h"

namespace sr3luahost {

void ZoneObjectResolver::ingest(const sr3zone::ObjectsSection& section) {
    for (const sr3zone::ObjectRecord& rec : section.records) {
        if (!rec.hasName || !rec.nameText.has_value() || rec.nameText->empty()) continue;
        byName_[AsciiLowerFold(*rec.nameText)] = rec;
    }
}

const sr3zone::ObjectRecord* ZoneObjectResolver::find(const std::string& name) const {
    auto it = byName_.find(AsciiLowerFold(name));
    if (it == byName_.end()) return nullptr;
    return &it->second;
}

} // namespace sr3luahost

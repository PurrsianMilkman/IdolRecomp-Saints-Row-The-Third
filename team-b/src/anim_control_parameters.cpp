// Animation runtime U0: the control-parameter registry and resolvers.
// Source: spec-anim-runtime.md Sec1-Sec2 only (see the header for the rules).
#include "sr3animruntime/control_parameters.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "sr3luahost/open_state.h"
#include "sr3xtbl/xtbl.h"

namespace sr3animruntime {

uint32_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

float BitsFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

namespace {

void requireType(const ParamRecord& r, ParamType t) {
    if (!r.valuesWritten)
        throw sr3luahost::OpenStateError("control-parameter record '" + r.name + "' +0x08..+0x10 (type 0)",
                                         "spec-anim-runtime.md Sec1.2: type 0 rows get nothing written there");
    if (r.type != t) throw std::logic_error("control parameter '" + r.name + "' is not of the requested type");
}

// 0x00dc5210 (spec-tables-environment.md Sec1.4): f32 from the element's own
// text, else the default; C runtime atof. atof(s) is defined as
// strtod(s, NULL); the engine stores the result as a 32-bit float.
float readAtofOr(const sr3xtbl::Node* row, std::string_view child, float dflt) {
    const std::string* t = sr3xtbl::ChildText(row, child);
    if (!t) return dflt;
    return static_cast<float>(std::strtod(t->c_str(), nullptr));
}

// 0x00dc5240 with default 0 (Sec1.2). Grammar per Sec7.3 (HIGH): the C
// runtime's base-0 conversion 0x00ea7ea9 - see the header.
int32_t readIntOr(const sr3xtbl::Node* row, std::string_view child, int32_t dflt) {
    const std::string* t = sr3xtbl::ChildText(row, child);
    if (!t) return dflt;
    return static_cast<int32_t>(std::strtol(t->c_str(), nullptr, 0));
}

}  // namespace

bool ParamRecord::defaultBool() const { requireType(*this, ParamType::Bool); return (defaultDword & 0xffu) != 0; }
bool ParamRecord::minBool() const { requireType(*this, ParamType::Bool); return (minDword & 0xffu) != 0; }
bool ParamRecord::maxBool() const { requireType(*this, ParamType::Bool); return (maxDword & 0xffu) != 0; }
int32_t ParamRecord::defaultInt() const { requireType(*this, ParamType::Integer); return static_cast<int32_t>(defaultDword); }
int32_t ParamRecord::minInt() const { requireType(*this, ParamType::Integer); return static_cast<int32_t>(minDword); }
int32_t ParamRecord::maxInt() const { requireType(*this, ParamType::Integer); return static_cast<int32_t>(maxDword); }
float ParamRecord::defaultFloat() const { requireType(*this, ParamType::Float); return BitsFloat(defaultDword); }
float ParamRecord::minFloat() const { requireType(*this, ParamType::Float); return BitsFloat(minDword); }
float ParamRecord::maxFloat() const { requireType(*this, ParamType::Float); return BitsFloat(maxDword); }

ControlParameterRegistry ControlParameterRegistry::load(const sr3xtbl::Document& doc) {
    ControlParameterRegistry reg;
    reg.loaded_ = true;
    const sr3xtbl::Node* wrap = sr3xtbl::FindChild(doc.root(), "control_parameters");
    const std::vector<const sr3xtbl::Node*> rows = sr3xtbl::Children(wrap, "control_parameter");
    if (rows.size() > 32767)
        throw std::length_error("control_parameters.xtbl: more rows than the signed 16-bit count holds");
    reg.records_.reserve(rows.size());
    for (const sr3xtbl::Node* row : rows) reg.records_.push_back(ReadControlParameterRow(row));
    return reg;
}

ParamRecord ReadControlParameterRow(const sr3xtbl::Node* row) {
    {
        ParamRecord r;
        const std::string* name = sr3xtbl::ChildText(row, "name");
        r.name = name ? *name : std::string("none");
        const std::string* typeText = sr3xtbl::ChildText(row, "data_type");
        const std::string_view typeName = typeText ? std::string_view(*typeText) : std::string_view("none");
        for (size_t i = 0; i < kParamTypeNames.size(); ++i) {
            if (sr3xtbl::NameEquals(typeName, kParamTypeNames[i])) {
                r.type = static_cast<ParamType>(i);
                break;
            }
        }
        switch (r.type) {
        case ParamType::Float:
            r.defaultDword = FloatBits(readAtofOr(row, "default_value", 0.0f));
            r.minDword = FloatBits(readAtofOr(row, "min_value", 0.0f));
            r.maxDword = FloatBits(readAtofOr(row, "max_value", 0.0f));
            r.valuesWritten = true;
            break;
        case ParamType::Integer:
            r.defaultDword = static_cast<uint32_t>(readIntOr(row, "default_value", 0));
            r.minDword = static_cast<uint32_t>(readIntOr(row, "min_value", 0));
            r.maxDword = static_cast<uint32_t>(readIntOr(row, "max_value", 0));
            r.valuesWritten = true;
            break;
        case ParamType::Bool: {
            const std::string* d = sr3xtbl::ChildText(row, "default_value");
            const std::string_view dv = d ? std::string_view(*d) : std::string_view("none");
            r.defaultDword = sr3xtbl::NameEquals(dv, "false") ? 0u : 1u;
            r.minDword = 0u;
            r.maxDword = 1u;
            r.valuesWritten = true;
            break;
        }
        case ParamType::None:
            break;
        }
        return r;
    }
}

ControlParameterRegistry ControlParameterRegistry::loadXml(std::string_view xmlBytes) {
    const sr3xtbl::Document doc = sr3xtbl::ParseDocument(xmlBytes);
    return load(doc);
}

int16_t ControlParameterRegistry::resolve(std::string_view name) const {
    for (size_t i = 0; i < records_.size(); ++i)
        if (sr3xtbl::NameEquals(records_[i].name, name)) return static_cast<int16_t>(i);
    return -1;
}

const ParamRecord* ControlParameterRegistry::record(int32_t id) const {
    if (id < 0 || id >= static_cast<int32_t>(records_.size())) return nullptr;
    return &records_[static_cast<size_t>(id)];
}

int16_t GraphAliasTable::resolveLocal(std::string_view name, const ControlParameterRegistry& registry) const {
    if (!present) return -1;
    for (size_t i = 0; i < globalIds.size(); ++i) {
        const ParamRecord* r = registry.record(globalIds[i]);
        if (!r) continue;  // CHOSEN: skip, where the engine would dereference null (Sec2.2 hazard)
        if (sr3xtbl::NameEquals(r->name, name)) return static_cast<int16_t>(i);
    }
    return -1;
}

int16_t GraphAliasTable::aliasToGlobal(int32_t alias) const {
    if (alias < 0 || alias >= static_cast<int32_t>(globalIds.size())) return -1;
    return globalIds[static_cast<size_t>(alias)];
}

int16_t GraphAliasTable::globalToAlias(int16_t id) const {
    for (size_t i = 0; i < globalIds.size(); ++i)
        if (globalIds[i] == id) return static_cast<int16_t>(i);
    return -1;
}

int16_t ResolveGraphScoped(const GraphAliasTable& aliases, std::string_view name,
                           const ControlParameterRegistry& registry) {
    const int16_t local = aliases.resolveLocal(name, registry);
    if (local < 0) return -1;
    return aliases.aliasToGlobal(local);
}

}  // namespace sr3animruntime

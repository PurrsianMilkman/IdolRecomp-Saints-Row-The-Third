#include "sr3geometry/character_matlib.h"

#include <cctype>

namespace sr3geometry {

namespace {

std::string toLowerAscii(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

bool endsWithCi(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return toLowerAscii(s.substr(s.size() - suffix.size())) == toLowerAscii(suffix);
}

}  // namespace

std::string ComposeMatlibFileName(const std::string& baseName) {
    // "strip extension, append the platform-selected one" (§12.1b,
    // 0x00da8790). `baseName` may or may not already carry an extension -
    // both are handled identically, since the real compose step always
    // produces a definite result regardless of what the input happened to
    // carry.
    std::string stem = baseName;
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem.resize(dot);
    return stem + ".matlib_pc";
}

CharacterMatlibAttachment ResolveCharacterMatlibAttachment(
    const std::string& identityName, const std::string& baseName,
    const std::function<bool(const std::string&, std::vector<uint8_t>&)>& loadByName) {
    CharacterMatlibAttachment out;
    out.identityName = identityName;
    out.type = CharacterResourceType::kMatlib;
    out.composedFileName = ComposeMatlibFileName(baseName);

    if (!loadByName) return out;  // no exists-check available - unresolved, not a guess

    std::vector<uint8_t> bytes;
    if (!loadByName(out.composedFileName, bytes)) return out;  // 0x00da90d0: does not exist

    // Attach: register the located bytes as this identity's own typed
    // (0x16) slot, parsed via the already-CONFIRMED general-purpose
    // MaterialBlock reader - not a new parser, not a per-texture-slot
    // splice into anything else.
    try {
        out.resource = MaterialBlock::parse(ByteView(bytes.data(), bytes.size()));
        out.locatedInArchive = true;
    } catch (const FormatError&) {
        // A real exists-check hit that doesn't parse as a MaterialBlock is
        // not reported as attached - see the header comment.
        out.locatedInArchive = false;
        out.resource = MaterialBlock{};
    }
    return out;
}

const std::string* PickMatlibTextureByRoleSuffix(const std::vector<std::string>& textureNames,
                                                  const std::string& partNameHint,
                                                  const std::vector<std::string>& suffixes) {
    const std::string hintLower = toLowerAscii(partNameHint);
    for (const std::string& name : textureNames) {
        if (!hintLower.empty() && toLowerAscii(name).find(hintLower) == std::string::npos) continue;
        // Strip the extension before checking the suffix, the same way
        // material_binding.h's own measured `_d`/`_dp`/`_n` convention is
        // stated (against the stem, not the raw ".tga"-suffixed name).
        std::string stem = name;
        const size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem.resize(dot);
        for (const std::string& suf : suffixes) {
            if (endsWithCi(stem, suf)) return &name;
        }
    }
    return nullptr;
}

}  // namespace sr3geometry

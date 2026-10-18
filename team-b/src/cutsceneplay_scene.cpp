// Cutscene scene model helpers - spec-cutscene-camera-format.md "In-game
// cutscene playback, end to end". See include/sr3cutsceneplay/scene.h.

#include "sr3cutsceneplay/scene.h"

#include <cctype>

namespace sr3cutsceneplay {

namespace {
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool EqualsIgnoreCase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return i == a.size() && b[i] == 0;
}
}  // namespace

std::vector<ShotSpan> ShotSpans(const Scene& scene) {
    std::vector<ShotSpan> out;
    int64_t frames = 0;
    for (const SceneShot& s : scene.shots) {
        ShotSpan sp;
        sp.start = static_cast<double>(frames) / kFramesPerSecond;
        frames += static_cast<int64_t>(s.definedShotLength) + 1;
        sp.end = static_cast<double>(frames) / kFramesPerSecond;
        out.push_back(sp);
    }
    return out;
}

double SceneDuration(const Scene& scene) {
    int64_t frames = 0;
    for (const SceneShot& s : scene.shots) frames += static_cast<int64_t>(s.definedShotLength) + 1;
    return static_cast<double>(frames) / kFramesPerSecond;
}

uint32_t WwiseId(const std::string& name) {
    uint32_t h = 2166136261u;
    for (char ch : name) {
        h *= 16777619u;
        h ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return h;
}

std::optional<SoundtrackSwitch> ParseSoundtrack(const std::string& text) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon < 1 || colon + 2 > text.size()) return std::nullopt;
    // One character dropped on each side of the ':' (the " : " form).
    const std::string group = text.substr(0, colon - 1);
    const std::string value = text.substr(colon + 2);
    if (group.empty() || value.empty()) return std::nullopt;
    SoundtrackSwitch s;
    s.group = group;
    s.value = value;
    s.groupId = WwiseId(group);
    s.valueId = WwiseId(value);
    s.bankFile = Lower(value) + ".bnk_pc";
    return s;
}

std::string BaseName(const std::string& path) {
    const size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string BinkMoviePath(const std::string& binkMovieFilename) {
    return "videos\\" + BaseName(binkMovieFilename);
}

std::string ScenePackageName(const std::string& sceneName, bool femalePlayer) {
    return sceneName + (femalePlayer ? "_f" : "") + ".str2_pc";
}

std::string ClipName(const std::string& filename) {
    std::string base = BaseName(filename);
    const std::string lower = Lower(base);
    const std::string ext = ".animx";
    if (lower.size() >= ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
        base.resize(base.size() - ext.size());
    return Lower(base);
}

std::string ResolveClipName(const std::string& filename, bool femaleOrForced,
                            const std::function<bool(const std::string&)>& clipExists) {
    const std::string plain = ClipName(filename);
    if (femaleOrForced && clipExists) {
        const std::string f = "f_" + plain;
        if (clipExists(f)) return f;
    }
    return plain;
}

bool IsPlayerActorName(const std::string& name) {
    return EqualsIgnoreCase(name, "Player") || EqualsIgnoreCase(name, "#Player#");
}

std::string SceneActorObjectName(const std::string& name, SceneKind kind, bool liveWorldObjectExists) {
    if (kind == SceneKind::Zscene && liveWorldObjectExists) return name + "_cs";
    return name;
}

std::string ShotActorObjectName(const std::string& name) {
    return EqualsIgnoreCase(name, "#PLAYER#") ? std::string("#PLAYER1#") : name;
}

std::string CharacterMesh(const SceneCharacter& c, const MeshContext& ctx) {
    if (c.isAngel) {
        if (ctx.missionM21Active) return "npc_Angel_clean";
        if (ctx.missionSh04Active) return "Npc_Angel_withmask";
        return "npc_Angel_nomask";
    }
    if (c.killbaneMesh && ctx.killbaneAlternate) return *c.killbaneMesh;
    return c.mesh;
}

}  // namespace sr3cutsceneplay

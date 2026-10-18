// lua_host_run helpers: file/string utilities and the two tools/*.tsv readers.
// Moved verbatim out of tools/lua_host_run.cpp's former anonymous namespace (see lua_host_run_support.h).
#include "lua_host_run_support.h"

namespace luahostrun {

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("could not open: " + path.string());
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("failed reading: " + path.string());
    return buf;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool endsWithLuaExt(const std::string& lowerName) {
    static const std::string ext = ".lua";
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

std::unordered_map<std::string, StaticRank> loadStaticReconciliation(const std::string& path) {
    std::unordered_map<std::string, StaticRank> out;
    std::ifstream f(path);
    if (!f) return out; // optional - tool still runs without it, just skips the cross-reference
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; } // header row
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string name, callsStr, scriptsStr, prefix, cluster;
        if (!std::getline(ss, name, '\t')) continue;
        if (!std::getline(ss, callsStr, '\t')) continue;
        if (!std::getline(ss, scriptsStr, '\t')) continue;
        StaticRank r;
        try {
            r.totalCallSites = std::stoull(callsStr);
            r.distinctScripts = std::stoull(scriptsStr);
        } catch (...) {
            continue;
        }
        out[name] = r;
    }
    return out;
}

// Real, top real Lua error messages by frequency (orchestrator's follow-up
// item, 2026-09-29): groups the real callError strings verdict_hook_fires_
// detail.tsv already captures by a NORMALIZED message text, so the SAME
// underlying error (e.g. "attempt to call a nil value (global 'foo')")
// recurring across many different scripts/hooks/lines counts as one
// bucket rather than one row per call site. Real, observed Lua 5.1
// behavior, not invented: a message raised from code executing INSIDE a
// loaded chunk is automatically prefixed by the VM with a source id and
// line number - luaL_loadbuffer's own chunk-name handling (this project
// passes a plain filename, never '='/'@'-prefixed) means Lua's real
// luaO_chunkid formats that id as `[string "name"]:line: ` - real,
// per-call-site noise for grouping purposes (the OWN chunk name embedded
// there tells you which SCRIPT currently owns the hook body that errored,
// a genuinely useful but DIFFERENT signal, not thrown away - it is still
// fully present, unstripped, in verdict_hook_fires_detail.tsv). A message
// raised directly by lua_pcall itself with no executing Lua frame to
// attribute it to (e.g. calling a nil global with no wrapping Lua call,
// the common Group-2-unconditional-and-undefined case) carries no such
// prefix at all and is left completely unchanged. Strips at most ONE
// leading `[string "..."]:<digits>: ` run; anything not matching that
// exact real shape is returned unchanged, never guessed at.
std::string stripLuaChunkLocationPrefix(const std::string& msg) {
    if (msg.size() < 2 || msg[0] != '[') return msg;
    size_t closeBracket = msg.find("]:");
    if (closeBracket == std::string::npos) return msg;
    size_t pos = closeBracket + 2;
    size_t digitsStart = pos;
    while (pos < msg.size() && msg[pos] >= '0' && msg[pos] <= '9') ++pos;
    if (pos == digitsStart || pos >= msg.size() || msg[pos] != ':') return msg;
    ++pos; // skip ':'
    if (pos < msg.size() && msg[pos] == ' ') ++pos;
    return msg.substr(pos);
}

std::string sanitizeTsv(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\t') c = ' ';
        if (c == '\n' || c == '\r') c = ' ';
    }
    return out;
}

// --- Step 1 (this task, after step 0 lands): real mission-driving pass
// helpers. -------------------------------------------------------------

// Manual tab-split (matching this file's own no-<regex> convention, same
// as loadStaticReconciliation() above) - tolerant of a missing trailing
// empty 9th field (other_entry_type_names, unused here) since
// std::getline does not emit a trailing empty token the way some TSV
// readers do.
std::vector<MissionPackageRow> loadMissionPackageRows(const std::string& path) {
    std::vector<MissionPackageRow> out;
    std::ifstream f(path);
    if (!f) return out; // real, checked negative if missing - reported by the caller, not silently assumed
    std::string line;
    bool first = true;
    while (std::getline(f, line)) {
        if (first) { first = false; continue; } // header row
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cols;
        std::istringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) cols.push_back(cell);
        if (cols.size() < 8) continue; // real, malformed-row skip (not expected against this file's own real output)
        MissionPackageRow r;
        r.containerName = cols[3];
        r.missionLuaEntryName = cols[5];
        r.missionLuaStem = cols[6];
        r.coversStartScript = (cols[7] == "1");
        out.push_back(std::move(r));
    }
    return out;
}

// Extracts NAME from a real Lua 5.1 "attempt to call global 'NAME' (a nil
// value)" error string (the same real error shape this file's own
// errorMessageCounts aggregate and §9.131's own dlc2_m02 analysis already
// rely on) - empty if the message doesn't match that exact real shape.
// Used to identify each mission's own first real unimplemented/undefined
// stub hit, in real encounter order - never a guess at which name "should"
// be missing.
std::string extractNilGlobalName(const std::string& msg) {
    static const std::string needle = "attempt to call global '";
    size_t pos = msg.find(needle);
    if (pos == std::string::npos) return std::string();
    size_t start = pos + needle.size();
    size_t end = msg.find('\'', start);
    if (end == std::string::npos) return std::string();
    return msg.substr(start, end - start);
}

} // namespace luahostrun

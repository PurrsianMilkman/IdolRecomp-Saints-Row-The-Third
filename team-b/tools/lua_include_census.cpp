// include( census for spec-lua-bindings.md Sec16.4 OPEN residue 1 (2026-10-01):
// does any shipped script call the bare global `include` (the deferred-include
// queue, Sec16.3) with a preload name (vint_lib, game_ui_globals, vdo_*,
// game_lib, system_lib)? Such a call in a gameplay-side script would load that
// file into the gameplay state. A plain text scan, not a parse: every
// `include` token (identifier boundary on both sides) followed by `(` or a
// string literal, outside a `--` line comment. Only the argument (a script
// name or "<non-literal>") and line numbers are written, no script text.
//
// Usage: lua_include_census <cache_dir> <out_dir>
#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "vpp/container.h"

namespace fs = std::filesystem;

namespace {

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

bool endsWithExt(const std::string& lowerName, const std::string& ext) {
    if (lowerName.size() < ext.size()) return false;
    return lowerName.compare(lowerName.size() - ext.size(), ext.size(), ext) == 0;
}

struct ScriptInstance {
    std::string archive;
    std::string pathChain;
    std::string entryName;
    std::string content;
};

struct WalkState {
    std::deque<std::vector<uint8_t>> bufferPool;
    std::vector<ScriptInstance> found;
};

void inspectLeaf(WalkState& st, const std::string& archive, const std::string& chain, const std::string& name,
                  const uint8_t* data, size_t len) {
    std::string lower = toLower(name);
    if (endsWithExt(lower, ".lua")) {
        ScriptInstance si;
        si.archive = archive;
        si.pathChain = chain;
        si.entryName = name;
        si.content.assign(reinterpret_cast<const char*>(data), len);
        st.found.push_back(std::move(si));
    }
}

void walk(WalkState& st, const vpp::Container& c, const std::string& archive, const std::string& chain) {
    const auto& entries = c.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::string name = e.name.empty() ? ("<unnamed hash=" + std::to_string(e.nameHash) + ">") : e.name;
        std::string childChain = chain.empty() ? name : (chain + " > " + name);

        if (e.payload.kind == vpp::PayloadKind::Raw) {
            bool nested = false;
            try {
                vpp::Container child = c.openNested(i);
                nested = true;
                walk(st, child, archive, childChain);
            } catch (const std::exception&) {
                nested = false;
            }
            if (!nested) {
                vpp::ByteView raw = c.rawEntryBytes(i);
                inspectLeaf(st, archive, childChain, name, raw.data(), raw.size());
            }
        } else {
            vpp::DecompressResult result = c.decompressEntry(i);
            if (result.status == vpp::DecodeStatus::Ok || result.status == vpp::DecodeStatus::SizeMismatch) {
                bool nested = false;
                if (result.data.size() >= 4) {
                    uint32_t magic = (uint32_t)result.data[0] | ((uint32_t)result.data[1] << 8) |
                                      ((uint32_t)result.data[2] << 16) | ((uint32_t)result.data[3] << 24);
                    if (magic == 0x51890ACEu) {
                        st.bufferPool.push_back(std::move(result.data));
                        try {
                            vpp::Container child(vpp::ByteView(st.bufferPool.back().data(), st.bufferPool.back().size()));
                            nested = true;
                            walk(st, child, archive, childChain);
                        } catch (const std::exception&) {
                            nested = false;
                        }
                        if (!nested) {
                            const auto& buf = st.bufferPool.back();
                            inspectLeaf(st, archive, childChain, name, buf.data(), buf.size());
                        }
                        continue;
                    }
                }
                inspectLeaf(st, archive, childChain, name, result.data.data(), result.data.size());
            }
        }
    }
}


bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// The literal argument of an include call starting after `include` at `p`, or
// "<non-literal>". Handles include("x"), include "x", include('x').
std::string includeArg(const std::string& s, size_t p, bool& isCall) {
    isCall = false;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    bool paren = p < s.size() && s[p] == '(';
    if (paren) {
        ++p;
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    }
    if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
        char q = s[p];
        size_t e = s.find(q, p + 1);
        size_t nl = s.find('\n', p + 1);
        if (e != std::string::npos && (nl == std::string::npos || e < nl)) {
            isCall = true;
            return s.substr(p + 1, e - p - 1);
        }
    }
    if (paren) {
        isCall = true;
        return "<non-literal>";
    }
    return "";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: lua_include_census <cache_dir> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    fs::path outDir = argv[2];
    fs::create_directories(outDir);
    const std::vector<std::string> preloadStems = {"system_lib", "vint_lib", "game_ui_globals", "vdo_base_object",
                                                   "vdo_anim_object", "vdo_input_tracker", "game_lib"};

    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());
    WalkState st;
    for (auto& archivePath : archives) {
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archivePath.filename().string(), "");
        } catch (const std::exception&) {
        }
    }

    std::ofstream out(outDir / "include_calls.tsv");
    out << "archive\tpath_chain\tentry_name\tline\targument\tis_preload_name\n";
    size_t calls = 0, preloadCalls = 0, scriptsWithCalls = 0;
    for (const auto& s : st.found) {
        bool any = false;
        size_t pos = 0;
        while ((pos = s.content.find("include", pos)) != std::string::npos) {
            size_t end = pos + 7;
            bool boundary = (pos == 0 || !isIdent(s.content[pos - 1])) && (end >= s.content.size() || !isIdent(s.content[end]));
            if (pos > 0 && s.content[pos - 1] == '.') boundary = false; // a field, not the global
            size_t lineStart = s.content.rfind('\n', pos);
            lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            bool commented = s.content.substr(lineStart, pos - lineStart).find("--") != std::string::npos;
            bool isCall = false;
            std::string arg = boundary && !commented ? includeArg(s.content, end, isCall) : "";
            if (isCall) {
                size_t line = 1 + static_cast<size_t>(std::count(s.content.begin(), s.content.begin() + pos, '\n'));
                std::string stem = toLower(arg);
                if (endsWithExt(stem, ".lua")) stem.resize(stem.size() - 4);
                bool pre = std::find(preloadStems.begin(), preloadStems.end(), stem) != preloadStems.end();
                out << s.archive << '\t' << s.pathChain << '\t' << s.entryName << '\t' << line << '\t' << arg << '\t'
                    << (pre ? 1 : 0) << '\n';
                ++calls;
                preloadCalls += pre;
                any = true;
            }
            pos = end;
        }
        scriptsWithCalls += any;
    }
    std::cout << "archives=" << archives.size() << "\n";
    std::cout << "real_lua_scripts_scanned=" << st.found.size() << "\n";
    std::cout << "include_calls=" << calls << " in " << scriptsWithCalls << " scripts\n";
    std::cout << "include_calls_naming_a_preload=" << preloadCalls << "\n";
    std::cout << "Wrote " << (outDir / "include_calls.tsv").string() << "\n";
    return 0;
}

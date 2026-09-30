// Non-CALL, non-DEFINE reference census, requested by the orchestrator
// 2026-09-29 as a third follow-up to Sec9.118 (definitions)/Sec9.121
// (call sites): does any real script reference a "<stem>_start" NAME as
// a plain VALUE - a table field (`{start = m01_start}`), a bare function
// argument (`some_engine_fn(m01_start)`), an assignment RHS
// (`local f = m01_start`) - without calling it? Neither prior census
// would catch this: Sec9.118/Sec9.121 both used sr3lua::Parser, which
// only records CALL sites and DEFINE sites, not bare name reads.
//
// Method, stated plainly as pattern-matching, not full AST tracking:
// for each real script, run BOTH sr3lua::Parser::analyze() (for the
// real, scope-aware CallSite/GlobalDefine positions - authoritative)
// AND a raw sr3lua::Lexer token walk over the SAME source (for every
// Name token's own line/col). For every Name token whose text exactly
// equals one of the 63 real "<stem>_start" full names (Sec9.118's own
// population), check whether its (line, col) matches a recorded
// CallSite or GlobalDefine position for that exact name in that script.
// If it matches neither, it is a real, non-call, non-define occurrence -
// reported with surrounding raw-text context for a human/peer to
// classify further (table field, argument, assignment, etc. - this tool
// does not itself classify WHICH kind, only that it exists and is
// neither a call nor a definition).
//
// Usage: lua_bare_reference_census <cache_dir> <names_file> <out_dir>
//   names_file: one "<stem>_start" full name per line (the real 63 from
//   Sec9.118's own entrypoint_per_script.tsv, suffix==start).

#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "sr3lua/lexer.h"
#include "sr3lua/parser.h"
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

bool endsWithLuaExt(const std::string& lowerName) {
    static const std::string ext = ".lua";
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
    if (endsWithLuaExt(lower)) {
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

std::vector<std::string> readNeedles(const fs::path& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }
    return out;
}

// Byte offset of a given 1-based (line, col) within source - used to pull
// surrounding context text for a report row.
size_t lineColToOffset(const std::string& src, int line, int col) {
    int curLine = 1;
    size_t i = 0;
    while (i < src.size() && curLine < line) {
        if (src[i] == '\n') ++curLine;
        ++i;
    }
    return i + static_cast<size_t>(col - 1);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: lua_bare_reference_census <cache_dir> <names_file> <out_dir>\n";
        return 1;
    }
    fs::path cacheDir = argv[1];
    std::vector<std::string> names = readNeedles(argv[2]);
    std::set<std::string> nameSet(names.begin(), names.end());
    fs::path outDir = argv[3];
    fs::create_directories(outDir);

    std::cout << "Loaded " << names.size() << " target _start names.\n";

    std::vector<fs::path> archives;
    for (auto& ent : fs::directory_iterator(cacheDir)) {
        if (!ent.is_regular_file()) continue;
        if (toLower(ent.path().extension().string()) == ".vpp_pc") archives.push_back(ent.path());
    }
    std::sort(archives.begin(), archives.end());

    WalkState st;
    for (auto& archivePath : archives) {
        std::string archiveName = archivePath.filename().string();
        std::vector<uint8_t> bytes;
        try {
            bytes = readFile(archivePath);
        } catch (const std::exception&) {
            continue;
        }
        try {
            vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
            walk(st, root, archiveName, "");
        } catch (const std::exception&) {
        }
    }
    std::cout << "Archive walk complete: " << archives.size() << " archives, " << st.found.size()
              << " real .lua entries found.\n";

    std::ofstream out(outDir / "bare_references.tsv");
    out << "archive\tpath_chain\tentry_name\tname\tline\tcol\tcontext\n";

    uint64_t totalNameTokens = 0, totalCallOrDefine = 0, totalBareRef = 0;
    uint64_t parseFailures = 0, lexFailures = 0;

    for (const auto& s : st.found) {
        // Quick pre-filter: skip scripts whose raw text doesn't contain
        // ANY target name at all (cheap substring check before the more
        // expensive parse+lex pass).
        bool mightContain = false;
        for (const auto& n : names) {
            if (s.content.find(n) != std::string::npos) { mightContain = true; break; }
        }
        if (!mightContain) continue;

        // Authoritative call/define positions (line,col) per name, from
        // the real parser - this is the filter set.
        std::set<std::pair<int, int>> knownPositions;
        try {
            sr3lua::ScriptAnalysis analysis = sr3lua::Parser::analyze(s.content);
            for (const auto& call : analysis.calls) {
                if (nameSet.count(call.name)) knownPositions.insert({call.line, call.col});
            }
            for (const auto& def : analysis.defines) {
                if (nameSet.count(def.name)) knownPositions.insert({def.line, def.col});
            }
        } catch (const sr3lua::LuaSyntaxError&) {
            ++parseFailures;
            continue;
        }

        // Raw token walk for every Name token matching a target name.
        try {
            sr3lua::Lexer lexer(s.content);
            while (true) {
                sr3lua::Token tok = lexer.next();
                if (tok.type == sr3lua::TokType::Eof) break;
                if (tok.type != sr3lua::TokType::Name) continue;
                if (!nameSet.count(tok.text)) continue;
                ++totalNameTokens;
                if (knownPositions.count({tok.line, tok.col})) {
                    ++totalCallOrDefine;
                    continue;
                }
                ++totalBareRef;
                size_t offset = lineColToOffset(s.content, tok.line, tok.col);
                size_t ctxStart = (offset > 50) ? offset - 50 : 0;
                size_t ctxLen = std::min<size_t>(tok.text.size() + 100, s.content.size() - ctxStart);
                std::string context = s.content.substr(ctxStart, ctxLen);
                for (char& c : context) {
                    if (c == '\t') c = ' ';
                    if (c == '\n' || c == '\r') c = ' ';
                }
                out << s.archive << "\t" << s.pathChain << "\t" << s.entryName << "\t" << tok.text << "\t"
                    << tok.line << "\t" << tok.col << "\t" << context << "\n";
            }
        } catch (const sr3lua::LuaSyntaxError&) {
            ++lexFailures;
            continue;
        }
    }
    out.close();

    std::cout << "\n=== RESULT (lua_bare_reference_census) ===\n";
    std::cout << "real_lua_scripts_scanned=" << st.found.size() << "\n";
    std::cout << "sr3lua_parse_failures=" << parseFailures << "\n";
    std::cout << "lexer_failures=" << lexFailures << "\n";
    std::cout << "total_target_name_tokens_found=" << totalNameTokens << "\n";
    std::cout << "matched_a_known_call_or_define_position=" << totalCallOrDefine << "\n";
    std::cout << "BARE_REFERENCES(neither call nor define)=" << totalBareRef << "\n";
    std::cout << "Wrote " << (outDir / "bare_references.tsv").string() << "\n";
    return 0;
}

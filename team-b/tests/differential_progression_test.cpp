// Team D's differential vectors against sr3progression (Team C): every row of
// spec-differential-vectors-progression.md V1-V5 and spec-differential-vectors-cash.md, read from
// the spec files at run time (the "expected" column is the original game's output).
// V6 (the container name hash) is not Team C code and is not run here.
// Usage: differential_progression_tests <repo root>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sr3progression/respect.h"

namespace {

using namespace sr3progression;

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    const size_t b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Section name ("V1", ..., or "cash") -> data rows (cells).
std::map<std::string, std::vector<std::vector<std::string>>> ReadTables(const std::string& path, bool cash) {
    std::map<std::string, std::vector<std::vector<std::string>>> out;
    std::ifstream in(path);
    std::string line, section = cash ? "cash" : "";
    bool header = false;
    while (std::getline(in, line)) {
        if (!cash && line.rfind("## V", 0) == 0) {
            section = line.substr(3, line.find('.') - 3);
            header = false;
            continue;
        }
        if (line.empty() || line[0] != '|') {
            header = false;
            continue;
        }
        if (!header) {  // the header row, then the separator
            header = true;
            std::getline(in, line);
            continue;
        }
        std::vector<std::string> cells;
        std::stringstream ss(line.substr(1));
        std::string c;
        while (std::getline(ss, c, '|')) cells.push_back(Trim(c));
        if (!cells.empty() && cells.back().empty()) cells.pop_back();
        out[section].push_back(cells);
    }
    return out;
}

int g_run = 0, g_fail = 0;
void Report(const std::string& what, const std::string& got, const std::string& want) {
    ++g_run;
    if (got != want) {
        if (++g_fail <= 40) std::cerr << "MISMATCH " << what << ": got '" << got << "', expected '" << want << "'\n";
    }
}

RespectTable TableFrom(const std::string& spec) {
    if (spec == "shipped") return RespectTable::ShippedCosts();
    std::vector<RespectLevelRow> rows;
    std::stringstream ss(spec);
    std::string c;
    while (std::getline(ss, c, ',')) {
        RespectLevelRow r;
        r.respect = std::stoi(c);
        rows.push_back(r);
    }
    return RespectTable(rows);
}

std::map<std::string, std::string> KeyValues(const std::string& s) {
    std::map<std::string, std::string> kv;
    std::stringstream ss(s);
    std::string tok;
    while (ss >> tok) {
        const size_t e = tok.find('=');
        if (e != std::string::npos) kv[tok.substr(0, e)] = tok.substr(e + 1);
    }
    return kv;
}

std::string AwardLine(const AwardResult& a, const PlayerRespect& p) {
    std::string lv;
    for (size_t i = 0; i < a.levelUps.size(); ++i) lv += (i ? "," : "") + std::to_string(a.levelUps[i].level);
    char buf[256];
    std::snprintf(buf, sizeof buf, "%d %d %d %d lv=%s hint=%s cash=%lld net=%d", a.returned, p.total, p.inLevel,
                  p.level, lv.c_str(),
                  a.respectHint ? (std::to_string(a.respectHintAmount) + "/" + std::to_string(a.respectHintLevel)).c_str()
                                : "none",
                  static_cast<long long>(a.overflowCashRaw), a.sentToNetwork ? 1 : 0);
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: differential_progression_tests <repo root>\n";
        return 1;
    }
    const std::string root = argv[1];
    auto prog = ReadTables(root + "/spec-differential-vectors-progression.md", false);
    auto cash = ReadTables(root + "/spec-differential-vectors-cash.md", true);
    if (prog["V1"].empty() || cash["cash"].empty()) {
        std::cerr << "vector files not found or empty under " << root << "\n";
        return 1;
    }

    for (const auto& r : prog["V1"])  // multiplier | amount | expected
        Report("V1 " + r[0] + " x " + r[1], std::to_string(ScaleByMultiplier(std::stof(r[0]), std::stoi(r[1]))), r[2]);

    const KillRespectTable kills;  // 10 for every rank
    for (const auto& r : prog["V2"])  // rank | gang | excluded | factor | expected
        Report("V2 rank " + r[0] + " f " + r[3],
               std::to_string(KillRespect(kills, std::stoi(r[0]), r[1] == "1", r[2] == "1", std::stof(r[3]))), r[4]);

    for (const auto& r : prog["V3"]) {  // fraction | MaxR | MaxLife | MaxCash | reached | "respect cash"
        const int32_t maxLife = std::stoi(r[2]);
        const DiversionReward d = DiversionRewardFor(std::stof(r[0]), std::stoi(r[1]), maxLife, std::stoi(r[3]),
                                                     r[4] == "1" ? maxLife : 0);
        Report("V3 f " + r[0] + " " + r[4], std::to_string(d.respect) + " " + std::to_string(d.cashDollars), r[5]);
    }

    const RespectTable shipped = RespectTable::ShippedCosts();
    for (const auto& r : prog["V4"]) {  // (total, inLevel, L) | respect | cash $ | multiplier | expected
        PlayerRespect p;
        std::sscanf(r[0].c_str(), "(%d, %d, %d)", &p.total, &p.inLevel, &p.level);
        RewardToast toast;
        toast.respect = std::stoi(r[1]);
        toast.cashDollars = std::stof(r[2]);
        AppliedUnlocks applied;
        const ToastFinalisation t = FinaliseRewardToast(toast, std::stof(r[3]), p, shipped, applied);
        char buf[200];
        std::snprintf(buf, sizeof buf, "%d %d %d %d overflowCash=%lld toastCash=%lld zeroHint=%d", t.award.granted,
                      p.total, p.inLevel, p.level, static_cast<long long>(t.award.overflowCashRaw),
                      static_cast<long long>(t.cashRaw), t.zeroGrantHint ? 1 : 0);
        Report("V4 " + r[0] + " " + r[1] + "/" + r[2] + " x" + r[3], buf, r[4]);
    }

    for (const auto& r : prog["V5"]) {  // key=value input | expected
        const auto kv = KeyValues(r[0]);
        PlayerRespect p;
        p.total = std::stoi(kv.at("total"));
        p.inLevel = std::stoi(kv.at("inLevel"));
        p.level = std::stoi(kv.at("L"));
        AwardContext ctx;
        ctx.hordeMode = kv.at("horde") == "1";
        ctx.locallyOwned = kv.at("owned") == "1";
        AppliedUnlocks applied;
        const AwardResult a = AwardRespect(p, std::stoi(kv.at("amount")), TableFrom(kv.at("table")), applied, ctx);
        Report("V5 " + r[0], AwardLine(a, p), r[1]);
    }

    for (const auto& r : cash["cash"])  // before | add | multiplier | expected
        Report("cash " + r[0] + " + " + r[1] + " x" + r[2],
               std::to_string(AddCashToWallet(std::stoll(r[0]), std::stoll(r[1]), std::stof(r[2]))), r[3]);

    std::cout << g_run << " vectors run (V1 " << prog["V1"].size() << ", V2 " << prog["V2"].size() << ", V3 "
              << prog["V3"].size() << ", V4 " << prog["V4"].size() << ", V5 " << prog["V5"].size() << ", cash "
              << cash["cash"].size() << "), " << g_fail << " mismatch(es)\n";
    return g_fail == 0 ? 0 : 1;
}

// Respect table, award routine and level-up loop - spec-tables-progression.md
// section 5 and B.1.1/B.2. See include/sr3progression/respect.h.

#include "sr3progression/respect.h"

#include <algorithm>
#include <utility>

namespace sr3progression {

RespectTable::RespectTable(std::vector<RespectLevelRow> rows) : rows_(std::move(rows)) {
    if (rows_.size() > static_cast<size_t>(kMaxRespectLevels)) rows_.resize(kMaxRespectLevels);
    int64_t sum = 0;
    for (const RespectLevelRow& r : rows_) sum += r.respect;
    if (!rows_.empty()) sum -= rows_.back().respect;
    cap_ = static_cast<int32_t>(sum);
}

RespectTable RespectTable::FromParsed(const std::vector<sr3tables_progression::RespectLevel>& rows) {
    std::vector<RespectLevelRow> out;
    for (const sr3tables_progression::RespectLevel& r : rows)
        out.push_back({r.respect.value, r.unlockables});
    return RespectTable(std::move(out));
}

RespectTable RespectTable::ShippedCosts() {
    std::vector<RespectLevelRow> rows;
    for (int n = 1; n <= 49; ++n) rows.push_back({2115 + 115 * (n - 1), {}});
    rows.push_back({0, {}});
    return RespectTable(std::move(rows));
}

int32_t RespectTable::cost(int level) const {
    if (level >= kMaxRespectLevels) return kCostPastLastLevel;
    if (level < 0 || level >= levelCount()) return 0;
    return rows_[static_cast<size_t>(level)].respect;
}

int64_t RespectTable::cumulative(int level) const {
    int64_t sum = 0;
    const int n = std::min(level, levelCount());
    for (int i = 0; i < n; ++i) sum += rows_[static_cast<size_t>(i)].respect;
    return sum;
}

int64_t Wrap32(int64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v))); }

std::vector<std::string> ApplyLevelUnlocks(int level, const RespectTable& table, AppliedUnlocks& applied) {
    std::vector<std::string> fresh;
    if (level < 0 || level >= table.levelCount()) return fresh;
    for (const std::string& id : table.record(level).unlockables)
        if (applied.insert(id).second) fresh.push_back(id);
    return fresh;
}

std::vector<LevelUp> ApplyLevelUp(PlayerRespect& p, int32_t grant, const RespectTable& table,
                                  AppliedUnlocks& applied, const AwardContext& ctx) {
    std::vector<LevelUp> ups;
    const int lastLevel = table.levelCount() - 1;
    int64_t r = Wrap32(static_cast<int64_t>(p.inLevel) + grant);  // 32-bit sum (spec-differential D-4)
    int32_t c = table.cost(p.level);
    if (c > 1 && r >= c) {
        for (;;) {
            ++p.level;
            LevelUp u;
            u.level = p.level;
            u.milestone = ctx.inCityZone && p.level % 10 == 0;
            u.newlyApplied = ApplyLevelUnlocks(p.level, table, applied);
            u.levelUpHint = !u.newlyApplied.empty() || ctx.forceLevelUpHint;
            ups.push_back(std::move(u));
            r -= c;
            c = table.cost(p.level);
            if (p.level >= lastLevel) {
                r = 0;
                break;
            }
            if (c <= 1 || r < c) break;  // a needed value <= 1 blocks levelling (5.4 note, 2026-10-20)
        }
    }
    p.inLevel = static_cast<int32_t>(std::max<int64_t>(r, 0));
    return ups;
}

int64_t DollarsToRawCash(int64_t dollars) {
    const int64_t raw = dollars * 100;
    return std::clamp<int64_t>(raw, -2000000000LL, 2000000000LL);
}

AwardResult AwardRespect(PlayerRespect& p, int32_t amount, const RespectTable& table,
                         AppliedUnlocks& applied, const AwardContext& ctx) {
    AwardResult res;
    if (amount < 1 || ctx.hordeMode) return res;
    if (!ctx.locallyOwned) {
        res.sentToNetwork = true;
        res.returned = amount;
        return res;
    }
    const int32_t cap = table.cap();
    if (p.total >= cap || p.level >= table.levelCount() - 1) {
        res.overflowCashRaw = DollarsToRawCash(amount);
        return res;
    }
    // The sums are signed 32-bit (spec-differential-results D-4): a total + amount that wraps negative
    // skips the cap clamp, grants the full amount and leaves the total floored at 0.
    int32_t grant = amount;
    if (Wrap32(static_cast<int64_t>(p.total) + amount) >= cap) grant = cap - p.total;
    p.total = static_cast<int32_t>(std::max<int64_t>(Wrap32(static_cast<int64_t>(p.total) + grant), 0));
    res.levelUps = ApplyLevelUp(p, grant, table, applied, ctx);
    res.granted = grant;
    res.returned = grant;
    res.respectHint = true;
    res.respectHintAmount = grant;
    res.respectHintLevel = p.level + 1;
    return res;
}

PlayerRespect RespectStateFromTotal(int32_t total, const RespectTable& table) {
    PlayerRespect p;
    AppliedUnlocks scratch;
    AwardRespect(p, total, table, scratch);
    return p;
}

bool RespectStateConsistent(const PlayerRespect& p, const RespectTable& table) {
    return static_cast<int64_t>(p.total) - p.inLevel == table.cumulative(p.level);
}

}  // namespace sr3progression

// Respect, level-up and the free-roam respect grants -
// spec-tables-progression.md "Free-roam respect grants and level-up flow
// (2026-10-14)" B.1-B.3, with section 5 (respect_levels.xtbl). Pure logic:
// the player's three respect fields, the unlock state and the cash wallet
// are plain values owned by the caller.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "sr3tables_progression/tables_core.h"
#include "sr3tables_progression/tables_world.h"

namespace sr3progression {

// ---------------------------------------------------------------------------
// respect_levels.xtbl
// ---------------------------------------------------------------------------

constexpr int kMaxRespectLevels = 50;         // the loader stores at most 50 records
constexpr int32_t kCostPastLastLevel = 22000; // record n's cost for n >= 50

struct RespectLevelRow {
    int32_t respect = 0;                    // cost to leave this displayed level
    std::vector<std::string> unlockables;   // granted on arriving at this row
};

class RespectTable {
public:
    RespectTable() = default;
    // Rows past 50 are ignored, as the loader stops storing at 50.
    explicit RespectTable(std::vector<RespectLevelRow> rows);
    // From the parsed respect_levels.xtbl (load unlockables.xtbl first in a
    // real host; names are kept as given here).
    static RespectTable FromParsed(const std::vector<sr3tables_progression::RespectLevel>& rows);
    // The shipped table's costs (B.2): row n = 2115 + 115(n - 1) for
    // n = 1..49, row 50 = 0; no unlockable names.
    static RespectTable ShippedCosts();

    int levelCount() const { return static_cast<int>(rows_.size()); }
    const RespectLevelRow& record(int i) const { return rows_[static_cast<size_t>(i)]; }
    // Record L's Respect: the cost of the next level from internal level L.
    // 22000 for L >= 50; 0 for a record past the loaded count below 50.
    int32_t cost(int level) const;
    // Sum of the first L records (capped at the loaded count): the total
    // respect at which internal level L starts.
    int64_t cumulative(int level) const;
    // Sum of every record minus the last record: the lifetime cap.
    int32_t cap() const { return cap_; }

private:
    std::vector<RespectLevelRow> rows_;
    int32_t cap_ = 0;
};

// ---------------------------------------------------------------------------
// The player's respect state and the award routine (B.1.1, B.2)
// ---------------------------------------------------------------------------

struct PlayerRespect {
    int32_t total = 0;    // save 0x4418
    int32_t inLevel = 0;  // save 0x441C
    int32_t level = 0;    // save 0x4420, internal L; displayed level = L + 1
};

// Unlockables already applied (the live "applied" flag per unlockable).
using AppliedUnlocks = std::set<std::string>;

struct AwardContext {
    bool hordeMode = false;
    bool locallyOwned = true;   // false: the award is sent as a network message
    bool inCityZone = true;     // the milestone hook acts only in sr3_city
    bool forceLevelUpHint = false;  // the global that forces hint 0xA9
};

// HUD hints the award posts.
constexpr int kHintRespectGain = 0x44;  // "+N Respect", second argument = L + 1
constexpr int kHintLevelUp = 0xA9;
constexpr int kMilestoneEvent = 0x22;   // decade levels, sr3_city only

struct LevelUp {
    int level = 0;  // the new internal level L
    std::vector<std::string> newlyApplied;  // record L's unlockables applied now
    bool milestone = false;                 // event 0x22 (L % 10 == 0, in the city)
    bool levelUpHint = false;               // hint 0xA9 posted
};

struct AwardResult {
    int32_t returned = 0;        // the routine's return value
    int32_t granted = 0;         // respect added to the total
    bool sentToNetwork = false;  // not locally owned
    int64_t overflowCashRaw = 0; // respect converted 1:1 to dollars, x100, clamped
    std::vector<LevelUp> levelUps;
    bool respectHint = false;    // hint 0x44 posted
    int32_t respectHintAmount = 0;
    int32_t respectHintLevel = 0;  // L + 1 after the award
};

// The award routine: guards, overflow to cash at the cap or at the last
// level, clamp to the cap, add, level-up loop, hint 0x44. `source` is only
// reported to telemetry by the game, so it is not taken here.
AwardResult AwardRespect(PlayerRespect& p, int32_t amount, const RespectTable& table,
                         AppliedUnlocks& applied, const AwardContext& ctx = {});

// The level-up loop alone (applied after the total was raised by `grant`).
std::vector<LevelUp> ApplyLevelUp(PlayerRespect& p, int32_t grant, const RespectTable& table,
                                  AppliedUnlocks& applied, const AwardContext& ctx = {});

// Record L's unlockables not yet applied are applied now (in record order);
// returns them.
std::vector<std::string> ApplyLevelUnlocks(int level, const RespectTable& table,
                                           AppliedUnlocks& applied);

// The state a total of respect reaches when granted from zero (B.3).
PlayerRespect RespectStateFromTotal(int32_t total, const RespectTable& table);
// total - inLevel == cumulative(L), true of every real save (B.2 data check).
bool RespectStateConsistent(const PlayerRespect& p, const RespectTable& table);

// Dollars to raw cash: x100, clamped to +-2,000,000,000.
int64_t DollarsToRawCash(int64_t dollars);

// ---------------------------------------------------------------------------
// The respect multiplier and the grant sources (B.1.1, B.1.2)
// ---------------------------------------------------------------------------

constexpr float kNewGameRespectMultiplier = 1.0f;
// Unlockable type Respect_Bonus_Modifier adds to the multiplier.
inline float AddRespectBonus(float multiplier, float modifier) { return multiplier + modifier; }
// round(multiplier x amount): the toast grant, streaking, game_award_respect,
// Pay-Cash-for-Respect.
int32_t ScaleByMultiplier(float multiplier, int32_t amount);

// The game's "round(multiplier x amount)" (spec-differential-results D-1): the product rounded to
// single precision, then to the nearest integer with halves away from zero; a result outside the
// signed 32-bit range is -2147483648.
int32_t RoundProductF32(double product);
// A signed 32-bit wrap of a sum (the award routine keeps its sums in 32 bits, D-4).
int64_t Wrap32(int64_t v);

// The cash wallet add for the local player (spec-differential-results D-22): a positive amount is
// first scaled by the cash-gain multiplier (Cash_Bonus_* / Cash_Bonus_Modifier, 1.0 at a new game):
// round_half_away(single(single(single(amount) / 100) x multiplier x 100)), clamped to +-2e9; then
// new = clamp(cash) + amount in signed 32 bits (a wrap goes negative), clamped to +-2e9, floored at 0.
// Negative amounts and other players' grants are not scaled. Raw cash (cents).
int64_t AddCashToWallet(int64_t cashRaw, int64_t amountRaw, float cashMultiplier, bool localPlayer = true);

// Instanced activity completion: two awards, Props_Award then
// round(Props_Award x (multiplier - 1)).
struct ActivityAwards {
    int32_t first = 0;
    int32_t second = 0;
};
ActivityAwards ActivityCompletionAwards(int32_t propsAward, float multiplier);

// The diversion reward rule: fraction clamped to [0, 1]; nothing when it is
// ~0; once the lifetime statistic reaches Max_Lifetime_Respect, cash =
// round(fraction x Max_Cash) and no respect; otherwise respect =
// trunc(fraction x Max_Respect) and no cash.
struct DiversionReward {
    int32_t respect = 0;
    int32_t cashDollars = 0;
};
DiversionReward DiversionRewardFor(float fraction, int32_t maxRespect, int32_t maxLifetimeRespect,
                                   int32_t maxCash, int64_t lifetimeStatistic);

struct DiversionRespectData {
    const char* name;
    int32_t maxRespect;
    int32_t maxLifetimeRespect;
    int32_t maxCash;
    int statId;  // "<name> - respect earned"; -1 where it is per record
};
// The shipped stunt diversions (B.1.2 row 1).
extern const DiversionRespectData kShippedStuntDiversions[11];

// A reward toast record: +0x10 respect, +0x14 cash in dollars. It grants
// when it is finalised.
struct RewardToast {
    int32_t respect = 0;
    float cashDollars = 0.0f;
};
// Respect OR cash, never both (spec-differential-results D-3): with respect > 0 only the respect is
// awarded (rounded as RoundProductF32; a 0 amount does nothing); otherwise a cash field > 0 pays
// round_half_away(single(cash x 100)), clamped.
struct ToastFinalisation {
    AwardResult award;          // round(multiplier x respect), source 3
    int64_t cashRaw = 0;        // the toast's own cash (only when it carries no respect)
    bool zeroGrantHint = false; // the "nothing granted" notification
};
ToastFinalisation FinaliseRewardToast(const RewardToast& toast, float multiplier, PlayerRespect& p,
                                      const RespectTable& table, AppliedUnlocks& applied,
                                      const AwardContext& ctx = {});

// Gang-member kills (props.xtbl, kill thug/killa/gangsta/kingpin, chosen by
// the victim's rank: >= 3 kingpin, 2 gangsta, 1 killa, else thug). Not
// multiplied by the respect multiplier; multiplied by the per-kill factor.
struct KillRespectTable {
    int32_t thug = 10, killa = 10, gangsta = 10, kingpin = 10;
};
int32_t KillRespect(const KillRespectTable& props, int victimRank, bool victimIsGangMember,
                    bool victimExcluded, float perKillFactor = 1.0f);

// Store purchases: round(round(price x Store_weapon_purchase_respect_multiplier)
// x multiplier). The shipped multiplier is 0, so no purchase respect.
int32_t StorePurchaseRespect(double price, double storeRespectMultiplier, float multiplier);

// Unlockable apply effects on progression (wiring for the review item RP-7).
// Respect_Bonus_Modifier (type 37): the multiplier is raised additively by the
// row's Modifier (B.1.1); any other row leaves it unchanged.
float ApplyRespectBonusUnlockable(float multiplier, const sr3tables_progression::Unlockable& u);

// Pay_Cash_for_Respect (type 32, B.1.2 row 6), applied when bought: charge
// Cash_Cost (returned as the table gives it; the shipped rows cost 0), then
// award round(multiplier x Respect_Received). `applies` is false for any
// other row, and nothing is awarded.
struct PayCashForRespectResult {
    bool applies = false;
    int32_t cashCost = 0;
    AwardResult award;
};
PayCashForRespectResult ApplyPayCashForRespectUnlockable(const sr3tables_progression::Unlockable& u,
                                                         float multiplier, PlayerRespect& p,
                                                         const RespectTable& table, AppliedUnlocks& applied,
                                                         const AwardContext& ctx = {});


// Shipped amounts of the data-driven sources (B.1.2).
constexpr int32_t kHoodRespect = 100;
constexpr int32_t kDistrictRespect = 250;
constexpr int32_t kSecretLocationRespect = 50;  // a stunt jump reads this field too
constexpr int32_t kFlashpointRespect = 100;
constexpr int32_t kFlashpointCashDollars = 1000;
constexpr int32_t kCashForRespect[3] = {4000, 8000, 12000};

}  // namespace sr3progression

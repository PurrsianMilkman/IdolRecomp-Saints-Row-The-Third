// Professor Genki - spec-activity-runtime.md section 6.9. See include/sr3activity/genki.h.

#include "sr3activity/genki.h"

#include <algorithm>
#include <cmath>

namespace sr3activity {

namespace {

double Num(const sr3xtbl::Node* n, const char* name) {
    if (!n) return 0.0;
    if (auto v = sr3xtbl::GetFloat(n, name)) return *v;
    return 0.0;
}

const sr3xtbl::Node* Path(const sr3xtbl::Node* n, std::initializer_list<const char*> names) {
    for (const char* s : names) {
        if (!n) return nullptr;
        n = sr3xtbl::FindChild(n, s);
    }
    return n;
}

std::vector<GenkiThresholdFactor> ReadTable(const sr3xtbl::Node* table, const char* row) {
    std::vector<GenkiThresholdFactor> out;
    if (!table) return out;
    for (const sr3xtbl::Node* r : sr3xtbl::Children(table, row)) out.push_back({Num(r, "Threshold"), Num(r, "Factor")});
    return out;
}

}  // namespace

std::optional<GenkiGlobals> ReadGenkiGlobals(const sr3xtbl::Document& doc) {
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    const sr3xtbl::Node* scoring = Path(scope, {"Running_man_globals", "Scoring"});
    if (!scoring) return std::nullopt;
    GenkiGlobals g;
    if (const sr3xtbl::Node* ks = Path(scoring, {"On_foot", "Kill_scores"})) {
        for (const sr3xtbl::Node* k : sr3xtbl::Children(ks, "Kill_score")) {
            const std::string* type = sr3xtbl::ChildText(k, "Type");
            if (!type) continue;
            int i = -1;
            if (sr3xtbl::NameEquals(*type, "Civilian")) i = 0;
            else if (sr3xtbl::NameEquals(*type, "Enemy Gang")) i = 1;
            else if (sr3xtbl::NameEquals(*type, "Hunter")) i = 2;
            if (i < 0) continue;
            GenkiKillScore& s = g.kills[static_cast<size_t>(i)];
            s.base = Num(k, "Base");
            s.oneHit = Num(k, "One_hit");
            s.humanShield = Num(k, "Human_shield");
            s.melee = Num(k, "Melee");
            s.sword = Num(k, "Sword");
            s.headshot = Num(k, "Headshot");
            s.nutshot = Num(k, "Nutshot");
            s.trapKill = Num(k, "Trap_Kill");
        }
    }
    const sr3xtbl::Node* t = sr3xtbl::FindChild(scoring, "Targets");
    g.ethicalBonusTime = Num(Path(t, {"Ethical_Target"}), "Bonus_Time");
    g.ethicalBonusCash = Num(Path(t, {"Ethical_Target"}), "Bonus_Cash");
    g.unethicalCashDeduction = Num(Path(t, {"Unethical_Target"}), "Cash_Deduction");
    g.unethicalTimeDeduction = Num(Path(t, {"Unethical_Target"}), "Time_Deduction");
    g.moneyBonusCash = Num(Path(t, {"Money_Target"}), "Bonus_Cash");
    g.timeBonusTime = Num(Path(t, {"Time_Target"}), "Bonus_Time");
    g.buttonBonusCash = Num(Path(t, {"Button"}), "Bonus_Cash");
    g.healthBonus = Num(Path(t, {"Health_Target"}), "Bonus_Health");
    const sr3xtbl::Node* te = sr3xtbl::FindChild(scoring, "Trap_Evade");
    g.electricEvadeCash = Num(Path(te, {"Electrical"}), "Bonus_Cash");
    g.electricEvadeTime = Num(Path(te, {"Electrical"}), "Bonus_Time");
    g.fireEvadeCash = Num(Path(te, {"Fire"}), "Bonus_Cash");
    g.fireEvadeTime = Num(Path(te, {"Fire"}), "Bonus_Time");
    const sr3xtbl::Node* combos = sr3xtbl::FindChild(scoring, "Combos");
    g.multiplierTable = ReadTable(Path(combos, {"Multiplier_table"}), "Multiplier");
    g.decayRateTable = ReadTable(Path(combos, {"Decay_rate_table"}), "Decay_rate");
    g.maxComboTicks = Num(combos, "Max_combo_ticks");
    g.baseDecayRate = Num(combos, "Base_decay_rate");
    return g;
}

double GenkiComboFactor(const GenkiGlobals& g, int consecutive) {
    double f = 1.0, best = -1.0;
    for (const GenkiThresholdFactor& m : g.multiplierTable)
        if (consecutive >= m.threshold && m.threshold > best) {
            best = m.threshold;
            f = m.factor;
        }
    return f;
}

std::optional<GenkiLevel> ReadGenkiLevel(const sr3xtbl::Document& doc, int levelIndex) {
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    const sr3xtbl::Node* levels = Path(scope, {"Activity", "Levels"});
    const sr3xtbl::Node* lv = levels ? sr3xtbl::NthChild(levels, "Level", static_cast<size_t>(levelIndex)) : nullptr;
    if (!lv) return std::nullopt;
    GenkiLevel l;
    l.timeLimitS = Num(lv, "Time_limit");
    const sr3xtbl::Node* ex = sr3xtbl::FindChild(lv, "Level_Exit");
    l.requiredCash = Num(ex, "Required_cash");
    l.requiredCashCoop = Num(ex, "Required_cash_coop");
    if (ex)
        if (const std::string* e = sr3xtbl::ChildText(ex, "End_exit")) l.endExit = *e;
    return l;
}

namespace {

std::vector<GenkiThresholdFactor> KeptRows(const std::vector<GenkiThresholdFactor>& in, size_t cap) {
    std::vector<GenkiThresholdFactor> out;
    for (const GenkiThresholdFactor& r : in) {
        if (out.size() >= cap) break;
        if (r.threshold < 0.0 || r.factor <= 0.0) continue;
        if (!out.empty() && r.threshold <= out.back().threshold) continue;
        out.push_back(r);
    }
    return out;
}

constexpr long long kSat = 2147483647LL;

}  // namespace

GenkiCombo::GenkiCombo(const GenkiGlobals& g)
    : mult_(KeptRows(g.multiplierTable, 16)), decay_(KeptRows(g.decayRateTable, 8)),
      maxTicks_(static_cast<int>(g.maxComboTicks)), baseRate_(static_cast<int>(g.baseDecayRate)) {}

void GenkiCombo::reset() {
    m_ = d_ = -1;
    ticks_ = peak_ = count_ = 0;
    bank_ = 0;
    reached_ = expired_ = false;
}

double GenkiCombo::multiplier() const { return m_ < 0 ? 1.0 : mult_[static_cast<size_t>(m_)].factor; }
double GenkiCombo::decayFactor() const { return d_ < 0 ? 1.0 : decay_[static_cast<size_t>(d_)].factor; }

void GenkiCombo::add(int n, long long cash, int ticks) {
    ticks_ = std::min(ticks_ + ticks, maxTicks_);
    peak_ = std::max(peak_, ticks_);
    if (ticks_ <= 0) return;
    count_ = static_cast<int>(std::min<long long>(static_cast<long long>(count_) + n, kSat));
    if (static_cast<size_t>(m_ + 1) < mult_.size() && count_ >= mult_[static_cast<size_t>(m_ + 1)].threshold) {
        ++m_;
        reached_ = true;
    }
    bank_ = std::min(bank_ + cash, kSat);
    if (static_cast<size_t>(d_ + 1) < decay_.size() && bank_ >= decay_[static_cast<size_t>(d_ + 1)].threshold) ++d_;
}

void GenkiCombo::refill(int n, long long cash, double k) {
    const int perSecond = static_cast<int>(static_cast<double>(baseRate_) * decayFactor());
    add(n, cash, static_cast<int>(static_cast<double>(perSecond) * k));
}

bool GenkiCombo::decay(double dt) {
    if (ticks_ <= 0) return false;
    const int perSecond = static_cast<int>(static_cast<double>(baseRate_) * decayFactor());
    ticks_ -= static_cast<int>(static_cast<double>(perSecond) * dt);
    if (ticks_ <= 0) {
        ticks_ = 0;
        expired_ = true;
    }
    return expired_;
}

GenkiRun::GenkiRun(const GenkiGlobals& g, const GenkiLevel& level, bool coop) : g_(g), combo_(g) {
    required_ = coop ? level.requiredCashCoop : level.requiredCash;
    timeLeftS_ = level.timeLimitS;
}

double GenkiRun::kill(const GenkiKill& k) {
    if (state_ != GenkiState::Running) return 0.0;
    const GenkiKillScore& s = g_.kills[static_cast<size_t>(k.victim)];
    const double m = combo_.multiplier();  // read before the increment
    auto pay = [&](double v) { return std::trunc(v * m); };
    double add = pay(s.base);
    if (k.oneHit) add += pay(s.oneHit);
    if (k.humanShield) add += pay(s.humanShield);
    if (k.melee) add += pay(s.melee);
    if (k.sword) add += pay(s.sword);
    if (k.headshot) add += pay(s.headshot);
    if (k.nutshot) add += pay(s.nutshot);
    if (k.trapKill) add += pay(s.trapKill);
    cash_ += add;
    combo_.refill(1, static_cast<long long>(add));
    return add;
}

// Pickups, the button and trap evades (6.9.1): cash paid at trunc(cash x M), time never
// multiplied; a negative cash or time ends the combo instead of adding to it. The run total is
// floored at 0.
void GenkiRun::pickup(double cash, double time) {
    const double paid = std::trunc(cash * combo_.multiplier());
    cash_ = std::max(0.0, cash_ + paid);
    timeLeftS_ += time;
    if (cash < 0.0 || time < 0.0) {
        lastEndBank_ = combo_.bank();
        combo_.reset();
    } else {
        combo_.add(0, static_cast<long long>(cash), 0);
    }
}

double GenkiRun::target(GenkiTarget t) {
    if (state_ != GenkiState::Running) return 0.0;
    switch (t) {
    case GenkiTarget::Ethical: pickup(g_.ethicalBonusCash, g_.ethicalBonusTime); break;
    case GenkiTarget::Unethical: pickup(-g_.unethicalCashDeduction, -g_.unethicalTimeDeduction); break;
    case GenkiTarget::Money: pickup(g_.moneyBonusCash, 0.0); break;
    case GenkiTarget::Time: pickup(0.0, g_.timeBonusTime); break;
    case GenkiTarget::Button: pickup(g_.buttonBonusCash, 0.0); break;
    case GenkiTarget::Health: return g_.healthBonus;
    }
    return 0.0;
}

void GenkiRun::trapEvaded(GenkiTrap t) {
    if (state_ != GenkiState::Running) return;
    if (t == GenkiTrap::Electric) pickup(g_.electricEvadeCash, g_.electricEvadeTime);
    else pickup(g_.fireEvadeCash, g_.fireEvadeTime);
}

long long GenkiRun::tick(double dt) {
    if (state_ != GenkiState::Running) return -1;
    timeLeftS_ -= dt;
    if (timeLeftS_ <= 0.0) state_ = GenkiState::Failed;
    if (combo_.decay(dt)) {
        const long long b = combo_.bank();
        lastEndBank_ = b;
        combo_.reset();
        return b;
    }
    return -1;
}

bool GenkiRun::reachExit() {
    if (state_ != GenkiState::Running || cash_ < required_) return false;
    state_ = GenkiState::Succeeded;
    return true;
}

void GenkiRun::playerDied() {
    if (state_ == GenkiState::Running) state_ = GenkiState::Failed;
}

}  // namespace sr3activity

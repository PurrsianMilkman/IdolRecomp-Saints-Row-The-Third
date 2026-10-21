// Trafficking - spec-activity-runtime.md section 6.3. See include/sr3activity/trafficking.h.

#include "sr3activity/trafficking.h"

namespace sr3activity {

namespace {

std::string Txt(const sr3xtbl::Node* n, const char* name) {
    if (!n) return std::string();
    if (const std::string* t = sr3xtbl::ChildText(n, name)) return *t;
    return std::string();
}

double Num(const sr3xtbl::Node* n, const char* name) {
    if (!n) return 0.0;
    if (auto v = sr3xtbl::GetFloat(n, name)) return *v;
    return 0.0;
}

}  // namespace

std::optional<TraffickingParams> ReadTraffickingParams(const sr3xtbl::Document& doc, int levelIndex) {
    const sr3xtbl::Node* scope = doc.table() ? doc.table() : doc.root();
    const sr3xtbl::Node* row = scope ? sr3xtbl::FindChild(scope, "Activity") : nullptr;
    const sr3xtbl::Node* levels = row ? sr3xtbl::FindChild(row, "Levels") : nullptr;
    const sr3xtbl::Node* lv = levels ? sr3xtbl::NthChild(levels, "Level", static_cast<size_t>(levelIndex)) : nullptr;
    if (!lv) return std::nullopt;
    TraffickingParams p;
    p.dealUseCount = static_cast<int>(Num(lv, "Deal_Use_Count"));
    p.maxComplications = static_cast<int>(Num(lv, "Max_complications"));
    p.startingNav = Txt(lv, "Starting_Nav");
    p.endingNav = Txt(lv, "Ending_Nav");
    p.dealerType = Txt(lv, "Dealer_Type");
    if (p.dealerType.empty()) p.dealerType = Txt(row, "Dealer_Type");
    p.vehicleType = Txt(lv, "Vehicle_Type");
    if (p.vehicleType.empty()) p.vehicleType = Txt(row, "Vehicle_Type");
    p.vehicleHealth = Num(row, "Vehicle_Health");
    p.vehicleOverrideSpeedMph = Num(row, "Vehicle_Override_Speed_MPH");
    p.policeNotoriety = static_cast<int>(Num(lv, "Police_Notoriety"));
    p.gangNotoriety = static_cast<int>(Num(lv, "Gang_Notoriety"));
    if (const sr3xtbl::Node* deals = sr3xtbl::FindChild(lv, "Deals"))
        for (const sr3xtbl::Node* d : sr3xtbl::Children(deals, "Deal"))
            p.deals.push_back({Txt(d, "Deal_Nav"), Txt(d, "Buyer_Spawn"), Txt(d, "Buyer_Type")});
    if (const sr3xtbl::Node* cs = sr3xtbl::FindChild(lv, "Level_Complications"))
        for (const sr3xtbl::Node* c : sr3xtbl::Children(cs, "Level_Complication"))
            p.complications.push_back({Txt(c, "Complication_Name"), Num(c, "Complication_Chance")});
    return p;
}

TraffickingRun::TraffickingRun(const TraffickingParams& params) : p_(params) {}

const TraffickingDeal* TraffickingRun::currentDeal() const {
    if (state_ != TraffickingState::Dealing || p_.deals.empty() || dealsDone_ >= p_.dealUseCount) return nullptr;
    return &p_.deals[static_cast<size_t>(dealsDone_) % p_.deals.size()];
}

std::optional<std::string> TraffickingRun::rollComplication(const std::vector<double>& samples) {
    if (state_ != TraffickingState::Dealing || complications_ >= p_.maxComplications) return std::nullopt;
    for (size_t i = 0; i < p_.complications.size() && i < samples.size(); ++i) {
        if (samples[i] < p_.complications[i].chance) {
            ++complications_;
            return p_.complications[i].name;
        }
    }
    return std::nullopt;
}

void TraffickingRun::completeDeal() {
    if (state_ != TraffickingState::Dealing) return;
    ++dealsDone_;
    if (dealsDone_ >= p_.dealUseCount) state_ = TraffickingState::ToEnding;
}

bool TraffickingRun::reachEnding() {
    if (state_ != TraffickingState::ToEnding) return false;
    state_ = TraffickingState::Succeeded;
    return true;
}

void TraffickingRun::fail(TraffickingFail r) {
    if (state_ == TraffickingState::Succeeded || state_ == TraffickingState::Failed) return;
    state_ = TraffickingState::Failed;
    failReason_ = r;
}

}  // namespace sr3activity

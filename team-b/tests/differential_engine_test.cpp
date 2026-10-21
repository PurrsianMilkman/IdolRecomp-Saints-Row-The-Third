// Team D differential vectors, vehicle engine / transmission / steering (spec-differential-results.md D-17..D-20,
// spec-differential-vectors-engine.md E1..E3).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "diff_vectors.h"
#include "sr3physics/vehicle_driving_slice.h"

using namespace sr3physics;

static int g_fail = 0;
static void report(const char* area, int total, int fail) {
    std::printf("%s: %d vectors, %d failures\n", area, total, fail);
    g_fail += fail;
    if (total == 0) ++g_fail;
}

static EngineCurveTuning engineFrom(const std::vector<double>& f) {
    EngineCurveTuning e;
    e.torque = f[0]; e.minRpm = f[1]; e.optRpm = f[2]; e.maxRpm = f[3];
    e.minTorqueFactor = f[4]; e.maxTorqueFactor = f[5];
    e.minResistance = f[6]; e.optResistance = f[7]; e.maxResistance = f[8];
    e.clutchSlipRpm = f[9];
    return e;
}

static bool parseEngine(const std::string& cell, std::vector<double>& f) {
    f.clear();
    std::stringstream ss(cell);
    std::string tok;
    while (std::getline(ss, tok, '/')) {
        double v;
        if (!dv::num(dv::trim(tok), v)) return false;
        f.push_back(v);
    }
    return f.size() == 10;
}

static void e1() {  // D-17 engine torque
    int total = 0, fail = 0;
    for (const auto& r : dv::rows("spec-differential-vectors-engine.md", "## E1.")) {
        std::vector<double> f;
        double rpm, thr, exp;
        if (r.size() < 4 || !parseEngine(r[0], f) || !dv::num(r[1], rpm) || !dv::num(r[2], thr) || !dv::num(r[3], exp)) continue;
        ++total;
        const double got = EngineNetTorque(engineFrom(f), rpm, thr, false, 1.0, rpm);
        if (std::fabs(got - exp) > 2e-3 + 1e-5 * std::fabs(exp) && ++fail <= 5)
            std::printf("FAIL E1 %s rpm %g thr %g: got %.3f exp %.3f\n", r[0].c_str(), rpm, thr, got, exp);
    }
    report("E1 engine torque", total, fail);
}

static void e2() {  // D-18 engine rpm update (first shipped engine)
    int total = 0, fail = 0;
    const EngineCurveTuning e = engineFrom({350, 1000, 5700, 6500, 0.9, 0.9, 0, 0, 0.2, 1750});
    for (const auto& r : dv::rows("spec-differential-vectors-engine.md", "## E2.")) {
        double dt, cur, thr, tr, exp;
        if (r.size() < 5 || !dv::num(r[0], dt) || !dv::num(r[1], cur) || !dv::num(r[2], thr) || !dv::num(r[3], tr) || !dv::num(r[4], exp)) continue;
        ++total;
        const double got = EngineRpmUpdate(e, cur, thr, tr, dt);
        if (std::fabs(got - exp) > 0.01 && ++fail <= 5) std::printf("FAIL E2 dt %g cur %g thr %g tr %g: got %.4f exp %.4f\n", dt, cur, thr, tr, got, exp);
    }
    report("E2 rpm update", total, fail);
}

static std::vector<double> csv(const std::string& s) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) v.push_back(std::atof(tok.c_str()));
    return v;
}

static void e3() {  // D-20 steering limiter sequences
    int total = 0, fail = 0;
    std::ifstream in(std::string(SR3_SPEC_DIR) + "/spec-differential-vectors-engine.md");
    std::string line;
    bool inE3 = false;
    double V = 0, d = 0, Vr = 0, dr = 0;
    std::vector<double> targets, orig;
    while (std::getline(in, line)) {
        if (line.rfind("## E3.", 0) == 0) { inE3 = true; continue; }
        if (!inE3) continue;
        if (line.rfind("- turnSpeed=", 0) == 0) {
            std::sscanf(line.c_str(), "- turnSpeed=%lf turnDamp=%lf returnSpeed=%lf returnDamp=%lf", &V, &d, &Vr, &dr);
        } else if (line.find("  - targets: ") == 0) {
            targets = csv(line.substr(13));
        } else if (line.find("  - original: ") == 0) {
            orig = csv(line.substr(14));
            ++total;
            SteeringProfileParams p{V, V * V / (2.0 * d), Vr, Vr * Vr / (2.0 * dr)};
            SteeringProfileLimiter lim(p);
            double worst = 0;
            size_t worstAt = 0;
            for (size_t i = 0; i < targets.size() && i < orig.size(); ++i) {
                const double got = lim.Update(targets[i], 1.0 / 30.0);
                if (std::fabs(got - orig[i]) > worst) { worst = std::fabs(got - orig[i]); worstAt = i; }
            }
            if (worst > 2e-5) {
                ++fail;
                std::printf("FAIL E3 V %g d %g Vr %g dr %g: worst diff %.6f at step %zu\n", V, d, Vr, dr, worst, worstAt);
            }
        }
    }
    report("E3 steering sequences", total, fail);
}

static std::vector<int> gearRun(double rpm, double upshift, int steps, const std::vector<double>& down, int maxGear = 5) {
    GearShiftTuning t;
    t.downshiftRpmByGear = down;
    t.upshiftRpmThreshold = upshift;
    t.maxGear = maxGear;
    t.shiftDurationSec = 0.3;
    GearShiftStateMachine g(t, 1);
    std::vector<int> out;
    for (int i = 0; i < steps; ++i) { g.Update(rpm, 1.0 / 30.0); out.push_back(g.gear()); }
    return out;
}

static void d19() {  // D-19 gear shifting (rule + the two tabulated sequences of spec-differential-results.md)
    int total = 0, fail = 0;
    auto check = [&](bool ok, const char* what) { ++total; if (!ok) { ++fail; std::printf("FAIL D19 %s\n", what); } };
    const std::vector<double> none(4, 0.0);
    check(gearRun(7000, 5500, 6, none) == std::vector<int>({2, 3, 4, 5, 5, 5}), "7000 rpm climbs one gear per step");
    check(gearRun(5500, 5500, 6, none) == std::vector<int>({1, 1, 1, 1, 1, 1}), "exactly at the upshift threshold: no shift (strict)");
    // downshift: strictly greater table value, gear above 1; evaluated before the upshift in the same step
    {
        GearShiftTuning t;
        t.downshiftRpmByGear = {2000, 2500, 3000, 3500};
        t.upshiftRpmThreshold = 5500;
        t.maxGear = 5;
        GearShiftStateMachine g(t, 3);
        g.Update(2500, 1.0 / 30.0);  // table[1] = 2500 is not > 2500 -> no downshift
        check(g.gear() == 3, "downshift needs table > rpm (strict)");
        g.Update(2499, 1.0 / 30.0);
        check(g.gear() == 2, "downshift when table > rpm");
        g.Update(100, 1.0 / 30.0);  // immediately again, no cooldown / no mid-shift block
        check(g.gear() == 1, "no cooldown or mid-shift block between decisions");
    }
    report("D-19 gear shifting", total, fail);
}

int main() {
    e1(); e2(); e3(); d19();
    return g_fail ? 1 : 0;
}

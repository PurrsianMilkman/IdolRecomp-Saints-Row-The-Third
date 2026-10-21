// Team D differential vectors, notoriety gain and decay (spec-differential-results.md D-21, spec-differential-vectors-ai.md).
// Each "### police|gang0, ..." block is a scripted sequence from level 0 / fraction 0; "C,points,Min,Max,Check,witnessed,x3" is a
// crime report (delay 0), "D" is one decay step (decay timer expired). Expected state after every event is "level/fraction".
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "sr3airuntime/notoriety_escalation.h"

using namespace sr3airuntime;

int main() {
    std::ifstream in(std::string(SR3_SPEC_DIR) + "/spec-differential-vectors-ai.md");
    if (!in) { std::printf("cannot open vectors\n"); return 1; }
    std::string line;
    int sequences = 0, events = 0, badSeq = 0, badEvents = 0;
    NotorietyEscalation* esc = nullptr;
    NotorietyTrack track = NotorietyTrack::kPolice;
    int64_t now = 0;
    bool seqBad = false;
    auto closeSeq = [&]() {
        if (esc) { ++sequences; if (seqBad) ++badSeq; delete esc; esc = nullptr; }
    };
    while (std::getline(in, line)) {
        if (line.rfind("### ", 0) == 0) {
            closeSeq();
            esc = new NotorietyEscalation();
            track = line.find("police") != std::string::npos ? NotorietyTrack::kPolice : NotorietyTrack::kLuchadores;
            now = 1000;
            seqBad = false;
            continue;
        }
        if (!esc || line.rfind("| `", 0) != 0) continue;
        const size_t a = line.find('`') + 1, b = line.find('`', a);
        const std::string ev = line.substr(a, b - a);
        const size_t c = line.find("| ", b);
        int expLevel = 0;
        double expFrac = 0;
        if (std::sscanf(line.c_str() + c + 2, "%d/%lf", &expLevel, &expFrac) != 2) continue;
        if (ev[0] == 'C') {
            int pts, mn, mx, chk, wit, x3;
            std::sscanf(ev.c_str(), "C,%d,%d,%d,%d,%d,%d", &pts, &mn, &mx, &chk, &wit, &x3);
            CrimeActivity act;
            act.name = "differential vector";
            act.delayMs = 0;
            act.gang = act.police = CrimeActivitySubBlock{pts, mn, mx, chk != 0};
            esc->ReportCrime(track, act, wit != 0, x3 != 0, now);
            esc->Tick(now);
        } else {
            now += 100000;  // the decay timer has expired
            esc->Tick(now);
        }
        ++events;
        const int gl = esc->level(track);
        const double gf = esc->fraction(track);
        if (gl != expLevel || std::fabs(gf - expFrac) > 1e-3) {
            seqBad = true;
            if (++badEvents <= 12) std::printf("FAIL seq %d event %s: got %d/%.3f expected %d/%.3f\n", sequences, ev.c_str(), gl, gf, expLevel, expFrac);
        }
    }
    closeSeq();
    std::printf("D-21 notoriety: %d sequences (%d differ), %d events (%d differ)\n", sequences, badSeq, events, badEvents);
    return (badEvents || sequences < 90) ? 1 : 0;
}

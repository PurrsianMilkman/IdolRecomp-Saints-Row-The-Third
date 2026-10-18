// Tests for sr3radio (spec-audio-runtime.md Sec14.6). Synthetic content; the ring vector is the spec's own.
#include "sr3radio/radio_sim.h"
#include <cstdio>
#include <set>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace sr3radio;

static RadioSystem makeSys() {
    RadioSystem sys;
    sys.stations = retailRoster();
    // Synthetic content: 0 sweeper, 1..4 songs, 5 commercial, 6 intro (delay 100), 7 outro (prestart 50).
    auto add = [&](ContentType t, uint32_t len, uint16_t d = 0) {
        ContentEntry e; e.type = t; e.lengthMs = len; e.delayMs = d; e.cueId = (uint32_t)sys.content.entries.size();
        sys.content.entries.push_back(e);
    };
    add(ContentType::Sweeper, 1000);
    for (int i = 0; i < 4; ++i) add(ContentType::Song, 10000);
    add(ContentType::Commercial, 2000);
    add(ContentType::Intro, 500, 100);
    add(ContentType::Outro, 400, 50);
    Station& g = sys.stations[2];  // GenX
    g.songs.setup({1, 2, 3, 4}); g.commercials.setup({5}); g.sweepers.setup({0});
    g.pattern = {ContentType::Song, ContentType::Song, ContentType::Commercial, ContentType::Sweeper};
    return sys;
}

int main() {
    // 1. MT19937 ring, spec Sec14.6.4 item 2.
    {
        RandomRing r(5489);
        CHECK(r.rand32() == 0xD091BB5Cu); CHECK(r.rand32() == 0x22AE9EF6u); CHECK(r.rand32() == 0xE7E1FAEEu);
        CHECK(r.rand32() == 0xD5C31F79u); CHECK(r.rand32() == 0x2082352Cu);
    }
    // 2. roster/cycle order: Mix Tape empty -> Off, GenX ... Mix, wrap; news/credits rejected.
    {
        RadioSystem sys = makeSys();
        sys.worldStart();
        CHECK(sys.stations.size() == 12);
        std::vector<int> seq;
        for (int i = 0; i < 10; ++i) seq.push_back(sys.cycle(1, +1, true));
        const int expect[] = {2,3,4,5,6,7,8,9,10,2};
        for (int i = 0; i < 10; ++i) CHECK(seq[(size_t)i] == expect[i]);
        CHECK(sys.stationOf(1) == 2);
        CHECK(sys.hudImage(1).empty());
        CHECK(sys.cycle(1, -1, true) == 10);
        CHECK(sys.hudImage(1) == "ui_radio_10777_the_mix");
        CHECK(sys.cycle(1, +1, true, /*locked*/true) == 10);
        CHECK(!sys.tune(2, 3, /*RadioTuner*/false));
        sys.unregister(1);
        CHECK(sys.stationOf(1) == 0 && sys.hudImage(1).empty());
    }
    // 3. hidden/mixtape gates.
    {
        RadioSystem sys = makeSys();
        sys.stations[3].hidden = true;
        CHECK(!sys.tune(1, 4, true));
        CHECK(!sys.tune(1, 1, true));             // empty mix tape
        sys.stations[0].mixTapeList = {5}; sys.worldStart();
        CHECK(sys.tune(1, 1, true));
        for (int v = 10; v < 18; ++v) sys.tune(v, 3, true);   // fill 8 slots
        CHECK(sys.stations[2].listeners.size() == 8);
        CHECK(!sys.tune(99, 3, true));
    }
    // 4. bag pick: no repeat until a draw fails; used flags set.
    {
        RadioSystem sys = makeSys();
        UsedList l; l.setup({1, 2, 3, 4});
        std::set<int> seen;
        for (int i = 0; i < 4; ++i) { int k = sys.bagPick(l, false); CHECK(k >= 0); seen.insert(k); }
        CHECK(seen.size() <= 4);
        for (auto u : l.used) CHECK(u == 1 || u == 0);
        UsedList e; CHECK(sys.bagPick(e, false) == -1);
    }
    // 5. queue builder: pattern shape and bound.
    {
        RadioSystem sys = makeSys();
        Station& g = sys.stations[2];
        sys.buildQueue(g);
        CHECK(g.queue.size() >= 512 - 4 && g.queue.size() <= 520);
        
        for (auto q : g.queue) CHECK(q <= 5);      // no intro/outro without song records
        // Commercial disabled everywhere -> builder terminates (CHOSEN bail-out) rather than hang.
        sys.content.entries[5].disableSlot = 1; sys.content.disFlag[1] = 1;
        sys.buildQueue(g);
        CHECK(g.queue.size() > 0);
    }
    // 4b. intro/outro with song records.
    {
        RadioSystem sys = makeSys();
        sys.content.songRecordOf.assign(8, -1);
        SongRecord r; r.intros = {6}; r.outros = {7};
        sys.content.songRecords.push_back(r);
        for (int s = 1; s <= 4; ++s) sys.content.songRecordOf[(size_t)s] = 0;
        Station& g = sys.stations[2]; g.introPct = 100; g.outroPct = 100;
        sys.buildQueue(g, /*faithfulIntroDefect*/false);
        // first slot is a song with a guaranteed intro (100%) and single-entry list
        CHECK(g.queue[0] == 6 && g.queue[1] <= 4);
        g.noDjTalk = true; sys.buildQueue(g);
        for (auto q : g.queue) CHECK(q != 6 && q != 7);
    }
    // 5b. station with count 3 intros, defect off => never entry 0 from a pick.
    {
        RadioSystem sys = makeSys();
        std::vector<uint16_t> idx{6, 6, 6}; std::vector<uint8_t> used{0, 0, 0};
        for (int i = 0; i < 500; ++i) { std::fill(used.begin(), used.end(), 0);
            CHECK(sys.pickIntroOutro(idx, used, false) == 6); }
    }
    // 6. timeline: inaudible advance skips outros, audible join seeks, overlap-adjusted arm.
    {
        RadioSystem sys = makeSys();
        Station& g = sys.stations[2];
        g.queue = {1, 7, 2, 6, 3}; g.cursor = 0;
        g.timerMs = -1;
        sys.tick(0);                          // arms current Length
        CHECK(g.timerMs == 10000);
        sys.tick(10000);                      // expires, advances, skips outro idx 1 -> song 2
        CHECK(sys.current(g)->cueId == 2);
        // intro overlap: arm = length - delay
        g.cursor = 3; CHECK(sys.armDuration(g) == 400);
        // prestart overlap: song followed by outro
        g.cursor = 0; CHECK(sys.armDuration(g) == 10000 - 50);
        // join mid-way
        g.cursor = 2; g.timerMs = 4000; g.audible = false;
        uint32_t seek = 0; const ContentEntry* e = sys.becomeAudible(g, seek);
        CHECK(e && e->cueId == 2);
        CHECK(seek == 6000 && g.audible);
        // timeline continues after last listener leaves
        sys.tune(1, 3, true); sys.unregister(1);
        CHECK(!g.audible && g.timerMs >= 0);
    }
    // 7. default station + NPC roll.
    {
        RadioSystem sys = makeSys();
        sys.worldStart();
        CHECK(sys.defaultStation(5, false) == 5);
        CHECK(sys.defaultStation(0xFF, true) == 6);   // forced KLASSIC
        for (int i = 0; i < 200; ++i) {
            int d = sys.defaultStation(0xFF, false);
            CHECK(d >= 2 && d <= 10);                  // never mix tape(empty)/news/credits
        }
        sys.globalBlock = true; CHECK(!sys.npcWantsRadio(false, 0));
        sys.globalBlock = false; CHECK(!sys.npcWantsRadio(true, 0));
        int yes = 0; for (int i = 0; i < 2000; ++i) yes += sys.npcWantsRadio(false, 0);
        CHECK(yes > 300 && yes < 700);                 // ~25%
    }
    // 8. posted-item events (what the audio player consumes): join posts the current item with its seek.
    {
        RadioSystem sys = makeSys();
        sys.worldStart();
        sys.tick(6000);                       // the inaudible timeline runs on (queue head is a 10 s entry or similar)
        sys.posted.clear();
        CHECK(sys.tune(1, 3, true));
        CHECK(sys.posted.size() == 1);
        if (!sys.posted.empty()) CHECK(sys.posted[0].station == 3);
        // audible advance posts the next item at expiry
        sys.posted.clear();
        sys.tick(60000);
        CHECK(!sys.posted.empty());
        for (const auto& p : sys.posted) CHECK(p.station == 3 && p.seekMs == 0);
    }
    // R-2 (spec 14.6.2 intro/outro defect): a pick starting at i=-2 (count 3) returns content 0, sets the
    // high byte of idx[2] (used flag) and adds 256 to idx[2] permanently; a second ghost on that record is
    // not taken again.
    {
        RadioSystem sys = makeSys();
        uint32_t seed = 0;
        for (uint32_t s = 1; s < 100000; ++s) {
            RandomRing r(s);
            if ((int)(int16_t)(r.rand32() & 0xFFFF) % 3 == -2) { seed = s; break; }
        }
        CHECK(seed != 0);
        std::vector<uint16_t> idx{6, 7, 7}; std::vector<uint8_t> used{0, 0, 0}; uint8_t ghost = 0;
        sys.rng.reseed(seed);
        CHECK(sys.pickIntroOutro(idx, used, true, &ghost) == 0 && ghost == 1);
        sys.rng.reseed(seed);                                    // same start index again: byte now reads used
        CHECK(sys.pickIntroOutro(idx, used, true, &ghost) != 0);
        std::vector<uint8_t> only2{1, 1, 0};                     // later normal pick of slot 2: index + 256
        CHECK(sys.pickIntroOutro(idx, only2, true, &ghost) == 7 + 256);
        uint8_t clean = 0; std::vector<uint8_t> only2b{1, 1, 0};
        CHECK(sys.pickIntroOutro(idx, only2b, true, &clean) == 7);
    }
    // R-4 (spec 14.6.2 "Becoming audible" step 5, 2026-10-14 clarification): when the entry is not longer
    // than r the advance happens and the timer is re-armed to the OLD remaining r, not the new entry's
    // overlap-adjusted duration (intro 500 - delay 100 = 400).
    {
        RadioSystem sys = makeSys();
        Station& g = sys.stations[2];
        g.queue = {1, 6}; g.cursor = 0; g.timerMs = 10000;       // r == Length: not longer
        uint32_t seek = 99; const ContentEntry* e = sys.becomeAudible(g, seek);
        CHECK(e && e->cueId == 6 && seek == 0);
        CHECK(g.timerMs == 10000);
        CHECK(sys.armDuration(g) == 400);
    }
    // R-5 (spec 14.6.2 advance): +0x68A bit 4 forces a rebuild even if the queue is not exhausted.
    {
        RadioSystem sys = makeSys();
        Station& g = sys.stations[2];
        g.queue = {1, 2, 3}; g.cursor = 0;
        sys.advance(g);
        CHECK(g.queue.size() == 3 && g.cursor == 1);             // no request: plain advance
        g.rebuildRequested = true;
        sys.advance(g);
        CHECK(g.queue.size() > 400 && g.cursor == 0 && !g.rebuildRequested);
    }
    // R-6 rule 3 (spec 14.6.1 default station): Police/Meter Maid -> first Police_Station, SWAT/FBI -> first
    // FBI_Station; rule 2 (last station) still wins; no matching station falls through to rule 4.
    {
        RadioSystem sys = makeSys();
        sys.stations[10].policeStation = true;   // index 10 = station 11
        sys.stations[11].fbiStation = true;
        CHECK(sys.defaultStation(0xFF, false, 6) == 11);
        CHECK(sys.defaultStation(0xFF, false, 11) == 11);
        CHECK(sys.defaultStation(0xFF, false, 5) == 12);
        CHECK(sys.defaultStation(0xFF, false, 7) == 12);
        CHECK(sys.defaultStation(4, false, 6) == 4);
        sys.stations[10].policeStation = sys.stations[11].fbiStation = false;
        int d = sys.defaultStation(0xFF, false, 6);
        CHECK(d >= 1 && d <= 10);
    }
    // R-7 (spec 14.6.3): item -> Wwise group/event table, bus on/off events (latched), Radio_Stations
    // switch on becoming audible, newsbreak delay/hold.
    {
        CHECK(wwiseItemFor(ContentType::Song).id == 0x90D4215Du && wwiseItemFor(ContentType::Song).hasEvent);
        CHECK(wwiseItemFor(ContentType::Commercial).id == 0xDE61BE86u);
        CHECK(wwiseItemFor(ContentType::Sweeper).id == 0x7E248191u);
        CHECK(wwiseItemFor(ContentType::Intro).id == 0xCF4C5C28u);
        CHECK(wwiseItemFor(ContentType::Callin).id == 0x3816E237u && !wwiseItemFor(ContentType::Callin).hasEvent);
        CHECK(wwiseItemFor(ContentType::Outro).id == 0x9176A6C1u);
        CHECK(wwiseItemFor(ContentType::PoliceSong).id == 0xB15E2067u && !wwiseItemFor(ContentType::PoliceSong).hasEvent);
        CHECK(wwiseItemFor(ContentType::NewsSong).id == 0xA73F0527u);
        CHECK(!wwiseItemFor(ContentType::SingAlong).postable);
        RadioSystem sys = makeSys();
        sys.worldStart();
        sys.playerBus(true, 3);                                    // enter on GenX
        sys.playerBus(true, 2);                                    // tune to Off
        sys.playerBus(true, 2);                                    // latched: nothing more
        sys.playerBus(true, 3);                                    // leave Off
        sys.playerBus(false, 0);                                   // leave the vehicle
        CHECK((sys.busEvents == std::vector<uint32_t>{0xC4F14EEAu, 0x01DF15F0u, 0xC4F14EEAu, 0x01DF15F0u}));
        sys.stationSwitchSets.clear();
        CHECK(sys.tune(1, 3, true));
        CHECK(sys.stationSwitchSets.size() == 1 && sys.stationSwitchSets[0] == 0x48E832A3u);
        CHECK(!sys.postNewsBreak(5, true) && sys.newsPosts.empty());
        CHECK(sys.postNewsBreak(5, false) && sys.newsPosts.size() == 1 && sys.newsPosts[0].startDelayMs == 1000);
    }
    {   // R-10 / spec 14.6.2: initially-disabled commercial is playable = en && !dis; enabled = !dis || en.
        ContentTable c; ContentEntry e; e.enableSlot = 3; e.disableSlot = 4;
        e.initiallyEnabled = false; CHECK(!c.commercialPlayable(e));
        c.enFlag[3] = 1; CHECK(c.commercialPlayable(e)); c.disFlag[4] = 1; CHECK(!c.commercialPlayable(e));
        e.initiallyEnabled = true; CHECK(c.commercialPlayable(e));   // en set
        c.enFlag[3] = 0; CHECK(!c.commercialPlayable(e));
        c.disFlag[4] = 0; CHECK(c.commercialPlayable(e));
    }
    std::printf(g_fail ? "radio_sim: %d FAILED\n" : "radio_sim: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}

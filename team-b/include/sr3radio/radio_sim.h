// sr3radio - free-roam vehicle radio logic (null audio backend model).
// Source: spec-audio-runtime.md Sec14.6 (FINAL items only). Pure logic, no I/O.
//  - MT19937 ring (Sec14.6.2 "Random source")        FINAL
//  - bag pick 0x0055D690, intro/outro pick 0x0055D750 (incl. engine defect, optional)
//  - queue builder 0x0055D7D0, advance 0x0055DB40
//  - tune gate 0x005602E0/0x0055ED20, cycle 0x00560580, default station 0x0055DEB0
//  - continuous timeline tick 0x0055FD90 (inaudible/audible, overlap-adjusted duration)
// CHOSEN stand-ins are marked CHOSEN. OPEN: Wwise attenuation (2D in-car feel), ducking.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sr3radio {

// Standard MT19937 feeding an 8192-value ring. seed 0 on a never-seeded generator == 5489.
class RandomRing {
public:
    explicit RandomRing(uint32_t seed = 5489u) { reseed(seed); }
    void reseed(uint32_t seed) {
        mt_[0] = seed;
        for (int i = 1; i < 624; ++i) mt_[i] = 0x6C078965u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + (uint32_t)i;
        idx_ = 624;
        ring_.resize(8192);
        for (auto& v : ring_) v = next();
        cursor_ = 0;
    }
    uint32_t rand32() { if (cursor_ >= ring_.size()) cursor_ = 0; ++draws_; return ring_[cursor_++]; }
    int32_t randInt(int32_t lo, int32_t hi) { return lo + (int32_t)(rand32() % (uint32_t)(hi - lo + 1)); }
    float randFloat() { return (float)((double)rand32() * (1.0 / 4294967296.0)); }
    size_t draws() const { return draws_; }
private:
    uint32_t next() {
        if (idx_ >= 624) {
            for (int k = 0; k < 624; ++k) {
                uint32_t y = (mt_[k] & 0x80000000u) | (mt_[(k + 1) % 624] & 0x7FFFFFFFu);
                mt_[k] = mt_[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1u) ? 0x9908B0DFu : 0u);
            }
            idx_ = 0;
        }
        uint32_t y = mt_[idx_++];
        y ^= y >> 11; y ^= (y << 7) & 0x9D2C5680u; y ^= (y << 15) & 0xEFC60000u; y ^= y >> 18;
        return y;
    }
    uint32_t mt_[624]{};
    int idx_ = 624;
    std::vector<uint32_t> ring_;
    size_t cursor_ = 0;
    size_t draws_ = 0;
};

enum class ContentType : uint8_t { Song = 0, Commercial = 1, Sweeper = 2, Intro = 3, Callin = 4, Outro = 5,
                                   PoliceSong = 6, NewsSong = 7, SingAlong = 8 };

struct ContentEntry {          // one row of the shared content table (stride 20, 0x013BBC18)
    uint32_t cueId = 0;        // FNV-1 of Cue
    uint32_t lengthMs = 0;
    std::string cue;           // cue name (diagnostics only; not in the engine row)
    uint16_t delayMs = 0;      // Delay (intro) or Prestart (outro)
    ContentType type = ContentType::Song;
    uint8_t enableSlot = 0xFF; // +0x11
    uint8_t disableSlot = 0xFF;// +0x12
    bool initiallyEnabled = true;  // +0x13 (commercials.xtbl InitialState; only "Disabled" clears it)
};

struct UsedList {              // {count, content indices, used flags}
    std::vector<uint16_t> idx;
    std::vector<uint8_t> used;
    void setup(std::vector<uint16_t> v) { idx = std::move(v); used.assign(idx.size(), 0); }
};

struct SongRecord {            // 0x48-byte record, up to 3 intros / 3 outros
    std::vector<uint16_t> intros, outros;
    std::vector<uint8_t> introUsed, outroUsed;
    // High byte of intros[2] (record +0x05). A pick starting at i = -2 (count 3) reads it as the used flag,
    // finds 0, writes 1: intros[2] reads +256 from then on (spec 14.6.2 intro/outro defect).
    uint8_t introGhostByte = 0;
};

struct ContentTable {
    std::vector<ContentEntry> entries;
    uint8_t enFlag[31]{}; uint8_t disFlag[31]{};   // commercial-event flag bytes per slot 0..30
    std::vector<SongRecord> songRecords;
    std::vector<int> songRecordOf;                 // content index -> songRecords index or -1
    int songRecordFor(uint16_t content) const {
        return content < songRecordOf.size() ? songRecordOf[content] : -1;
    }
    // 0x0055BFA0. Slot > 30 (incl. 0xFF) reads 0. (Spec says slot 30 is admitted; modelled as <=30.)
    // Initial state is commercials.xtbl InitialState (+0x13), spec 14.6.2: Disabled -> playable = en && !dis.
    bool commercialPlayable(const ContentEntry& e) const {
        bool en = e.enableSlot <= 30 && enFlag[e.enableSlot] != 0;
        bool dis = e.disableSlot <= 30 && disFlag[e.disableSlot] != 0;
        bool initDis = !e.initiallyEnabled;
        return initDis ? (en && !dis) : (!dis || en);
    }
};

struct Station {
    std::string name;              // +0x40 Game_display_resource, e.g. "RADIO_OFF"
    std::string hudImage;          // +0x80 Interface_image ("" = none)
    uint32_t wwiseId = 0;          // +0x140
    bool selectable = false;       // bits 5&6
    bool isMixTape = false;        // bit 3 (station 0); bit 5 = list non-empty (mixTapeNonEmpty)
    bool mixTapeNonEmpty = false;
    bool hidden = false;           // bit 7
    bool policeStation = false, fbiStation = false, newsStation = false;
    bool noDjTalk = false;         // +0x689 bit 4
    int8_t introPct = 0, outroPct = 0;
    UsedList songs, commercials, sweepers;
    std::vector<ContentType> pattern;  // Song / Commercial / Sweeper
    // runtime
    std::vector<uint16_t> queue; size_t cursor = 0; int patternPos = 0;
    std::vector<uint16_t> mixTapeList;
    bool audible = false;
    bool rebuildRequested = false; // +0x68A bit 4: advance rebuilds even if the queue is not exhausted
    int64_t timerMs = -1;          // remaining ms on deadline, -1 = not armed
    std::vector<int> listeners;    // vehicle handles (max 8)
    uint32_t postedEntries = 0;

    bool acceptsTune() const {     // 0x0055ED20 gate
        bool bit5 = isMixTape ? mixTapeNonEmpty : selectable;
        return bit5 && !hidden && listeners.size() < 8;
    }
};

// Item -> Wwise (spec 14.6.3, 0x0055FAA0, FINAL). SetSwitch(group, cueId) then PostEvent(group id) on the
// station voice; the switch group and the event share one id. hasEvent=false: no event in any shipped bank.
struct WwiseItem { uint32_t id = 0; bool postable = false; bool hasEvent = false; };
inline WwiseItem wwiseItemFor(ContentType t) {
    switch (t) {
        case ContentType::Song:       return {0x90D4215Du, true, true};   // Radio_Songs
        case ContentType::Commercial: return {0xDE61BE86u, true, true};   // Radio_Commercials
        case ContentType::Sweeper:    return {0x7E248191u, true, true};   // Radio_Sweepers
        case ContentType::Intro:      return {0xCF4C5C28u, true, true};   // Radio_Intros
        case ContentType::Callin:     return {0x3816E237u, true, false};  // Radio_Callins
        case ContentType::Outro:      return {0x9176A6C1u, true, true};   // Radio_Outros
        case ContentType::PoliceSong: return {0xB15E2067u, true, false};  // Radio_Police_FBI
        case ContentType::NewsSong:   return {0xA73F0527u, true, true};   // Radio_News_Breaks
        case ContentType::SingAlong:  break;                              // posts nothing
    }
    return {};
}
constexpr uint32_t kWwiseRadioStationsSwitch = 0x02C9B0FEu;  // set once when a station becomes audible
constexpr uint32_t kWwiseBusOnEvent  = 0xC4F14EEAu;          // entering a vehicle whose station is not Off
constexpr uint32_t kWwiseBusOffEvent = 0x01DF15F0u;          // leaving / tuning Off
constexpr uint32_t kNewsBreakEvent = 0xA73F0527u;            // posted on the player vehicle voice
constexpr uint32_t kNewsBreakStartDelayMs = 1000;            // 0x0055DC80

// One "post this content item on that station" call the audio backend must act on (spec 14.6.2: Post +
// Seek). Produced by becomeAudible() and by the audible-station tick advance; drained by the player.
struct PostedItem { int station = 0; uint16_t content = 0; uint32_t seekMs = 0; };

class RadioSystem {
public:
    ContentTable content;
    std::vector<PostedItem> posted;  // drain after tick()/tune()
    std::vector<uint32_t> busEvents; // global 2D voice events (kWwiseBusOnEvent/OffEvent), drained by the player
    std::vector<uint32_t> stationSwitchSets;  // Radio_Stations switch value (station id) per audible transition
    struct NewsPost { uint32_t cueId; uint32_t startDelayMs; };
    std::vector<NewsPost> newsPosts;
    bool busLatched = false;         // byte 0x013C60E6
    // 0x0055E110(on, station): bus events are posted when the on/off state changes (latched).
    void playerBus(bool inVehicle, int stationNumber) {
        const bool off = stationNumber < 1 || stations[(size_t)stationNumber - 1].name == "RADIO_OFF";
        const bool want = inVehicle && !off;
        if (want == busLatched) return;
        busLatched = want;
        busEvents.push_back(want ? kWwiseBusOnEvent : kWwiseBusOffEvent);
    }
    // Lua radio_newsbreak: held back while any of the 3 active-flag slots is set; else posted on the
    // player vehicle voice with a 1000 ms start delay.
    bool postNewsBreak(uint32_t cueId, bool anyActiveFlag) {
        if (anyActiveFlag) return false;
        newsPosts.push_back({cueId, kNewsBreakStartDelayMs});
        return true;
    }
    std::vector<Station> stations;   // index = number-1
    RandomRing rng;
    bool globalBlock = false;        // 0x013BB7D4
    int simultaneousNpcRadios = 1;
    float ambientSpawnAcquireRadio = 0.25f;

    // Bag pick 0x0055D690 (no wrap-around). Returns index into list, -1 on empty list.
    int bagPick(UsedList& l, bool commercials) {
        const int n = (int)l.idx.size();
        if (n == 0) return -1;
        for (int guard = 0; guard < 4096; ++guard) {
            int r = (int)(rng.rand32() % (uint32_t)n);
            for (int i = r; i < n; ++i) {
                if (l.used[i]) continue;
                if (commercials && !content.commercialPlayable(content.entries[l.idx[i]])) continue;
                l.used[i] = 1; return i;
            }
            std::fill(l.used.begin(), l.used.end(), (uint8_t)0);
        }
        return -1;  // CHOSEN: engine loops forever if all ads disabled; we bail out instead
    }

    // Intro/outro pick 0x0055D750. faithfulDefect reproduces the negative-index quirk
    // (a ghost slot yields content entry 0); the +256 corruption of later picks is NOT
    // modelled (CHOSEN). With faithfulDefect=false the start index is wrapped to >= 0.
    int pickIntroOutro(std::vector<uint16_t>& idx, std::vector<uint8_t>& used, bool faithfulDefect,
                       uint8_t* ghostByte = nullptr) {
        const int count = (int)idx.size();
        if (count == 0) return -1;
        int i = (int)(int16_t)(rng.rand32() & 0xFFFF) % count;   // C truncating remainder
        if (i < 0 && !faithfulDefect) i += count;
        for (int probe = 0; probe <= count + 1; ++probe) {
            if (i < 0) {
                // i=-2 (count 3) reads idx[2]'s high byte as a used flag: 0 -> taken, set to 1 (idx[2] += 256
                // permanently), returns content 0. Once set it reads as used and the probe moves on.
                if (i == -2 && count == 3 && ghostByte && *ghostByte == 0) { *ghostByte = 1; return 0; }
                i = (i + 1) % count;                              // i=-1 reads count byte (non-zero)
                continue;
            }
            if (!used[(size_t)i]) {
                used[(size_t)i] = 1;
                return idx[(size_t)i] + ((i == 2 && ghostByte) ? 256 * (int)*ghostByte : 0);
            }
            i = (i + 1) % count;
        }
        std::fill(used.begin(), used.end(), (uint8_t)0);
        return idx[(size_t)std::max(0, i)];
    }

    // Queue builder 0x0055D7D0.
    void buildQueue(Station& s, bool faithfulIntroDefect = true) {
        s.queue.clear(); s.cursor = 0;
        s.rebuildRequested = false;   // build end clears +0x68A bit 4
        if (s.pattern.empty()) return;
        const int pc = (int)s.pattern.size();
        int steps = s.patternPos;   // saved position +0x19E seeds the step counter (spec 14.6.2 clarification)
        int slot = 0;               // slot type = pattern[n mod count] from entry 0 of THIS rebuild
        while (s.queue.size() < 512 && steps < 512) {
            ContentType t = s.pattern[(size_t)(slot % pc)];
            ++slot;
            ++steps;
            if (t == ContentType::Sweeper) {
                int k = bagPick(s.sweepers, false);
                if (k >= 0) s.queue.push_back(s.sweepers.idx[(size_t)k]);
            } else if (t == ContentType::Commercial) {
                int k = bagPick(s.commercials, true);
                if (k >= 0) s.queue.push_back(s.commercials.idx[(size_t)k]);
            } else {
                int k = bagPick(s.songs, false);
                if (k < 0) continue;
                uint16_t song = s.songs.idx[(size_t)k];
                int intro = -1, outro = -1;
                int rec = content.songRecordFor(song);
                bool talk = s.queue.size() < 511 && !s.noDjTalk && rec >= 0;
                if (talk) {
                    SongRecord& r = content.songRecords[(size_t)rec];
                    if (r.introUsed.size() != r.intros.size()) r.introUsed.assign(r.intros.size(), 0);
                    if (r.outroUsed.size() != r.outros.size()) r.outroUsed.assign(r.outros.size(), 0);
                    if ((int)(rng.rand32() % 100) < s.introPct && !r.intros.empty())
                        intro = pickIntroOutro(r.intros, r.introUsed, faithfulIntroDefect, &r.introGhostByte);
                    if (intro < 0 && (int)(rng.rand32() % 100) < s.outroPct && !r.outros.empty())
                        outro = pickIntroOutro(r.outros, r.outroUsed, faithfulIntroDefect);
                }
                if (intro >= 0) s.queue.push_back((uint16_t)intro);
                s.queue.push_back(song);
                if (outro >= 0) s.queue.push_back((uint16_t)outro);
            }
        }
        // CHOSEN: the exact end-of-build value of +0x19E is not spelled out beyond "position"; we store
        // the step counter mod pattern length (matches the vector: final position 2 on the first build).
        s.patternPos = steps % pc;
    }

    // Advance 0x0055DB40.
    void advance(Station& s) {
        ++s.cursor;
        if (s.cursor >= s.queue.size() || s.rebuildRequested) {
            if (s.isMixTape) { s.queue = s.mixTapeList; s.cursor = 0; }
            else buildQueue(s);
        }
    }
    const ContentEntry* current(const Station& s) const {
        if (s.cursor >= s.queue.size() || s.queue[s.cursor] >= content.entries.size()) return nullptr;  // +256 defect can overrun
        return &content.entries[s.queue[s.cursor]];
    }
    // Duration to arm for the current entry (0x0055FAA0 overlap rule).
    uint32_t armDuration(const Station& s) const {
        const ContentEntry* e = current(s);
        if (!e) return 1000;
        if (e->type == ContentType::Intro) return e->lengthMs - std::min<uint32_t>(e->lengthMs, e->delayMs);
        if (s.cursor + 1 < s.queue.size()) {
            if (s.queue[s.cursor + 1] >= content.entries.size()) return e->lengthMs;
            const ContentEntry& n = content.entries[s.queue[s.cursor + 1]];
            if (n.type == ContentType::Outro) return e->lengthMs - std::min<uint32_t>(e->lengthMs, n.delayMs);
        }
        return e->lengthMs;
    }

    // World start / load: queues are never saved; rebuild every non-mixtape queue, tick once.
    void worldStart() {
        for (auto& s : stations) {
            if (s.isMixTape) { s.queue = s.mixTapeList; s.cursor = 0; s.mixTapeNonEmpty = !s.mixTapeList.empty(); }
            else buildQueue(s);
        }
        tick(0);
    }

    // Per-frame tick 0x005607B0/0x0055FD90, dt in ms.
    void tick(uint32_t dtMs) {
        for (size_t si = 0; si < stations.size(); ++si) {
            Station& s = stations[si];
            if (s.pattern.empty() && !s.isMixTape) continue;      // Off, News, ...
            if (s.isMixTape && s.mixTapeList.empty()) continue;
            if (s.queue.empty()) { if (s.isMixTape) s.queue = s.mixTapeList; else buildQueue(s); }
            if (s.queue.empty()) continue;
            auto armFor = [&](const Station& st) -> int64_t {
                if (st.audible) return armDuration(st);
                const ContentEntry* c = current(st);
                return c ? c->lengthMs : 1000;
            };
            if (s.timerMs < 0) { s.timerMs = armFor(s); continue; }
            s.timerMs -= (int64_t)dtMs;
            int guard = 0;
            while (s.timerMs <= 0 && guard++ < 64) {
                int64_t overshoot = -s.timerMs;
                advance(s);
                if (!s.audible) { int g2 = 0; while (current(s) && current(s)->type == ContentType::Outro && g2++ < 8) advance(s); }
                else { ++s.postedEntries; if (current(s)) posted.push_back({(int)si + 1, s.queue[s.cursor], 0}); }
                s.timerMs = armFor(s) - overshoot;
            }
            if (s.timerMs <= 0) s.timerMs = 1;
        }
    }

    // First listener makes a station audible (Sec14.6.2 "Becoming audible", step 4).
    // Returns the entry to post and the seek position in ms.
    const ContentEntry* becomeAudible(Station& s, uint32_t& seekMs) {
        seekMs = 0; s.audible = true;
        stationSwitchSets.push_back(s.wwiseId);
        const ContentEntry* e = current(s);
        if (!e) return nullptr;
        int64_t r = s.timerMs < 0 ? (int64_t)e->lengthMs : s.timerMs;
        if ((int64_t)e->lengthMs > r) seekMs = (uint32_t)((int64_t)e->lengthMs - r);
        else { advance(s); e = current(s); if (!e) r = 1000; }   // timer re-armed to the OLD remaining r (spec 14.6.2 step 5 clarification)
        s.timerMs = r; ++s.postedEntries;
        if (e) posted.push_back({(int)(&s - stations.data()) + 1, s.queue[s.cursor], seekMs});
        return e;
    }

    // ---- vehicle side
    int stationOf(int vehicle) const {
        for (size_t i = 0; i < stations.size(); ++i)
            for (int h : stations[i].listeners) if (h == vehicle) return (int)i + 1;
        return 0;
    }
    void unregister(int vehicle) {
        for (auto& s : stations) removeListener(s, vehicle);
    }
    // 0x005602E0. True on success or if already tuned. vehicleHasTuner = def+0x870 bit 26.
    bool tune(int vehicle, int stationNumber, bool vehicleHasTuner) {
        if (!vehicleHasTuner) return false;
        if (stationNumber < 1 || stationNumber > (int)stations.size()) return false;
        Station& t = stations[(size_t)stationNumber - 1];
        if (t.hidden) return false;
        int old = stationOf(vehicle);
        if (old == stationNumber) return true;
        if (!t.acceptsTune()) return false;
        t.listeners.push_back(vehicle);
        if (old) removeListener(stations[(size_t)old - 1], vehicle);
        if (!t.audible) { uint32_t seek; becomeAudible(t, seek); }
        return true;
    }
    // 0x00560580: circular raw walk, first accepted wins. dir = +1 / -1.
    int cycle(int vehicle, int dir, bool vehicleHasTuner, bool controlsLocked = false) {
        if (controlsLocked || stations.empty()) return stationOf(vehicle);
        int n = (int)stations.size();
        int cur = stationOf(vehicle);
        int idx = cur ? cur - 1 : (dir > 0 ? n - 1 : 0);
        for (int step = 0; step < n; ++step) {
            idx = ((idx + dir) % n + n) % n;
            if (tune(vehicle, idx + 1, vehicleHasTuner)) return idx + 1;
        }
        return cur;
    }
    // 0x0055DEB0 default station on entering. lastStation = vehicle +0x1630 (0xFF = none).
    // The Police/FBI rule yields 0xFF in retail (no such stations) and is omitted.
    // Rule 3 (0x0055A680): Special_Vehicle_Type Police(6)/Meter Maid(11) -> first Police_Station;
    // SWAT Van(5)/FBI(7) -> first FBI_Station; none exists in retail (0xFF) -> falls on to rule 4.
    int defaultStation(int lastStation, bool forceKlassic, int specialVehicleType = -1) {
        if (forceKlassic)
            for (size_t i = 0; i < stations.size(); ++i) if (stations[i].name == "LOGO_KLASSIC") return (int)i + 1;
        if (lastStation != 0xFF && lastStation >= 1) return lastStation;
        if (specialVehicleType == 6 || specialVehicleType == 11 || specialVehicleType == 5 || specialVehicleType == 7) {
            const bool police = specialVehicleType == 6 || specialVehicleType == 11;
            for (size_t i = 0; i < stations.size(); ++i)
                if (police ? stations[i].policeStation : stations[i].fbiStation) return (int)i + 1;
        }
        int n = (int)stations.size();
        for (int tries = 0; tries < 1000; ++tries) {
            int k = rng.randInt(0, n);   // inclusive upper bound as in the engine; k==n re-rolled (CHOSEN)
            if (k >= n) continue;
            const Station& s = stations[(size_t)k];
            if (s.policeStation || s.fbiStation) continue;
            if (!(s.isMixTape ? s.mixTapeNonEmpty : s.selectable) || s.hidden) continue;
            return k + 1;
        }
        return 0;
    }
    // HUD popup image for the station the vehicle is on ("" = none).
    std::string hudImage(int vehicle) const {
        int n = stationOf(vehicle);
        return n ? stations[(size_t)n - 1].hudImage : std::string();
    }
    // NPC acquisition (0x0055ACD0 shape). Persona/class/random choice is left to the caller.
    bool npcWantsRadio(bool vehicleBlocked, int activeNpcRadios) {
        if (vehicleBlocked || globalBlock) return false;
        if (rng.randFloat() > ambientSpawnAcquireRadio) return false;
        return activeNpcRadios < simultaneousNpcRadios;
    }

private:
    static void removeListener(Station& s, int vehicle) {
        for (size_t k = 0; k < s.listeners.size(); ++k)
            if (s.listeners[k] == vehicle) { s.listeners.erase(s.listeners.begin() + (long)k); break; }
        if (s.listeners.empty()) s.audible = false;   // timer NOT reset: timeline carries on
    }
};

// Retail roster, spec-audio-runtime.md Sec14.6.1 table (FINAL). Playlists are left empty:
// per-station cue tables are not parsed in this repo yet, so no tracks are queued.
inline std::vector<Station> retailRoster() {
    struct R { const char* name; const char* img; uint32_t id; bool sel; bool news; };
    static const R rows[] = {
        {"RADIO_OFF", "", 0x37798A64u, true, false},
        {"LOGO_GENX", "ui_radio_89_gen_x", 0x48E832A3u, true, false},
        {"LOGO_KRHYME", "ui_radio_954_khryme", 0xD11F2385u, true, false},
        {"LOGO_K12", "ui_radio_976_k12", 0xFA83B765u, true, false},
        {"LOGO_KLASSIC", "ui_radio_1024_klassic", 0x7EBD4413u, true, false},
        {"LOGO_ADULT_SWIM", "ui_radio_wddt_adult_swim", 0xF024CB66u, true, false},
        {"LOGO_KABRON", "ui_radio_1042_kabron", 0x304556E6u, true, false},
        {"LOGO_KRUNCH", "ui_radio_10666_the_blood", 0x4EC69BA6u, true, false},
        {"LOGO_MIX", "ui_radio_10777_the_mix", 0x5DB98EB5u, true, false},
        {"", "", 0u, false, true},     // radio_newsbreaks
        {"", "", 0u, false, false},    // credits_radio
    };
    std::vector<Station> v;
    Station mix; mix.name = "My Radio 85.5"; mix.hudImage = "ui_radio_mixtape"; /* HIGH, not FINAL */ mix.isMixTape = true;
    v.push_back(mix);
    for (const R& r : rows) {
        Station s; s.name = r.name; s.hudImage = r.img; s.wwiseId = r.id; s.selectable = r.sel; s.newsStation = r.news;
        v.push_back(s);
    }
    return v;
}

}  // namespace sr3radio

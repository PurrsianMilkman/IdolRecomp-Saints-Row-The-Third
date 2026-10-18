// sr3radio content loader. See include/sr3radio/radio_content.h.
#include "sr3radio/radio_content.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "sr3tables_audio_radio/tables.h"
#include "sr3xtbl/xtbl.h"

namespace sr3radio {

using sr3xtbl::Node;

uint32_t wwiseHash(const std::string& name) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c : name) {
        h *= 0x01000193u;
        h ^= (uint32_t)std::tolower(c);
    }
    return h;
}

namespace {

constexpr size_t kContentCap = 925;   // 0x39D (spec 14.6.2)
constexpr size_t kSongRecordCap = 299;

std::string text(const Node* n, const char* child) {
    const std::string* s = sr3xtbl::ChildText(n, child);
    return s ? *s : std::string();
}

int32_t asInt(const std::string& s) { return s.empty() ? 0 : sr3xtbl::ParseInt32(s); }

struct Loader {
    RadioSystem& sys;
    RadioLoadReport& rep;

    // 0x0055C950: register a cue; a duplicate hash returns the existing index.
    int registerCue(const std::string& cue, uint32_t length, uint16_t delay, ContentType type) {
        const uint32_t id = wwiseHash(cue);
        auto& e = sys.content.entries;
        for (size_t i = 0; i < e.size(); ++i)
            if (e[i].cueId == id) return (int)i;
        if (e.size() >= kContentCap) { rep.warnings.push_back("content table full at " + cue); return -1; }
        ContentEntry ce;
        ce.cue = cue; ce.cueId = id; ce.lengthMs = length; ce.delayMs = delay; ce.type = type;
        e.push_back(ce);
        return (int)e.size() - 1;
    }
    // 0x0055C910: look up only (intro/outro names).
    int lookupCue(const std::string& cue) const {
        const uint32_t id = wwiseHash(cue);
        const auto& e = sys.content.entries;
        for (size_t i = 0; i < e.size(); ++i)
            if (e[i].cueId == id) return (int)i;
        return -1;
    }

    // Registers every <Info> of `listName` as `type`; returns the content indices in file order.
    std::vector<uint16_t> registerList(const Node* row, const char* listName, ContentType type, bool hasDelay,
                                       const char* delayTag) {
        std::vector<uint16_t> out;
        const Node* list = sr3xtbl::FindChild(row, listName);
        for (const Node* info : sr3xtbl::Children(list, "Info")) {
            const std::string cue = text(info, "Cue");
            if (cue.empty()) continue;
            uint32_t len = (uint32_t)asInt(text(info, "Length"));
            uint16_t d = hasDelay ? (uint16_t)asInt(text(info, delayTag)) : 0;
            int idx = registerCue(cue, len, d, type);
            if (idx >= 0) out.push_back((uint16_t)idx);
        }
        return out;
    }

    bool loadStation(const std::vector<uint8_t>& bytes, Station& st) {
        sr3xtbl::Document doc = sr3xtbl::ParseDocument(bytes.data(), bytes.size());
        const Node* row = sr3xtbl::FindChild(doc.table(), "NewEntity");
        if (!row) return false;
        const Node* set = sr3xtbl::FindChild(row, "Radio_Station_Settings");
        st.introPct = (int8_t)asInt(text(set, "Intro_percent"));
        st.outroPct = (int8_t)asInt(text(set, "Outro_percent"));
        st.name = text(set, "Game_display_resource");
        st.hudImage = text(set, "Interface_image");

        // Registration order follows the record offsets: sweepers, commercials, intros, call-ins,
        // outros, sing-alongs, songs (+0x148..+0x190). Verified by the retail GENX indices (sweepers
        // 0-1, commercials 2-82, intros 83-136, outros 137-172, songs 173-190).
        std::vector<uint16_t> sweepers = registerList(row, "Sweeper_list", ContentType::Sweeper, false, "");
        std::vector<uint16_t> comms = registerList(row, "Commercial_list", ContentType::Commercial, false, "");
        registerList(row, "Intro_list", ContentType::Intro, true, "Delay");
        registerList(row, "CallIn_list", ContentType::Callin, true, "Delay");
        registerList(row, "Outro_list", ContentType::Outro, true, "Prestart");
        registerList(row, "Singalong_list", ContentType::SingAlong, false, "");
        // CHOSEN: spec registers the intro/outro lists into the table but a song's Intros/Outros are
        // resolved by name against the whole table; same here.

        ContentType songType = ContentType::Song;
        if (st.policeStation || st.fbiStation) songType = ContentType::PoliceSong;
        else if (st.newsStation) songType = ContentType::NewsSong;

        std::vector<uint16_t> songs;
        const Node* sl = sr3xtbl::FindChild(row, "Song_List");
        for (const Node* info : sr3xtbl::Children(sl, "Info")) {
            const std::string cue = text(info, "Cue");
            if (cue.empty()) continue;
            int idx = registerCue(cue, (uint32_t)asInt(text(info, "Length")), 0, songType);
            if (idx < 0) continue;
            songs.push_back((uint16_t)idx);
            if (sys.content.songRecordOf.size() <= (size_t)idx) sys.content.songRecordOf.resize((size_t)idx + 1, -1);
            if (sys.content.songRecordOf[(size_t)idx] >= 0) continue;   // duplicate song keeps its record
            if (sys.content.songRecords.size() >= kSongRecordCap) { rep.warnings.push_back("song record pool full"); continue; }
            SongRecord rec;
            auto fill = [&](const char* listTag, std::vector<uint16_t>& dst) {
                for (const Node* c : sr3xtbl::Children(sr3xtbl::FindChild(info, listTag), "Cue")) {
                    if (!c->text()) continue;
                    int k = lookupCue(*c->text());
                    if (k < 0) rep.warnings.push_back("unknown intro/outro cue " + *c->text());   // spec 14.6.6: stored as 0xFFFF
                    if (dst.size() < 3) dst.push_back((uint16_t)(k < 0 ? 0xFFFF : k));   // record holds at most 3
                }
            };
            fill("Intros", rec.intros);
            fill("Outros", rec.outros);
            rec.introUsed.assign(rec.intros.size(), 0);
            rec.outroUsed.assign(rec.outros.size(), 0);
            sys.content.songRecordOf[(size_t)idx] = (int)sys.content.songRecords.size();
            sys.content.songRecords.push_back(std::move(rec));
        }

        st.songs.setup(songs);
        st.commercials.setup(comms);
        st.sweepers.setup(sweepers);
        st.pattern.clear();
        const Node* pl = sr3xtbl::FindChild(row, "Station_Playlist");
        for (const Node* pe : sr3xtbl::Children(pl, "Playlist_entry")) {
            const std::string* t = pe->text();
            ContentType ty = ContentType::Song;   // anything else reads as Song
            if (t && sr3xtbl::NameEquals(*t, "Commercial")) ty = ContentType::Commercial;
            else if (t && sr3xtbl::NameEquals(*t, "Sweeper")) ty = ContentType::Sweeper;
            st.pattern.push_back(ty);
        }
        return true;
    }
};

}  // namespace

bool loadRadioContent(const XtblProvider& provider, RadioSystem& sys, RadioLoadReport& report, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!provider("radio_stations.xtbl", bytes)) { err = "radio_stations.xtbl not found"; return false; }
    sr3xtbl::Document listDoc = sr3xtbl::ParseDocument(bytes.data(), bytes.size());
    auto settings = sr3tables_audio_radio::ParseRadioSettings(listDoc);
    if (!settings) { err = "radio_stations.xtbl has no settings row"; return false; }

    sys.content = ContentTable{};
    sys.stations.clear();
    sys.simultaneousNpcRadios = settings->SimultaneousNpcRadiosOrDefault();

    Station mix; mix.name = "My Radio 85.5"; mix.hudImage = "ui_radio_mixtape"; /* HIGH, not FINAL */ mix.isMixTape = true;
    sys.stations.push_back(mix);

    Loader ld{sys, report};
    for (const auto& info : settings->stations) {
        Station st;
        st.selectable = info.selectable;
        st.policeStation = info.policeStation;
        st.fbiStation = info.fbiStation;
        st.newsStation = info.newsStation;
        st.wwiseId = info.wwiseId ? wwiseHash(*info.wwiseId) : 0u;
        if (!info.filename) { err = "station row without xtbl_name"; return false; }
        std::vector<uint8_t> sb;
        if (!provider(*info.filename, sb)) { err = "station file not found: " + *info.filename; return false; }
        try {
            if (!ld.loadStation(sb, st)) { err = "station file has no NewEntity: " + *info.filename; return false; }
        } catch (const std::exception& ex) { err = *info.filename + ": " + ex.what(); return false; }
        ++report.stationFilesLoaded;
        sys.stations.push_back(std::move(st));
    }

    // commercial_events.xtbl (30 slots) and commercials.xtbl (patches existing entries only).
    std::vector<sr3tables_audio_radio::CommercialEvent> events;
    if (provider("commercial_events.xtbl", bytes)) {
        sr3xtbl::Document d = sr3xtbl::ParseDocument(bytes.data(), bytes.size());
        events = sr3tables_audio_radio::ParseCommercialEventsTable(d);
    } else report.warnings.push_back("commercial_events.xtbl missing");
    // New-game flag state {1, 2, 6, 27} (spec 14.6.4 item 2 / spec-save-format 12.8). The enable and
    // disable bytes read the same flag array (spec 14.6.2 "Commercial enabled test").
    for (int slot : {1, 2, 6, 27}) { sys.content.enFlag[slot] = 1; sys.content.disFlag[slot] = 1; }
    if (provider("commercials.xtbl", bytes)) {
        sr3xtbl::Document d = sr3xtbl::ParseDocument(bytes.data(), bytes.size());
        for (const auto& c : sr3tables_audio_radio::ParseCommercialsTable(d)) {
            if (!c.name) continue;
            const uint32_t id = wwiseHash(*c.name);
            for (auto& e : sys.content.entries) {
                if (e.cueId != id) continue;
                if (c.length.present) e.lengthMs = (uint32_t)c.length.value;
                e.initiallyEnabled = c.EnabledOrDefault();
                auto slotOf = [&](const std::optional<std::string>& ev) -> uint8_t {
                    if (!ev) return 0xFF;
                    int32_t v = sr3tables_audio_radio::ResolveCommercialEventValue(*ev, events);
                    return v < 0 ? (uint8_t)0xFF : (uint8_t)v;
                };
                e.enableSlot = slotOf(c.enableEvent);
                e.disableSlot = slotOf(c.disableEvent);
                break;
            }
        }
    } else report.warnings.push_back("commercials.xtbl missing");

    report.contentEntries = sys.content.entries.size();
    report.songRecords = sys.content.songRecords.size();
    return true;
}

}  // namespace sr3radio

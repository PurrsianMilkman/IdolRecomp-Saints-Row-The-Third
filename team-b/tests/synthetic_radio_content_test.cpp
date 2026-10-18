// Synthetic loader tests for sr3radio content (spec-audio-runtime.md 14.6.2 / 14.6.6).
#include "sr3radio/radio_content.h"
#include "sr3radio/radio_sim.h"
#include <cstdio>
#include <map>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
using namespace sr3radio;

int main() {
    std::map<std::string, std::string> files;
    files["radio_stations.xtbl"] = R"(<root><Table><NewEntity><Radio_Station_List>
      <Info><xtbl_name><Filename>s.xtbl</Filename></xtbl_name><Station_flags><Flag>Selectable</Flag></Station_flags><wwise_id>S</wwise_id></Info>
    </Radio_Station_List></NewEntity></Table></root>)";
    files["s.xtbl"] = R"(<root><Table><NewEntity><Radio_Station_Settings><Intro_percent>40</Intro_percent><Outro_percent>40</Outro_percent></Radio_Station_Settings>
      <Intro_list><Info><Cue>INTRO_A</Cue><Length>100</Length><Delay>10</Delay></Info></Intro_list>
      <Outro_list><Info><Cue>OUTRO_A</Cue><Length>100</Length><Prestart>10</Prestart></Info></Outro_list>
      <Song_List><Info><Cue>SONG_A</Cue><Length>5000</Length>
        <Intros><Cue>INTRO_A</Cue><Cue>NO_SUCH_INTRO</Cue><Cue>INTRO_A</Cue><Cue>INTRO_A</Cue></Intros>
        <Outros><Cue>NO_SUCH_OUTRO</Cue></Outros></Info></Song_List>
      <Station_Playlist><Playlist_entry>Song</Playlist_entry></Station_Playlist></NewEntity></Table></root>)";
    XtblProvider prov = [&](const std::string& n, std::vector<uint8_t>& b) {
        auto it = files.find(n); if (it == files.end()) return false;
        b.assign(it->second.begin(), it->second.end()); return true;
    };
    RadioSystem sys; RadioLoadReport rep; std::string err;
    CHECK(loadRadioContent(prov, sys, rep, err));
    CHECK(sys.content.songRecords.size() == 1);
    if (sys.content.songRecords.size() == 1) {
        const SongRecord& r = sys.content.songRecords[0];
        // 14.6.6: unknown name -> index -1 stored as 0xFFFF (occupies a slot); max 3 per record.
        CHECK((r.intros == std::vector<uint16_t>{0, 0xFFFF, 0}));
        CHECK((r.outros == std::vector<uint16_t>{0xFFFF}));
    }
    std::printf(g_fail ? "radio_content: %d FAILED\n" : "radio_content: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}

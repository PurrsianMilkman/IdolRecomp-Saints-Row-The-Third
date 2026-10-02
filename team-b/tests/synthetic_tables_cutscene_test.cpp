// Synthetic tests for sr3tables_cutscene (the zscene / cutscene scene table),
// built FROM THE TEXT of spec-lua-api-behaviour.md Sec26.25 ("Scene entry
// (stride 0xf8)" and lifecycle item 5, job nnlt) - hand-written XML fixtures,
// not derived from the reader's source. The real-data statement is
// lua_host_run's zscene_table= line on the shipped cutscene_tables.vpp_pc.
//
// Mutation checks (each was applied to src/tables_cutscene.cpp by hand and
// made at least one CHECK below fail; reverted afterwards):
//   M1 "Designer" -> 2 instead of 0                    (testKinds)
//   M2 capacity = accepted + 12 without the 200 cap     (testNameFilter)
//   M3 soundtrack split keeping the space before ':'    (testSoundtrack)
//   M4 main filter skipping "dlc" case-insensitively    (testNameFilter)
//   M5 strncpy length 63 instead of 64                  (testLightset)
//   M6 resources built for a zscene                     (testResources)

#include <cctype>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "sr3tables_cutscene/scene_table.h"

namespace {

using namespace sr3tables_cutscene;
using namespace sr3xtbl;

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":" << __LINE__   \
                      << "\n";                                                    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

SceneFields parseCutscene(const std::string& inner, const ParserOptions& o = {}) {
    const Document d = ParseDocument("<root><Table><Cutscene>" + inner + "</Cutscene></Table></root>");
    return ParseSceneFile(d, o);
}

void testKinds() {
    // "Zscene" -> 1 (2 when 0x0153ba02 is set), "Story" -> 2, "Designer" -> 0,
    // any other text -> 2.
    CHECK(parseCutscene("<CutsceneType>Zscene</CutsceneType>").kind == std::optional<int>(1));
    ParserOptions storyOpt;
    storyOpt.zsceneAsStory = true;
    CHECK(parseCutscene("<CutsceneType>Zscene</CutsceneType>", storyOpt).kind == std::optional<int>(2));
    CHECK(parseCutscene("<CutsceneType>Story</CutsceneType>").kind == std::optional<int>(2));
    CHECK(parseCutscene("<CutsceneType>Designer</CutsceneType>").kind == std::optional<int>(0));
    CHECK(parseCutscene("<CutsceneType>Video</CutsceneType>").kind == std::optional<int>(2));
    CHECK(parseCutscene("<CutsceneType>Zscene </CutsceneType>").kind == std::optional<int>(2)); // text is never trimmed
    // OPEN: no text, or a case-only variant (the comparison's case rule is not stated).
    CHECK(!parseCutscene("").kind);
    CHECK(!parseCutscene("<CutsceneType></CutsceneType>").kind);
    CHECK(!parseCutscene("<CutsceneType>zscene</CutsceneType>").kind);
    CHECK(!parseCutscene("<CutsceneType>DESIGNER</CutsceneType>").kind);
    CHECK(parseCutscene("<CutsceneType>Story</CutsceneType>").cutsceneTypeText == std::optional<std::string>("Story"));
    // The element name match is case-insensitive (sr3xtbl, traffic-ai 1.3).
    CHECK(parseCutscene("<cutscenetype>Zscene</cutscenetype>").kind == std::optional<int>(1));
    // No Cutscene element: everything OPEN.
    const SceneFields none = ParseSceneFile(ParseDocument("<root><Table><Other/></Table></root>"));
    CHECK(!none.cutsceneElementFound && !none.kind && !none.resources);
}

void testLightset() {
    const SceneFields absent = parseCutscene("<CutsceneType>Zscene</CutsceneType>");
    CHECK(!absent.hasLightset && !absent.lightsetText);
    const SceneFields shortName = parseCutscene("<SceneLightset>ls_a</SceneLightset>");
    CHECK(shortName.hasLightset && shortName.lightsetText == std::optional<std::string>("ls_a"));
    CHECK(!shortName.lightsetUnterminated);
    // strncpy into 64 bytes: 63 characters still get a NUL; 64 or more do not.
    const std::string s63(63, 'a'), s64(64, 'b'), s80(80, 'c');
    const SceneFields f63 = parseCutscene("<SceneLightset>" + s63 + "</SceneLightset>");
    CHECK(f63.lightsetText == std::optional<std::string>(s63) && !f63.lightsetUnterminated);
    const SceneFields f64 = parseCutscene("<SceneLightset>" + s64 + "</SceneLightset>");
    CHECK(f64.lightsetText == std::optional<std::string>(s64) && f64.lightsetUnterminated);
    const SceneFields f80 = parseCutscene("<SceneLightset>" + s80 + "</SceneLightset>");
    CHECK(f80.lightsetText == std::optional<std::string>(std::string(64, 'c')) && f80.lightsetUnterminated);
    // Present with no text: has a lightset (REAL), text OPEN.
    const SceneFields empty = parseCutscene("<SceneLightset/>");
    CHECK(empty.hasLightset && !empty.lightsetText);
}

void testSoundtrack() {
    const SceneFields absent = parseCutscene("<CutsceneType>Zscene</CutsceneType>");
    CHECK(!absent.soundtrackPresent && absent.soundtrackIdsKnownZero()); // REAL: both 0 when absent
    // The shipped form `left : right`: one character dropped on each side.
    const SceneFields st = parseCutscene("<Soundtrack>CINEMATIC : CT_PIERCE_01</Soundtrack>");
    CHECK(st.soundtrackPresent && !st.soundtrackIdsKnownZero() && !st.soundtrackSplitOpen);
    CHECK(st.soundtrackLeft == std::optional<std::string>("CINEMATIC"));
    CHECK(st.soundtrackRight == std::optional<std::string>("CT_PIERCE_01"));
    // Not the spaced form: still one character each side, as stated.
    const SceneFields tight = parseCutscene("<Soundtrack>AB:CD</Soundtrack>");
    CHECK(tight.soundtrackLeft == std::optional<std::string>("A") && tight.soundtrackRight == std::optional<std::string>("D"));
    // Split at the FIRST colon.
    const SceneFields two = parseCutscene("<Soundtrack>A : B : C</Soundtrack>");
    CHECK(two.soundtrackLeft == std::optional<std::string>("A") && two.soundtrackRight == std::optional<std::string>("B : C"));
    // OPEN shapes.
    CHECK(parseCutscene("<Soundtrack>NOCOLON</Soundtrack>").soundtrackSplitOpen);
    CHECK(parseCutscene("<Soundtrack>:X</Soundtrack>").soundtrackSplitOpen);
    CHECK(parseCutscene("<Soundtrack>X:</Soundtrack>").soundtrackSplitOpen);
    CHECK(parseCutscene("<Soundtrack/>").soundtrackSplitOpen);
    CHECK(!parseCutscene("<Soundtrack>X:</Soundtrack>").soundtrackRight);
    // "X :" has one character after the colon: right half empty, left "X".
    const SceneFields edge = parseCutscene("<Soundtrack>X : </Soundtrack>");
    CHECK(!edge.soundtrackSplitOpen && edge.soundtrackLeft == std::optional<std::string>("X") &&
          edge.soundtrackRight == std::optional<std::string>(""));
}

void testResources() {
    const std::string chars =
        "<Characters>"
        "<Character><Name>#PLAYER#</Name><Variant>default</Variant></Character>"
        "<Character><Name>Pierce</Name><Mesh>npc_pierce</Mesh></Character>"
        "<Character><Name>K</Name><Mesh>npc_k</Mesh><Killbane>npc_Killbane_nomask</Killbane></Character>"
        "<Character><Name>Angel</Name><Mesh>npc_angel</Mesh><IsAngel>True</IsAngel></Character>"
        "</Characters>"
        "<Vehicles><Vehicle><Name>Heli</Name><Variant>heist</Variant></Vehicle>"
        "<Vehicle><Name>Car</Name><Mesh>car_a</Mesh></Vehicle></Vehicles>";
    // Story: Mesh and Killbane names, the IsAngel placeholder, the Variant texts.
    const SceneFields story = parseCutscene("<CutsceneType>Story</CutsceneType>" + chars);
    CHECK(story.resources.has_value());
    if (story.resources) {
        const auto& r = *story.resources;
        CHECK(r.size() == 6);
        if (r.size() == 6) {
            CHECK(r[0].source == SceneResource::Source::CharacterMesh && r[0].text == "npc_pierce");
            CHECK(r[1].source == SceneResource::Source::CharacterMesh && r[1].text == "npc_k");
            CHECK(r[2].source == SceneResource::Source::CharacterKillbane && r[2].text == "npc_Killbane_nomask");
            CHECK(r[3].source == SceneResource::Source::AngelFixedNames && !r[3].textPresent); // names OPEN
            CHECK(r[4].source == SceneResource::Source::VehicleVariant && r[4].text == "heist" && r[4].textPresent);
            CHECK(r[5].source == SceneResource::Source::VehicleVariant && !r[5].textPresent);
        }
    }
    // "Empty for a zscene."
    const SceneFields z = parseCutscene("<CutsceneType>Zscene</CutsceneType>" + chars);
    CHECK(z.resources.has_value() && z.resources->empty());
    // Designer and an OPEN kind: not described -> OPEN.
    CHECK(!parseCutscene("<CutsceneType>Designer</CutsceneType>" + chars).resources);
    CHECK(!parseCutscene(chars).resources);
}

Document names(const std::vector<std::string>& list) {
    std::string x = "<Root><Table>";
    for (const auto& n : list) x += "<Cutscene><Name>" + n + "</Name></Cutscene>";
    x += "<Cutscene><Other>1</Other></Cutscene></Table></Root>";
    return ParseDocument(x);
}

void testNameFilter() {
    const Document d = names({"01_z01", "dlc1_x", "patch_y", "Bob_CTE", "DLC2_z", "dlcfoo", "patchwork"});
    const NameFilterResult m = AcceptedSceneNames(d, "main");
    CHECK(m.cutsceneElements == 8 && m.elementsWithoutName == 1);
    CHECK(m.accepted == (std::vector<std::string>{"01_z01", "Bob_CTE"}));
    CHECK(m.skipped == (std::vector<std::string>{"dlc1_x", "patch_y", "dlcfoo", "patchwork"}));
    CHECK(m.caseAmbiguous == (std::vector<std::string>{"DLC2_z"})); // case rule OPEN
    // A patch container takes only the names starting with its own name.
    const NameFilterResult p = AcceptedSceneNames(d, "dlc1");
    CHECK(p.accepted == (std::vector<std::string>{"dlc1_x"}));
    const NameFilterResult p2 = AcceptedSceneNames(d, "dlc2");
    CHECK(p2.accepted.empty() && p2.caseAmbiguous == (std::vector<std::string>{"DLC2_z"}));
    // At most 200; capacity = accepted + 12, capped at 200.
    std::vector<std::string> many;
    for (int i = 0; i < 230; ++i) many.push_back("s" + std::to_string(i));
    const NameFilterResult big = AcceptedSceneNames(names(many), "main");
    CHECK(big.accepted.size() == 200 && big.cappedAt200);
    CHECK(MainCapacity(109) == 121);
    CHECK(MainCapacity(188) == 200);
    CHECK(MainCapacity(189) == 200);
    CHECK(MainCapacity(200) == 200);
    CHECK(MainCapacity(0) == 12);
}

void testBuild() {
    const std::string list =
        "<Root><Table><Cutscene><Name>Z_A</Name></Cutscene><Cutscene><Name>story_b</Name></Cutscene>"
        "<Cutscene><Name>dlc_c</Name></Cutscene><Cutscene><Name>missing_d</Name></Cutscene>"
        "<Cutscene><Name>bad_e</Name></Cutscene></Table></Root>";
    std::map<std::string, std::string> files = {
        {"z_a.cte_xtbl", "<root><Table><Cutscene><CutsceneType>Zscene</CutsceneType></Cutscene></Table></root>"},
        {"story_b.cte_xtbl", "<root><Table><Cutscene><CutsceneType>Story</CutsceneType></Cutscene></Table></root>"},
        {"dlc_c.cte_xtbl", "<root><Table><Cutscene><CutsceneType>Zscene</CutsceneType></Cutscene></Table></root>"},
        {"bad_e.cte_xtbl", "not xml"},
    };
    std::vector<std::string> opened;
    const SceneTable t = BuildMainSceneTable(list, [&](const std::string& fileName) -> std::optional<OpenedFile> {
        opened.push_back(fileName);
        std::string lower;
        for (char c : fileName) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto it = files.find(lower);
        if (it == files.end()) return std::nullopt;
        return OpenedFile{it->first, it->second};
    });
    // "<name>.cte_xtbl" is asked for with the name as written; dlc_c is never opened.
    CHECK(opened == (std::vector<std::string>{"Z_A.cte_xtbl", "story_b.cte_xtbl", "missing_d.cte_xtbl", "bad_e.cte_xtbl"}));
    CHECK(t.names.accepted.size() == 4 && t.capacity == 16);
    CHECK(t.entries.size() == 2);
    CHECK(t.missingSceneFiles == (std::vector<std::string>{"missing_d"}));
    CHECK(t.unparseableSceneFiles == (std::vector<std::string>{"bad_e.cte_xtbl"}));
    if (t.entries.size() == 2) {
        CHECK(t.entries[0].name == "Z_A" && t.entries[0].fields.kind == std::optional<int>(1));
        CHECK(t.entries[0].crc == NameHash("z_a", 0));
        CHECK(t.entries[1].name == "story_b" && t.entries[1].fields.kind == std::optional<int>(2));
    }
    // 0x00721be0: CRC of the lower-cased name, first match.
    CHECK(t.find("z_A") == &t.entries[0]);
    CHECK(t.find("STORY_B") == &t.entries[1]);
    CHECK(t.find("dlc_c") == nullptr && t.find("missing_d") == nullptr);
}

} // namespace

int main() {
    testKinds();
    testLightset();
    testSoundtrack();
    testResources();
    testNameFilter();
    testBuild();
    std::cout << "sr3tables_cutscene: " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

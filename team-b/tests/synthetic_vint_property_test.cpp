// Synthetic tests (2026-10-03) for typed vint_set_property/vint_get_property:
// the per-type property-descriptor tables (spec-lua-bindings.md Sec9.1/
// Sec9.2/Sec9.2a, Sec20.1/Sec20.2 - include/sr3luahost/vint_property_types.h)
// and the query-time screen_nw/screen_se/screen_size geometry plus the
// screen_size setter (spec-vint-render.md Sec7/Sec8). No game data.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "sr3luahost/host.h"
#include "sr3luahost/vint_property_types.h"
#include "sr3save/save_crc.h"

using namespace sr3luahost;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "CHECK FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) <= eps; }

VintTaggedValue num(double d) { VintTaggedValue v; v.kind = VintTaggedValue::Kind::Number; v.number = d; return v; }
VintTaggedValue boolean(bool b) { VintTaggedValue v; v.kind = VintTaggedValue::Kind::Boolean; v.boolean = b; return v; }
VintTaggedValue str(const std::string& s) { VintTaggedValue v; v.kind = VintTaggedValue::Kind::String; v.text = s; return v; }

template <typename F>
bool throwsOpen(F f, const char* needle = nullptr) {
    try {
        f();
    } catch (const OpenStateError& e) {
        if (needle && std::string(e.what()).find(needle) == std::string::npos) {
            std::cerr << "  (OpenStateError without '" << needle << "': " << e.what() << ")\n";
            return false;
        }
        return true;
    }
    return false;
}

std::vector<double> nums(const std::optional<std::vector<VintTaggedValue>>& v) {
    std::vector<double> out;
    if (!v) return out;
    for (const auto& x : *v) out.push_back(x.number);
    return out;
}

} // namespace

int main() {
    // --- 1. the descriptor tables (Sec9.1/Sec9.2) --------------------------
    {
        const auto& types = vintElementTypes();
        CHECK(types.size() == 13);
        const char* order[13] = {"element", "tween", "animation", "bitmap", "gradient", "text", "group",
                                 "document", "point", "clip", "bitmap_circle", "sr2_map", "video"};
        const size_t rows[13] = {17, 16, 3, 5, 8, 21, 4, 1, 1, 2, 7, 2, 9};
        size_t total = 0, typed = 0, spec = 0, field = 0;
        for (size_t i = 0; i < types.size() && i < 13; ++i) {
            CHECK(std::string(types[i].name) == order[i]);
            CHECK(types[i].rows.size() == rows[i]);
            for (const auto& r : types[i].rows) {
                ++total;
                CHECK(r.nameHash == sr3save::nameHash(r.name));
                CHECK(r.code != 0xc); // Sec9.2: no row uses 0xc
                if (r.code != kVintCodeOpen) ++typed;
                if (r.source == VintCodeSource::Spec) ++spec;
                if (r.source == VintCodeSource::FieldType) ++field;
                CHECK((r.code == kVintCodeOpen) == (r.source == VintCodeSource::Open));
            }
        }
        CHECK(total == 96);
        CHECK(typed == 43 && spec + field == 43);
        // Exactly the 10 0xd rows of Sec9.2a and the 3 0xe rows.
        size_t enums = 0, variants = 0;
        for (const auto& t : types)
            for (const auto& r : t.rows) {
                enums += r.code == kVintCodeEnum;
                variants += r.code == kVintCodeVariant;
            }
        CHECK(enums == 10 && variants == 3);
        // Parent chain (Sec9.1): gradient -> bitmap -> element; the three bases have none.
        CHECK(vintTypeIsA("gradient", "bitmap") && vintTypeIsA("gradient", "element"));
        CHECK(!vintTypeIsA("tween", "element") && !vintTypeIsA("animation", "element"));
        CHECK(vintIsElementFamily("video") && !vintIsElementFamily("tween") && !vintIsElementFamily("nonsense"));
        // Own table first, then parents.
        auto img = findVintPropertyDescriptor("gradient", sr3save::nameHash("image"));
        CHECK(img && std::string(img->owner->name) == "bitmap" && img->row->code == kVintCodeImageName);
        auto alpha = findVintPropertyDescriptor("gradient", sr3save::nameHash("ALPHA")); // lower-cased hash
        CHECK(alpha && std::string(alpha->owner->name) == "element" && alpha->row->code == kVintCodeFloat);
        auto grpOffset = findVintPropertyDescriptor("group", sr3save::nameHash("offset"));
        CHECK(grpOffset && std::string(grpOffset->owner->name) == "group"); // group's own re-listing wins
        CHECK(!findVintPropertyDescriptor("tween", sr3save::nameHash("alpha")));
        CHECK(!findVintPropertyDescriptor("bitmap", sr3save::nameHash("zoom")));
        CHECK(!findVintPropertyDescriptor("not_a_type", sr3save::nameHash("alpha")));
        // Arity (Sec20.1/Sec20.2).
        CHECK(vintCodeArity(kVintCodeVec2) == 2 && vintCodeArity(kVintCodeVec3) == 3 && vintCodeArity(kVintCodeEnum) == 1);
        CHECK(vintCodeArity(kVintCodeOpen) == 0 && vintCodeArity(kVintCodeVariant) == 0);
        // Enumerations (Sec9.2a).
        int32_t v = -1;
        CHECK(lookupVintEnum("text", "horz_align", "Right", &v) == VintEnumLookup::Found && v == 2);
        CHECK(lookupVintEnum("group", "auto_offset", "sw", &v) == VintEnumLookup::Found && v == 9); // ancestor binding
        CHECK(lookupVintEnum("tween", "state", "waiting", &v) == VintEnumLookup::Found && v == 0); // idle duplicate
        CHECK(lookupVintEnum("tween", "algorithm", "ease_in_out_bounce", &v) == VintEnumLookup::Found && v == 24);
        CHECK(lookupVintEnum("tween", "algorithm", "ease_out_quart", &v) == VintEnumLookup::Open); // not in clean text
        CHECK(lookupVintEnum("element", "render_mode", "bogus", &v) == VintEnumLookup::Miss);
        CHECK(lookupVintEnum("sr2_map", "map_mode", "MINIMAP", &v) == VintEnumLookup::Found && v == 1);
    }

    // --- 2. typed set/get, EngineState level ------------------------------
    {
        EngineState es;
        uint32_t g = es.registerTypedVdoObjectForTesting("grp", "group", 0, 0);
        uint32_t b = es.registerTypedVdoObjectForTesting("bmp", "bitmap", g, 0);
        CHECK(es.vdoObjectFirstChild(g) == b);
        // Element defaults (spec-vint-render.md Sec8).
        CHECK(nums(es.getVintProperty(b, "alpha")) == std::vector<double>{1});
        CHECK(nums(es.getVintProperty(b, "tint")) == (std::vector<double>{1, 1, 1}));
        CHECK(nums(es.getVintProperty(b, "scale")) == (std::vector<double>{1, 1}));
        CHECK(nums(es.getVintProperty(b, "anchor")) == (std::vector<double>{0, 0}));
        CHECK(es.getVintProperty(b, "visible") && (*es.getVintProperty(b, "visible"))[0].boolean == true);
        // mouse_depth follows depth while unset (Sec1).
        es.setVintProperty(b, "depth", {num(-7)});
        CHECK(nums(es.getVintProperty(b, "mouse_depth")) == std::vector<double>{-7});
        es.setVintProperty(b, "mouse_depth", {num(3)});
        CHECK(nums(es.getVintProperty(b, "mouse_depth")) == std::vector<double>{3});
        // Floats are 32-bit fields.
        es.setVintProperty(b, "alpha", {num(0.1)});
        CHECK(nums(es.getVintProperty(b, "alpha"))[0] == static_cast<double>(0.1f));
        // Exactly arity values consumed; extra trailing values ignored.
        es.setVintProperty(b, "anchor", {num(1), num(2), num(3)});
        CHECK(nums(es.getVintProperty(b, "anchor")) == (std::vector<double>{1, 2}));
        CHECK(es.findVintStoredProperty(b, "anchor")->typedAs == kVintCodeVec2);
        // Shape refusals (OPEN: the switch's handling of mismatched input is not specified).
        CHECK(throwsOpen([&] { es.setVintProperty(b, "anchor", {num(5)}); }, "takes 2"));
        CHECK(throwsOpen([&] { es.setVintProperty(b, "visible", {num(1)}); }, "number"));
        CHECK(throwsOpen([&] { es.setVintProperty(b, "depth", {num(2.5)}); }, "non-whole"));
        CHECK(throwsOpen([&] { es.setVintProperty(b, "image", {num(4)}); }));
        CHECK(nums(es.getVintProperty(b, "anchor")) == (std::vector<double>{1, 2})); // refusals wrote nothing
        // Name miss (CONFIRMED): no write, zero values.
        es.setVintProperty(b, "no_such", {num(1)});
        es.setVintProperty(b, "zoom", {num(1)}); // sr2_map's row, not on bitmap's chain
        CHECK(!es.getVintProperty(b, "no_such") && !es.getVintProperty(b, "zoom"));
        CHECK(es.findVintProperty(b, "zoom") == nullptr);
        CHECK(es.vintPropertyStats().setNameMiss == 2 && es.vintPropertyStats().getNameMiss == 2);
        // A Spec-coded string row.
        es.setVintProperty(b, "image", {str("ui_blank")});
        CHECK(es.getVintProperty(b, "image") && (*es.getVintProperty(b, "image"))[0].text == "ui_blank");
        // An OPEN-coded row: CHOSEN pass-through, stored and echoed as given.
        es.setVintProperty(b, "image_crc", {str("anything"), num(9)});
        auto crc = es.getVintProperty(b, "image_crc");
        CHECK(crc && crc->size() == 2 && (*crc)[0].text == "anything" && es.findVintStoredProperty(b, "image_crc")->typedAs == 0);
        // A subtype row never set: no default in any spec -> OPEN.
        CHECK(throwsOpen([&] { es.getVintProperty(b, "source_nw"); }, "no value known"));
        // Enumerations: names resolve to their value; the get push form is OPEN.
        es.setVintProperty(b, "auto_offset", {str("w")});
        CHECK(es.findVintStoredProperty(b, "auto_offset")->typedAs == kVintCodeEnum &&
              es.findVintProperty(b, "auto_offset")->at(0).number == 2);
        CHECK(throwsOpen([&] { es.getVintProperty(b, "auto_offset"); }, "type code 13"));
        // Fail-stub setters (spec-vint-render.md Sec7.3): nothing written.
        es.setVintProperty(b, "screen_nw", {num(1), num(1)});
        es.setVintProperty(b, "unscaled_size", {num(5), num(5)});
        CHECK(es.findVintProperty(b, "screen_nw") == nullptr && es.findVintProperty(b, "unscaled_size") == nullptr);
        CHECK(es.vintPropertyStats().setReadOnlyNoop == 2);

        uint32_t t = es.registerTypedVdoObjectForTesting("txt", "text", g, 0);
        es.setVintProperty(t, "horz_align", {str("RIGHT")});
        CHECK(es.findVintProperty(t, "horz_align")->at(0).number == 2);
        es.setVintProperty(t, "horz_align", {num(1)});
        CHECK(es.findVintProperty(t, "horz_align")->at(0).number == 1);
        es.setVintProperty(t, "insert_values", {str("a"), num(2)}); // 0xe outside the tween gate: raw
        CHECK(es.findVintStoredProperty(t, "insert_values")->typedAs == 0 && es.findVintProperty(t, "insert_values")->size() == 2);

        uint32_t tw = es.registerTypedVdoObjectForTesting("twn", "tween", 0, 0);
        es.setVintProperty(tw, "loop_mode", {str("cycle")});
        CHECK(es.findVintProperty(tw, "loop_mode")->at(0).number == 1);
        uint64_t before = es.vintPropertyStats().setRawEnumUnresolved;
        es.setVintProperty(tw, "algorithm", {str("ease_out_quart")}); // name not in clean text: raw
        CHECK(es.vintPropertyStats().setRawEnumUnresolved == before + 1 &&
              es.findVintProperty(tw, "algorithm")->at(0).text == "ease_out_quart");
        // Callback name (case 8): stored; the pool claim is not modelled; get push OPEN.
        es.setVintProperty(tw, "end_event", {str("vint_anim_loop_callback")});
        CHECK(es.findVintStoredProperty(tw, "end_event")->typedAs == kVintCodeCallback);
        CHECK(throwsOpen([&] { es.getVintProperty(tw, "end_event"); }, "type code 8"));
        CHECK(!es.getVintProperty(tw, "alpha")); // element row, not on tween's chain

        // Untyped (test-registered) objects: the old behaviour, unchanged.
        uint32_t u = es.registerVdoObjectForTesting("plain", 0, 0);
        CHECK(!es.getVintProperty(u, "scale"));
        es.setVintProperty(u, "anything_at_all", {num(1.1), str("x")});
        auto uv = es.getVintProperty(u, "anything_at_all");
        CHECK(uv && uv->size() == 2 && (*uv)[0].number == 1.1);
    }

    // --- 3. tween start_value/end_value: borrow the TARGET's code, write the
    // TWEEN's own property (Sec20.1, CORRECTED 2026-10-03) -----------------
    {
        EngineState es;
        uint32_t target = es.registerTypedVdoObjectForTesting("target", "bitmap", 0, 0);
        uint32_t tw = es.registerTypedVdoObjectForTesting("twn", "tween", 0, 0);
        // No target known yet: CHOSEN raw store.
        es.setVintProperty(tw, "start_value", {num(5), num(6), num(7)});
        CHECK(es.findVintStoredProperty(tw, "start_value")->typedAs == 0 && es.findVintProperty(tw, "start_value")->size() == 3);
        CHECK(es.vintPropertyStats().setRawTweenBorrowUnresolved == 1);
        es.setVintProperty(tw, "target_handle", {num(target)});
        es.setVintProperty(tw, "target_property", {str("anchor")});
        es.setVintProperty(tw, "start_value", {num(1.25), num(2.5), num(9)});
        const VintStoredProperty* sv = es.findVintStoredProperty(tw, "start_value");
        CHECK(sv && sv->typedAs == kVintCodeVec2 && sv->values.size() == 2 && sv->values[1].number == 2.5);
        CHECK(nums(es.getVintProperty(tw, "start_value")) == (std::vector<double>{1.25, 2.5}));
        // The target is untouched - the write lands on the tween only.
        CHECK(nums(es.getVintProperty(target, "anchor")) == (std::vector<double>{0, 0}));
        CHECK(es.vintPropertyStats().setTweenBorrowTyped == 1);
        // Case-insensitive gate, same stored property.
        es.setVintProperty(tw, "END_VALUE", {num(3), num(4)});
        CHECK(nums(es.getVintProperty(tw, "end_value")) == (std::vector<double>{3, 4}));
        // A float target: one value, 32-bit.
        es.setVintProperty(tw, "target_property", {str("alpha")});
        es.setVintProperty(tw, "end_value", {num(0.3)});
        CHECK(nums(es.getVintProperty(tw, "end_value")) == std::vector<double>{static_cast<double>(0.3f)});
        CHECK(throwsOpen([&] { es.setVintProperty(tw, "end_value", {str("x")}); }));
        // Target row with an OPEN code: raw.
        es.setVintProperty(tw, "target_property", {str("image_crc")});
        es.setVintProperty(tw, "end_value", {num(1), num(2)});
        CHECK(es.findVintStoredProperty(tw, "end_value")->typedAs == 0);
        // Target row not on the target's chain: raw (code not determinable).
        es.setVintProperty(tw, "target_property", {str("zoom")});
        es.setVintProperty(tw, "end_value", {num(8)});
        CHECK(es.findVintStoredProperty(tw, "end_value")->typedAs == 0);
        // A non-tween object with the same names gets no borrow (bitmap has no start_value row at all).
        es.setVintProperty(target, "start_value", {num(1)});
        CHECK(es.findVintProperty(target, "start_value") == nullptr);
    }

    // --- 4. query-time geometry (spec-vint-render.md Sec7) -----------------
    {
        EngineState es;
        // Sec7.2's worked example: bitmap directly under the root, anchor
        // (100,50), auto_offset c on unscaled_size (64,32) -> offset
        // (-32,-16) (the applier is not run here, so the offset is set
        // directly), scale (2,2), rotation 0 -> nw (36,18), se (164,82),
        // size (128,64). Document space - no screen fit.
        uint32_t b = es.registerTypedVdoObjectForTesting("bmp", "bitmap", 0, 0);
        CHECK(throwsOpen([&] { es.getVintProperty(b, "screen_nw"); }, "size path")); // bitmap size: OPEN until set
        es.setVintUnscaledSizeForTesting(b, 64, 32);
        es.setVintProperty(b, "anchor", {num(100), num(50)});
        es.setVintProperty(b, "offset", {num(-32), num(-16)});
        es.setVintProperty(b, "scale", {num(2), num(2)});
        CHECK(nums(es.getVintProperty(b, "screen_nw")) == (std::vector<double>{36, 18}));
        CHECK(nums(es.getVintProperty(b, "screen_se")) == (std::vector<double>{164, 82}));
        CHECK(nums(es.getVintProperty(b, "screen_size")) == (std::vector<double>{128, 64}));
        CHECK(nums(es.getVintProperty(b, "unscaled_size")) == (std::vector<double>{64, 32}));
        // Hidden / zero-alpha does not matter (Sec3: query geometry ignores culling).
        es.setVintProperty(b, "visible", {boolean(false)});
        es.setVintProperty(b, "alpha", {num(0)});
        CHECK(nums(es.getVintProperty(b, "screen_size")) == (std::vector<double>{128, 64}));

        // screen_size setter (Sec7.3): scale := scale * requested / current.
        es.setVintProperty(b, "screen_size", {num(256), num(64)});
        CHECK(nums(es.getVintProperty(b, "scale")) == (std::vector<double>{4, 2}));
        CHECK(nums(es.getVintProperty(b, "screen_size")) == (std::vector<double>{256, 64}));
        es.setVintProperty(b, "screen_size", {num(0), num(-5)}); // clamped to 1e-4 per axis
        auto sc = nums(es.getVintProperty(b, "scale"));
        CHECK(sc.size() == 2 && sc[0] > 0 && near(sc[0], 4 * 1e-4 / 256, 1e-9) && near(sc[1], 2 * 1e-4 / 64, 1e-9));
        CHECK(throwsOpen([&] { es.setVintProperty(b, "screen_size", {num(1)}); }, "takes 2"));

        // Divide-by-zero guard (HOST-SAFETY): a zero current size writes nothing.
        uint32_t z = es.registerTypedVdoObjectForTesting("zero", "element", 0, 0);
        es.setVintProperty(z, "scale", {num(0), num(3)});
        CHECK(nums(es.getVintProperty(z, "screen_size")) == (std::vector<double>{0, 3}));
        es.setVintProperty(z, "screen_size", {num(10), num(10)});
        CHECK(nums(es.getVintProperty(z, "scale")) == (std::vector<double>{0, 3}));
        CHECK(es.vintPropertyStats().screenSizeDivZeroGuards == 1);

        // Rotation and scale pivot about the anchor; offset is inside the
        // element's own frame (Sec5.2). element default unscaled_size (1,1).
        uint32_t e = es.registerTypedVdoObjectForTesting("e", "element", 0, 0);
        es.setVintProperty(e, "anchor", {num(10), num(10)});
        es.setVintProperty(e, "scale", {num(2), num(2)});
        CHECK(nums(es.getVintProperty(e, "screen_nw")) == (std::vector<double>{10, 10}));
        CHECK(nums(es.getVintProperty(e, "screen_se")) == (std::vector<double>{12, 12}));
        es.setVintProperty(e, "offset", {num(-0.5), num(-0.5)});
        CHECK(nums(es.getVintProperty(e, "screen_nw")) == (std::vector<double>{9, 9}));
        es.setVintProperty(e, "scale", {num(1), num(1)});
        es.setVintProperty(e, "offset", {num(0), num(0)});
        es.setVintProperty(e, "rotation", {num(1.5707963267948966)}); // +90 deg: clockwise on screen, y down
        auto nw = nums(es.getVintProperty(e, "screen_nw"));
        auto se = nums(es.getVintProperty(e, "screen_se"));
        CHECK(nw.size() == 2 && near(nw[0], 9) && near(nw[1], 10));   // (1,0) -> (0,1), (0,1) -> (-1,0)
        CHECK(se.size() == 2 && near(se[0], 10) && near(se[1], 11));

        // Children: a parent's offset does NOT move them; anchor/scale do.
        uint32_t g = es.registerTypedVdoObjectForTesting("g", "group", 0, 0);
        CHECK(nums(es.getVintProperty(g, "screen_size")) == (std::vector<double>{0, 0})); // childless: a point
        CHECK(throwsOpen([&] { es.getVintProperty(g, "screen_nw"); }, "collapsed point"));
        es.setVintProperty(g, "anchor", {num(10), num(20)});
        es.setVintProperty(g, "offset", {num(500), num(500)});
        uint32_t c1 = es.registerTypedVdoObjectForTesting("c1", "element", g, 0);
        es.setVintProperty(c1, "anchor", {num(1), num(2)});
        es.setVintProperty(c1, "scale", {num(4), num(2)});
        CHECK(nums(es.getVintProperty(c1, "screen_nw")) == (std::vector<double>{11, 22}));
        CHECK(nums(es.getVintProperty(c1, "screen_se")) == (std::vector<double>{15, 24}));
        es.setVintProperty(g, "scale", {num(2), num(1)}); // non-uniform parent scale
        CHECK(nums(es.getVintProperty(c1, "screen_nw")) == (std::vector<double>{12, 22}));
        CHECK(nums(es.getVintProperty(c1, "screen_se")) == (std::vector<double>{20, 24}));
        // group = union of its element children only; tween children skipped.
        uint32_t c2 = es.registerTypedVdoObjectForTesting("c2", "point", g, 0);
        es.registerTypedVdoObjectForTesting("anim_twn", "tween", g, 0);
        es.setVintProperty(c2, "anchor", {num(-3), num(0)});
        es.setVintProperty(c2, "scale", {num(5), num(5)});
        CHECK(nums(es.getVintProperty(c2, "screen_size")) == (std::vector<double>{1, 1})); // point: always (1,1)
        // ...but its box in a parent's union is the shared body: (-3,0)*2+(10,20) .. +(10,5)
        CHECK(nums(es.getVintProperty(g, "screen_nw")) == (std::vector<double>{4, 20}));
        CHECK(nums(es.getVintProperty(g, "screen_se")) == (std::vector<double>{20, 25}));
        CHECK(nums(es.getVintProperty(g, "screen_size")) == (std::vector<double>{16, 5}));
        // group / point screen_size setters fail (no write).
        es.setVintProperty(g, "screen_size", {num(1), num(1)});
        es.setVintProperty(c2, "screen_size", {num(9), num(9)});
        CHECK(nums(es.getVintProperty(g, "scale")) == (std::vector<double>{2, 1}));
        CHECK(nums(es.getVintProperty(c2, "scale")) == (std::vector<double>{5, 5}));
        // A childless group inside a union: its point is OPEN.
        es.registerTypedVdoObjectForTesting("empty_grp", "group", g, 0);
        CHECK(throwsOpen([&] { es.getVintProperty(g, "screen_size"); }, "collapsed point"));
        // text: layout OPEN.
        uint32_t t = es.registerTypedVdoObjectForTesting("t", "text", 0, 0);
        CHECK(throwsOpen([&] { es.getVintProperty(t, "screen_size"); }));
        CHECK(throwsOpen([&] { es.setVintProperty(t, "screen_size", {num(1), num(1)}); }, "layout-ready"));
        CHECK(throwsOpen([&] { es.getVintProperty(t, "unscaled_size"); }));
        // An untyped child under a typed parent: is-an-element unknown -> OPEN.
        uint32_t p2 = es.registerTypedVdoObjectForTesting("p2", "element", 0, 0);
        uint32_t plain = es.registerVdoObjectForTesting("plain", p2, 0);
        es.setVdoObjectFirstChildForTesting(p2, plain);
        CHECK(throwsOpen([&] { es.getVintProperty(p2, "screen_size"); }, "no registered type"));
    }

    // --- 5. through Lua (the real stubs) ------------------------------------
    {
        std::vector<RegisteredName> names = {{"vint_set_property", "ui"}, {"vint_get_property", "ui"}};
        Host host(names);
        lua_State* ui = host.uiState();
        EngineState& es = host.engineState();
        uint32_t b = es.registerTypedVdoObjectForTesting("bmp", "bitmap", 0, 0);
        uint32_t tw = es.registerTypedVdoObjectForTesting("twn", "tween", 0, 0);
        es.setVintUnscaledSizeForTesting(b, 64, 32);
        const std::string B = std::to_string(b), T = std::to_string(tw);
        auto r = host.runChunk(ui,
            "local b, t = " + B + ", " + T + "\n"
            "assert(select('#', vint_set_property(b, 'anchor', 100, 50)) == 0)\n"
            "vint_set_property(b, 'offset', -32, -16)\n"
            "vint_set_property(b, 'scale', 2, 2)\n"
            "local x, y = vint_get_property(b, 'screen_nw')\n"
            "assert(x == 36 and y == 18, 'screen_nw')\n"
            "local w, h = vint_get_property(b, 'screen_size')\n"
            "assert(w == 128 and h == 64, 'screen_size')\n"
            "assert(select('#', vint_get_property(b, 'tint')) == 3, 'vec3 pushes 3')\n"
            "assert(vint_get_property(b, 'visible') == true, 'bool default')\n"
            "assert(select('#', vint_get_property(b, 'no_such_property')) == 0, 'miss')\n"
            "vint_set_property(b, 'screen_size', 256, 128)\n"
            "local sx, sy = vint_get_property(b, 'scale')\n"
            "assert(sx == 4 and sy == 4, 'screen_size setter')\n"
            "vint_set_property(t, 'target_handle', b)\n"
            "vint_set_property(t, 'target_property', 'anchor')\n"
            "vint_set_property(t, 'start_value', 7, 8)\n"
            "local s1, s2 = vint_get_property(t, 'start_value')\n"
            "assert(s1 == 7 and s2 == 8, 'tween start_value')\n"
            "local ax, ay = vint_get_property(b, 'anchor')\n"
            "assert(ax == 100 and ay == 50, 'target untouched')\n"
            "vint_set_property(t, 'state', 'running')\n"
            "vint_set_property(t, 'loop_mode', 2)\n",
            "vint_prop_test.lua");
        CHECK(r.loadOk && r.pcallOk);
        if (!r.pcallOk) std::cerr << r.pcallError << "\n";
        // Refusals surface as Lua errors naming the OPEN item.
        auto r2 = host.runChunk(ui, "vint_set_property(" + B + ", 'anchor', 1)", "vint_prop_test.lua");
        CHECK(r2.loadOk && !r2.pcallOk && r2.pcallError.find("takes 2") != std::string::npos);
        auto r3 = host.runChunk(ui, "local v = vint_get_property(" + T + ", 'state')", "vint_prop_test.lua");
        CHECK(r3.loadOk && !r3.pcallOk && r3.pcallError.find("type code 13") != std::string::npos);
        CHECK(es.findVintProperty(tw, "state")->at(0).number == 1);
        CHECK(es.findVintProperty(tw, "loop_mode")->at(0).number == 2);
    }

    if (g_failures == 0) std::cout << "ALL vint property TESTS PASSED\n";
    return g_failures == 0 ? 0 : 1;
}

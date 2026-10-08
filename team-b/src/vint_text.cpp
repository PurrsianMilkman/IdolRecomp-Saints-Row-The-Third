// sr3font - Vint text string building, markup and layout (spec-vint-
// render.md Sec14.2-Sec14.5). See include/sr3font/vint_text.h for scope.
//
// Every rule below cites the spec sentence it implements. Where the spec is
// silent on an edge the shipped strings never reach, the choice is labelled
// CHOSEN with its reasoning; nothing here is taken from the executable.
#include "sr3font/vint_text.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sr3font {

namespace {

constexpr size_t kStreamCapacity = 0x3200; // 12,800 bytes (Sec12.2)
constexpr size_t kStyleEntries = 16;       // Sec14.3 working state
constexpr size_t kFormatBuffer = 4096;     // Sec14.2 step 3: the insertion output buffer, in UTF-16 units

double toF32(double v) { return static_cast<double>(static_cast<float>(v)); }

bool isHex(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int hexVal(uint32_t c) {
    if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
    return static_cast<int>(c - 'A' + 10);
}
uint32_t lowerAscii(uint32_t c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

// Case-insensitive "does `body` start with `name`".
bool ciStartsWith(const std::u16string& body, const char* name) {
    const size_t n = std::strlen(name);
    if (body.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (lowerAscii(body[i]) != static_cast<uint32_t>(name[i])) return false;
    return true;
}

std::string narrowLow(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) out.push_back(static_cast<char>(c & 0xff));
    return out;
}

} // namespace

// ---- Sec14.2 --------------------------------------------------------------

std::optional<std::u16string> decodeRawTextTag(const std::string& raw, std::string& openReason) {
    if (!raw.empty() && static_cast<unsigned char>(raw[0]) == 0x80) {
        openReason = "text_tag is in the 0x80-tagged UI string form (decoder 0x00e22ac0): its per-character "
                     "record decode is not specified byte-exactly in clean spec text (spec-vint-render.md Sec14.2 "
                     "step 2 -> spec-lua-api-behaviour.md Sec2.3)";
        return std::nullopt;
    }
    // Widened byte-for-byte (0x00db06e0, HIGH).
    std::u16string out;
    out.reserve(raw.size());
    for (char c : raw) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    return out;
}

FormatResult formatInsertions(const std::u16string& src, bool valuesSet, const std::vector<InsertValue>& values) {
    FormatResult r;
    // Sec14.2: count 32 when set, otherwise no values at all.
    const size_t count = valuesSet ? 32 : 0;
    size_t nextValue = 0; // CHOSEN: `{}` and `%s` share one "next value in order" counter (the spec says
                          // "the next value in order" for both and names no second counter)
    auto valueAt = [&](size_t idx) -> InsertValue {
        if (idx < values.size()) return values[idx];
        return InsertValue{}; // Nil
    };
    auto out = [&](const std::u16string& s) {
        for (char16_t c : s) {
            // CHOSEN: the 4,096-unit buffer is treated as 4,095 text units
            // plus a terminator; text past it is dropped (no shipped string
            // comes near - the longest English string is 371 characters).
            if (r.text.size() + 1 >= kFormatBuffer) return;
            r.text.push_back(c);
        }
    };
    // Inserts value `idx` per the `{N}` rule: beyond the count -> nothing;
    // nil -> the literal; a string -> localisation lookup first (OPEN here);
    // other types -> 0x00e23f40's text conversion (OPEN here).
    auto insertValue = [&](size_t idx, const std::u16string& modifier) {
        if (idx >= count) { ++r.insertionsVanished; return; }
        const InsertValue v = valueAt(idx);
        std::string mod;
        for (char16_t c : modifier) mod.push_back(static_cast<char>(lowerAscii(c)));
        if (v.kind == InsertValue::Kind::Nil) {
            out(u"{No insertion value specified!}");
            return;
        }
        if (mod == "not_localized") {
            if (v.kind == InsertValue::Kind::String) { out(v.text); return; }
            r.open.push_back("insert_values: non-string value with :not_localized needs 0x00e23f40's text conversion "
                             "(format not in clean spec text)");
            return;
        }
        if (mod == "text_tag_crc") {
            const bool integral = v.kind == InsertValue::Kind::Number && std::trunc(v.number) == v.number;
            if (!integral) { out(u"{Invalid insertion value for a hash!}"); return; }
            r.open.push_back("insert_values: {N:text_tag_crc} needs the game's localisation table (not in this host)");
            return;
        }
        // CHOSEN: an unknown modifier keyword is treated as no modifier (the
        // spec lists only the two keywords and does not say what others do).
        if (v.kind == InsertValue::Kind::String) {
            r.open.push_back("insert_values: a string value is first tried as a localisation key (game lookup, not in "
                             "this host) before being used as plain text");
            return;
        }
        r.open.push_back("insert_values: number/boolean values are converted to text by 0x00e23f40 (format not in "
                         "clean spec text)");
    };

    const size_t n = src.size();
    for (size_t i = 0; i < n; ++i) {
        const char16_t c = src[i];
        if (c == u'%') {
            if (i + 1 >= n) break; // CHOSEN: a trailing lone '%' produces nothing (no "other character" follows)
            const char16_t d = src[i + 1];
            ++i;
            if (d == u'%') { out(u"%"); continue; }
            if (d == u's' || d == u'S') {
                // "%s - the next value in order, as text (a nil value inserts nothing)"
                const size_t idx = nextValue++;
                if (idx >= count) { ++r.insertionsVanished; continue; }
                const InsertValue v = valueAt(idx);
                if (v.kind == InsertValue::Kind::Nil) continue;
                r.open.push_back("insert_values: %s with a non-nil value needs the game's value-to-text conversion");
                continue;
            }
            out(std::u16string(1, d)); // `%` + other -> that character
            continue;
        }
        if (c == u'{') {
            if (i + 1 < n && src[i + 1] == u'{') { out(u"{"); ++i; continue; }
            const size_t close = src.find(u'}', i + 1);
            if (close == std::u16string::npos) {
                // CHOSEN: an unterminated `{` is copied literally (not specified).
                out(u"{");
                continue;
            }
            std::u16string body = src.substr(i + 1, close - i - 1);
            i = close;
            if (body.empty()) { insertValue(nextValue++, std::u16string()); continue; } // `{}`
            size_t j = 0;
            while (j < body.size() && (body[j] == u' ' || body[j] == u'\t')) ++j;
            if (j < body.size() && body[j] >= u'0' && body[j] <= u'9') {
                size_t idx = 0;
                while (j < body.size() && body[j] >= u'0' && body[j] <= u'9') {
                    idx = std::min<size_t>(idx * 10 + static_cast<size_t>(body[j] - u'0'), 1000000);
                    ++j;
                }
                while (j < body.size() && (body[j] == u' ' || body[j] == u'\t')) ++j;
                std::u16string modifier;
                if (j < body.size() && body[j] == u':') {
                    modifier = body.substr(j + 1);
                    // CHOSEN: spaces/tabs around the keyword are ignored (the
                    // spec allows them inside the braces).
                    while (!modifier.empty() && (modifier.back() == u' ' || modifier.back() == u'\t')) modifier.pop_back();
                    size_t k = 0;
                    while (k < modifier.size() && (modifier[k] == u' ' || modifier[k] == u'\t')) ++k;
                    modifier = modifier.substr(k);
                }
                insertValue(idx, modifier);
                continue;
            }
            if (j >= body.size()) { insertValue(nextValue++, std::u16string()); continue; } // `{ }` = `{}` (CHOSEN)
            // `{NAME}` - a game-side named symbol (table 0x02a5a014 slot +0x0c).
            r.open.push_back("{" + narrowLow(body) + "}: a game-side named symbol (slot +0x0c -> 0x008437a0), not "
                             "available in this host");
            continue;
        }
        out(std::u16string(1, c));
    }
    return r;
}

// ---- Sec14.4 named colours --------------------------------------------------

const std::vector<NamedColour>& namedColours() {
    // Sec14.4's table read in COLUMN order. The registration order of the
    // compile-time list 0x01301860 is not stated as such; column order is
    // CHOSEN because it is the only reading of the three-column table that
    // agrees with the emulated `gre` -> `green` vector (row order would put
    // `grey` first). Values: the table's nearest-byte hex (the stored floats
    // themselves are not given, except red's 0.8/0.134/0 via the emulated
    // colour record in Sec14.5 - which packs to the same bytes).
    static const std::vector<NamedColour> table = {
        {"kill", 0xCC, 0x22, 0x00},      {"use", 0xF6, 0xBB, 0x4F},       {"revive", 0x68, 0x64, 0xEE},
        {"defend", 0x65, 0xB2, 0x4C},    {"location", 0x19, 0x9D, 0xE5},  {"coop", 0x94, 0x00, 0xC5},
        {"em", 0x68, 0x07, 0x88},        {"saints", 0x94, 0x00, 0xC5},    {"highlight", 0xD9, 0xBF, 0x1F},
        {"syndicate", 0xD0, 0x4A, 0x92}, {"red", 0xCC, 0x22, 0x00},       {"yellow", 0xF6, 0xBB, 0x4F},
        {"purple", 0x94, 0x00, 0xC5},    {"green", 0x65, 0xB2, 0x4C},     {"blue", 0x19, 0x9D, 0xE5},
        {"teal", 0x19, 0x9D, 0xE5},      {"cyan", 0x19, 0x9D, 0xE5},      {"full_bright", 0xFF, 0xFF, 0xFF},
        {"pink", 0xDB, 0x00, 0xDB},      {"orange", 0xF5, 0x96, 0x1F},    {"grey", 0x82, 0x82, 0x82},
        {"gold", 0xEB, 0xD9, 0x6E},      {"silver", 0xC4, 0xC4, 0xC4},    {"bronze", 0xA8, 0x61, 0x2E},
        {"mp_friend", 0x33, 0xA8, 0xFF}, {"mp_foe", 0xCF, 0x1F, 0x1F},
    };
    return table;
}

const NamedColour* findNamedColour(const std::string& arg) {
    // "compares only as many characters as the tag gives, so a prefix matches
    // the first registered name it starts" (Sec14.4); case-insensitive.
    for (const NamedColour& nc : namedColours()) {
        const size_t len = std::strlen(nc.name);
        if (arg.size() > len) continue;
        bool eq = true;
        for (size_t i = 0; i < arg.size() && eq; ++i)
            eq = lowerAscii(static_cast<unsigned char>(arg[i])) == static_cast<uint32_t>(nc.name[i]);
        if (eq) return &nc;
    }
    return nullptr;
}

// ---- Sec14.3 layout -------------------------------------------------------

namespace {

class Layouter {
public:
    Layouter(const LayoutParams& p, const LayoutMetrics& m, const InlineImageLookup& img)
        : p_(p), m_(m), img_(img) {
        stack_.resize(kStyleEntries);
        stack_[0] = Style{p.textScaleX, p.textScaleY, -1, -1, -1, -1}; // entry 0 (Sec14.3)
    }

    void process(const std::u16string& s, int depth) {
        size_t i = 0;
        const size_t n = s.size();
        while (!stopped_) {
            const uint32_t c = i < n ? s[i] : 0;
            if (c == 0) { endAtNul(); return; } // NUL: end of text; ends the current line
            if (c == '[' && isTagStart(s, i)) { i = processTag(s, i, depth); continue; }
            i = runBuilder(s, i);
        }
    }

    LayoutResult finish() {
        // Final placement (Sec14.3 steps 1-5).
        for (const StreamRecord& rec : r_.records) {
            if (rec.op == StreamRecord::Op::Colour || rec.line < 0 || rec.line >= static_cast<int>(r_.lines.size())) continue;
            LayoutLine& ln = r_.lines[static_cast<size_t>(rec.line)];
            ln.height = std::max(ln.height, rec.height); // step 1
        }
        double w = 0, h = 0;
        for (const LayoutLine& ln : r_.lines) { w = std::max(w, ln.width); h += ln.height; } // step 2
        double y = 0;
        for (LayoutLine& ln : r_.lines) {
            switch (p_.align) { // step 3 - within the widest line, not wrap_width
                case HorzAlign::Center: ln.x = (w - ln.width) / 2; break;
                case HorzAlign::Right: ln.x = w - ln.width; break;
                default: ln.x = 0; break;
            }
            ln.y = y; // step 4
            y += ln.height;
        }
        for (StreamRecord& rec : r_.records) { // step 5
            if (rec.op == StreamRecord::Op::Colour || rec.line < 0 || rec.line >= static_cast<int>(r_.lines.size())) continue;
            const LayoutLine& ln = r_.lines[static_cast<size_t>(rec.line)];
            rec.x = toF32(rec.x + ln.x);
            rec.y = toF32(rec.y + ln.y + (ln.height - rec.height) / 2);
        }
        r_.width = w;
        r_.height = h;
        // Stream end: a zero dword if at least 4 bytes remain, counted.
        if (kStreamCapacity - r_.streamBytes >= 4) r_.streamBytes += 4;
        return r_;
    }

private:
    struct Style {
        double sx = 1, sy = 1, r = -1, g = -1, b = -1, a = -1;
    };

    // At a `[` that is not followed (after spaces) by another `[`, a tag is
    // processed (Sec14.3 "Segmenting"). CHOSEN: a `[` with no `]` anywhere
    // after it is not a tag (it is laid out as an ordinary character) - the
    // spec does not say what an unterminated tag does.
    static bool isTagStart(const std::u16string& s, size_t i) {
        size_t j = i + 1;
        while (j < s.size() && s[j] == u' ') ++j;
        if (j < s.size() && s[j] == u'[') return false;
        return s.find(u']', i + 1) != std::u16string::npos;
    }

    double scaledLineHeight(const Style& st) const { return (m_.lineHeight + p_.leading) * st.sy; }

    bool reserve(size_t bytes) {
        if (r_.streamBytes + bytes > kStreamCapacity) {
            // CHOSEN: running out of stream space truncates cleanly (Sec14.3
            // "a replacement should simply truncate"; the shipped code garbles).
            r_.truncated = true;
            stopped_ = true;
            return false;
        }
        r_.streamBytes += bytes;
        return true;
    }

    void emitRun(StreamRecord rec) {
        if (rec.text.empty()) return;
        const size_t units = rec.text.size() + (rec.text.size() % 2); // odd-length runs padded with one 0 unit
        if (!reserve(4 + 28 + 2 * units)) return;
        rec.x = toF32(rec.x);
        lineHasContent_ = true;
        r_.records.push_back(std::move(rec));
    }

    void emitColour(const Style& st) {
        if (!reserve(4 + 16)) return;
        StreamRecord rec;
        rec.op = StreamRecord::Op::Colour;
        rec.r = st.r;
        rec.g = st.g;
        rec.b = st.b;
        rec.a = st.a;
        r_.records.push_back(rec);
    }

    // Closes the current line: its width is the pen minus the trailing
    // `kerning` byte (x the current style's sx - the reading that matches the
    // emulated `AB`, kerning +3, text_scale (2,1) = 74 vector).
    void closeLine() {
        LayoutLine ln;
        ln.width = pen_ - p_.kerning * stack_[idx_].sx;
        r_.lines.push_back(ln);
        pen_ = 0;
        lineHasContent_ = false;
    }

    // An explicit line break (LF, `\n`, `\N`). "An empty line (two line
    // breaks in a row) is given a single space so it has height."
    void lineBreak() {
        if (!lineHasContent_) {
            const Style& st = stack_[idx_];
            StreamRecord rec = newRun(st);
            rec.text = u" ";
            // CHOSEN: the inserted space is measured with no following
            // character (no kerning); the spec does not give its next char.
            pen_ += (m_.advance(' ', 0) + p_.kerning) * st.sx;
            emitRun(rec);
        }
        closeLine();
    }

    // NUL: ends the current line - which adds no line when nothing is on it
    // ("a trailing line break at the very end adds no line"; an empty
    // string emits nothing but the terminator).
    void endAtNul() {
        if (lineHasContent_) closeLine();
        else pen_ = 0;
    }

    // A wrap break. CHOSEN: a wrap that would leave an empty line (only
    // possible when a run's first character is a space that later becomes
    // the break point, with nothing earlier on the line) closes no line.
    void wrapBreak() {
        if (lineHasContent_) closeLine();
        else pen_ = 0;
    }

    StreamRecord newRun(const Style& st) const {
        StreamRecord rec;
        rec.op = StreamRecord::Op::Run;
        rec.x = pen_;
        rec.y = 0;
        rec.sx = st.sx;
        rec.sy = st.sy;
        rec.font = p_.font;
        rec.spacing = p_.kerning;
        rec.height = scaledLineHeight(st); // registered height = (line height + leading) x sy
        rec.line = static_cast<int>(r_.lines.size());
        return rec;
    }

    // The run builder 0x00e36070 (Sec14.3 "Per character" + "Wrapping").
    // Returns the source index to continue from.
    //
    // CHOSEN: break opportunities are tracked within the current run (one
    // run record) - "the most recent break opportunity on the line" can only
    // differ from that when markup splits a line into several runs, which no
    // emulated vector covers; rewriting an already-emitted record of an
    // earlier run is not something the spec describes.
    size_t runBuilder(const std::u16string& s, size_t i) {
        const size_t n = s.size();
        const Style st = stack_[idx_];
        StreamRecord rec = newRun(st);
        std::vector<size_t> src;   // source index of each placed character
        std::vector<double> penAt; // pen before each placed character
        struct Opportunity {
            size_t k;       // break before placed character k
            bool dropSpace; // the character at k is the break-point space (dropped)
        };
        std::optional<Opportunity> opp;
        const bool anywhere = m_.breakAnywhere;
        while (true) {
            const uint32_t c = i < n ? s[i] : 0;
            if (c == 0) { emitRun(rec); return i; }
            if (c == '\n') { emitRun(rec); lineBreak(); return i + 1; }
            if (c == '\\' && i + 1 < n && (s[i + 1] == u'n' || s[i + 1] == u'N')) {
                emitRun(rec);
                lineBreak();
                return i + 2;
            }
            const size_t start = i;
            uint32_t ch;
            if (c == '[') {
                if (i + 1 < n && s[i + 1] == u'[') { ch = '['; i += 2; } // `[[` -> one literal `[`
                else if (isTagStart(s, i)) { emitRun(rec); return i; }
                else { ch = '['; i += 1; }
            } else {
                ch = c;
                i += 1;
            }
            const uint32_t next = i < n ? s[i] : 0;
            // advance = (font advance with kerning against the next character
            // + the kerning byte) x style sx
            const double adv = (m_.advance(ch, next) + p_.kerning) * st.sx;
            if (ch == ' ' && !anywhere) opp = Opportunity{rec.text.size(), true}; // opportunity AT the space
            // Wrap: the first character of each run is never tested; strictly
            // past only (an exact fit stays).
            if (p_.wrapWidth > 0 && !rec.text.empty() && pen_ + adv > p_.wrapWidth) {
                size_t keep, resume;
                double width;
                if (anywhere || !opp) {
                    // "Break anywhere" fonts: every character is an
                    // opportunity (spaces kept) - the most recent is the
                    // current one. No opportunity: break before the current
                    // character, splitting the word.
                    keep = rec.text.size();
                    resume = start;
                    width = pen_;
                } else {
                    keep = opp->k;
                    width = keep < penAt.size() ? penAt[keep] : pen_;
                    if (opp->dropSpace) resume = keep < src.size() ? src[keep] + 1 : i; // the space is dropped
                    else resume = keep < src.size() ? src[keep] : start;
                }
                rec.text.resize(keep);
                pen_ = width;
                emitRun(rec);
                wrapBreak();
                return resume; // the new line begins exactly where the old one stopped
            }
            src.push_back(start);
            penAt.push_back(pen_);
            rec.text.push_back(static_cast<char16_t>(ch));
            pen_ += adv;
            if (ch == '-' && !anywhere) opp = Opportunity{rec.text.size(), false}; // AFTER the hyphen
        }
    }

    // A tag (Sec14.4). Returns the index after its `]`.
    size_t processTag(const std::u16string& s, size_t i, int depth) {
        const size_t close = s.find(u']', i + 1);
        size_t j = i + 1;
        while (j < close && s[j] == u' ') ++j;
        const std::u16string body = s.substr(j, close - j);
        if (ciStartsWith(body, "format")) {
            // push: copy the current entry into the next slot and make it
            // current. CHOSEN depth cap: the real code corrupts its own index
            // on a 17th entry (Sec14.4 "nesting overflow"); "a replacement
            // should cap the depth" - extra pushes are counted and their
            // matching pops consume the count instead of popping.
            if (idx_ + 1 >= kStyleEntries) {
                ++overflowDepth_;
                ++r_.formatDepthCapped;
            } else {
                stack_[idx_ + 1] = stack_[idx_];
                ++idx_;
            }
        } else if (ciStartsWith(body, "/format")) {
            // pop (only if not already at entry 0), then ALWAYS a colour record
            if (overflowDepth_ > 0) --overflowDepth_;
            else if (idx_ > 0) --idx_;
            emitColour(stack_[idx_]);
        } else if (ciStartsWith(body, "color:")) {
            applyColour(body.substr(6));
        } else if (ciStartsWith(body, "scale:")) {
            applyScale(body.substr(6));
        } else if (ciStartsWith(body, "image:")) {
            placeImage(body.substr(6));
        } else {
            // Anything else in brackets is shown: `[[unrecognized: '...']`
            // laid out as text, and a line break follows it (its own end of
            // string ends the line).
            ++r_.unrecognizedTags;
            const std::u16string contents = s.substr(i + 1, close - i - 1);
            const std::u16string formatted = u"[[unrecognized: '" + contents + u"']";
            // CHOSEN host guard: the formatted text is laid out through the
            // same segment loop (so its `[[` becomes `[`); an unrecognized tag
            // nested inside THAT text (depth >= 1) is dropped rather than
            // expanded again.
            if (depth < 1) process(formatted, depth + 1);
        }
        return close + 1;
    }

    void applyColour(const std::u16string& arg) {
        Style& st = stack_[idx_];
        if (!arg.empty() && arg[0] == u'#') {
            size_t digits = 0;
            while (1 + digits < arg.size() && isHex(arg[1 + digits])) ++digits;
            const size_t pairs = digits / 2;
            if (pairs < 3) return; // fewer than three complete pairs: ignored entirely, no record
            auto pair = [&](size_t k) {
                const int v = hexVal(arg[1 + 2 * k]) * 16 + hexVal(arg[2 + 2 * k]);
                return toF32(v / 255.0); // "each pair / 255" (stored as a 4-byte float)
            };
            st.r = pair(0);
            st.g = pair(1);
            st.b = pair(2);
            if (pairs >= 4) st.a = pair(3); // #RRGGBBAA: explicit alpha (escapes parent fades)
            // #RRGGBB: alpha copied from the current entry (left as is)
        } else {
            const NamedColour* nc = findNamedColour(narrowLow(arg));
            if (!nc) return; // unknown name: ignored entirely
            st.r = toF32(nc->r / 255.0);
            st.g = toF32(nc->g / 255.0);
            st.b = toF32(nc->b / 255.0);
        }
        emitColour(st);
    }

    void applyScale(const std::u16string& arg) {
        // `N` is decimal digits with an optional `.` fraction; (N, N),
        // absolute (replaces text_scale). CHOSEN: an argument with no digits
        // leaves the scale unchanged (the spec gives only the valid syntax).
        size_t k = 0;
        double v = 0;
        bool any = false;
        while (k < arg.size() && arg[k] >= u'0' && arg[k] <= u'9') { v = v * 10 + (arg[k] - u'0'); ++k; any = true; }
        if (k < arg.size() && arg[k] == u'.') {
            ++k;
            double scale = 0.1;
            while (k < arg.size() && arg[k] >= u'0' && arg[k] <= u'9') {
                v += (arg[k] - u'0') * scale;
                scale *= 0.1;
                ++k;
                any = true;
            }
        }
        if (!any) return;
        stack_[idx_].sx = toF32(v);
        stack_[idx_].sy = toF32(v);
    }

    void placeImage(const std::u16string& arg) {
        // NAME: up to 63 characters, each UTF-16 unit truncated to its low byte.
        std::string name = narrowLow(arg.substr(0, std::min<size_t>(arg.size(), 63)));
        const Style& st = stack_[idx_];
        InlineImageInfo info = img_ ? img_(name) : InlineImageInfo{};
        const double adv = info.found ? info.width * st.sx : 0;
        // "If it would overrun the wrap width and the line is not empty, the
        // line breaks first and the image starts the next line."
        if (p_.wrapWidth > 0 && lineHasContent_ && pen_ + adv > p_.wrapWidth) wrapBreak();
        if (!reserve(4 + 20)) return;
        StreamRecord rec;
        rec.op = StreamRecord::Op::Image;
        rec.x = toF32(pen_);
        rec.y = 0;
        rec.sx = st.sx;
        rec.sy = st.sy;
        rec.imageName = name;
        rec.image = info;
        rec.height = info.found ? info.height * st.sy : 0;
        rec.line = static_cast<int>(r_.lines.size());
        r_.records.push_back(rec);
        lineHasContent_ = true;
        pen_ += adv;
    }

    const LayoutParams& p_;
    const LayoutMetrics& m_;
    const InlineImageLookup& img_;
    std::vector<Style> stack_;
    size_t idx_ = 0;
    size_t overflowDepth_ = 0;
    double pen_ = 0;
    bool lineHasContent_ = false;
    bool stopped_ = false;
    LayoutResult r_;
};

} // namespace

LayoutResult layoutText(const std::u16string& text, const LayoutParams& params, const LayoutMetrics& metrics,
                        const InlineImageLookup& imageLookup) {
    Layouter l(params, metrics, imageLookup);
    l.process(text, 0);
    return l.finish();
}

} // namespace sr3font

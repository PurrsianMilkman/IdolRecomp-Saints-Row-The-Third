// Implementation of sr3xtbl (see include/sr3xtbl/xtbl.h for the design, the
// tolerance policy and the spec sections everything is taken from).

#include "sr3xtbl/xtbl.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sr3xtbl {

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
namespace {

inline bool isWs(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline uint8_t lowerAscii(uint8_t c) { return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c - 'A' + 'a') : c; }

bool allWs(std::string_view s) {
    for (char c : s) if (!isWs(static_cast<uint8_t>(c))) return false;
    return true;
}

std::string_view trimWs(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && isWs(static_cast<uint8_t>(s[b]))) ++b;
    while (e > b && isWs(static_cast<uint8_t>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Illegal-in-XML control byte: below 0x20 and not TAB/CR/LF.
inline bool isControl(uint8_t c) { return c < 0x20 && c != '\t' && c != '\r' && c != '\n'; }

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------
const char* WarningKindName(WarningKind k) {
    switch (k) {
        case WarningKind::MismatchedCloseTag: return "MismatchedCloseTag";
        case WarningKind::UnclosedElement: return "UnclosedElement";
        case WarningKind::StrayCloseTag: return "StrayCloseTag";
        case WarningKind::CloseTagCaseDiffers: return "CloseTagCaseDiffers";
        case WarningKind::UnclosedAtEof: return "UnclosedAtEof";
        case WarningKind::ControlCharacterInText: return "ControlCharacterInText";
        case WarningKind::ControlCharacterInName: return "ControlCharacterInName";
        case WarningKind::NameContainsWhitespace: return "NameContainsWhitespace";
        case WarningKind::TextOutsideRoot: return "TextOutsideRoot";
        case WarningKind::ExtraTopLevelElement: return "ExtraTopLevelElement";
        case WarningKind::StrayLessThan: return "StrayLessThan";
        case WarningKind::UnknownEntity: return "UnknownEntity";
        case WarningKind::BadNumericEntity: return "BadNumericEntity";
    }
    return "?";
}

bool NameEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

const std::string* Node::attribute(std::string_view name) const {
    for (const Attribute& a : attrs_) {
        if (NameEquals(a.name, name)) return &a.value;
    }
    return nullptr;
}

const Node* Document::table() const { return FindChild(root_, "Table"); }

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------
class Parser {
public:
    Parser(Document& doc, const uint8_t* data, size_t size, const ParseOptions& opt)
        : doc_(doc), d_(data), n_(size), opt_(opt) {}

    void run() {
        size_t pos = 0;
        if (n_ >= 2 && ((d_[0] == 0xFF && d_[1] == 0xFE) || (d_[0] == 0xFE && d_[1] == 0xFF))) {
            throw FormatError("xtbl: input starts with a UTF-16 byte-order mark; not an XML-like 8-bit text table", 0);
        }
        if (n_ >= 3 && d_[0] == 0xEF && d_[1] == 0xBB && d_[2] == 0xBF) {
            doc_.bom_ = true;
            pos = 3;
        }
        size_t first = pos;
        while (first < n_ && isWs(d_[first])) ++first;
        if (first >= n_) throw FormatError("xtbl: input is empty or whitespace only (no root element)", n_);
        if (d_[first] != '<') throw FormatError("xtbl: input does not begin with '<' (not XML-like)", first);

        pos = first;
        while (pos < n_) {
            if (d_[pos] == '<') pos = markup(pos);
            else pos = textRun(pos);
        }

        // End of input with elements still open: close them, recording each.
        while (!stack_.empty()) {
            Node* nd = stack_.back().node;
            warn(WarningKind::UnclosedAtEof, n_, "end of input while <" + nd->name_ + "> (offset " + std::to_string(nd->offset_) + ") is still open");
            closeTop();
        }
        if (!doc_.root_) throw FormatError("xtbl: no root element found", n_);
    }

private:
    struct Frame {
        Node* node;
        bool hasChildren = false;
        bool mixed = false;           // non-whitespace text together with child elements
        bool textAfterChild = false;  // ... of which some came after a child
    };

    Document& doc_;
    const uint8_t* d_;
    size_t n_;
    ParseOptions opt_;
    std::vector<Frame> stack_;

    void warn(WarningKind k, size_t off, std::string detail) {
        ++doc_.warningCount_;
        if (doc_.warnings_.size() < Document::kMaxStoredWarnings) {
            doc_.warnings_.push_back(Warning{k, off, std::move(detail)});
        }
    }

    // ---- character data ---------------------------------------------------
    // Text runs to the next '<'. Returns the position of that '<' (or n_).
    size_t textRun(size_t pos) {
        size_t end = pos;
        while (end < n_ && d_[end] != '<') ++end;
        appendText(pos, end, /*raw=*/false);
        return end;
    }

    // Adds d_[b,e) to the innermost open element's text (or drops it outside the root).
    // raw == true for CDATA (no entity decoding).
    void appendText(size_t b, size_t e, bool raw) {
        if (b >= e) return;
        std::string_view seg(reinterpret_cast<const char*>(d_) + b, e - b);
        if (stack_.empty()) {
            if (!allWs(seg)) warn(WarningKind::TextOutsideRoot, b, "text outside the root element dropped");
            return;
        }
        Frame& f = stack_.back();
        for (size_t i = b; i < e; ++i) {
            if (isControl(d_[i])) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "control byte 0x%02X in the text of <", d_[i]);
                warn(WarningKind::ControlCharacterInText, i, std::string(buf) + f.node->name_ + "> (kept)");
                break; // one warning per run
            }
        }
        const bool wsOnly = allWs(seg);
        if (f.hasChildren && wsOnly) return; // indentation between child elements
        if (f.hasChildren) { f.mixed = true; f.textAfterChild = true; }
        std::string decoded;
        if (raw || !opt_.decodeEntities) decoded.assign(seg);
        else decodeInto(b, e, decoded);
        f.node->text_ += decoded;
    }

    // Entity-decodes d_[b,e) into `out`.
    void decodeInto(size_t b, size_t e, std::string& out) {
        size_t i = b;
        while (i < e) {
            const uint8_t c = d_[i];
            if (c != '&') {
                size_t j = i;
                while (j < e && d_[j] != '&') ++j;
                out.append(reinterpret_cast<const char*>(d_) + i, j - i);
                i = j;
                continue;
            }
            // find ';' within a short window
            size_t j = i + 1;
            while (j < e && j < i + 12 && d_[j] != ';' && d_[j] != '&' && !isWs(d_[j])) ++j;
            if (j < e && d_[j] == ';' && j > i + 1) {
                std::string_view ent(reinterpret_cast<const char*>(d_) + i + 1, j - i - 1);
                bool done = true;
                if (ent == "amp") out.push_back('&');
                else if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "quot") out.push_back('"');
                else if (ent == "apos") out.push_back('\'');
                else if (ent[0] == '#') {
                    uint32_t cp = 0;
                    bool ok = ent.size() > 1;
                    if (ok && (ent[1] == 'x' || ent[1] == 'X')) {
                        ok = ent.size() > 2;
                        for (size_t k = 2; ok && k < ent.size(); ++k) {
                            const char h = ent[k];
                            int v = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
                            if (v < 0 || cp > 0x10FFFF) { ok = false; break; }
                            cp = cp * 16 + static_cast<uint32_t>(v);
                        }
                    } else {
                        for (size_t k = 1; ok && k < ent.size(); ++k) {
                            if (!isDigit(ent[k]) || cp > 0x10FFFF) { ok = false; break; }
                            cp = cp * 10 + static_cast<uint32_t>(ent[k] - '0');
                        }
                    }
                    if (ok && cp != 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                        appendUtf8(out, cp);
                    } else {
                        warn(WarningKind::BadNumericEntity, i, "&" + std::string(ent) + "; is not a valid character reference (kept as text)");
                        done = false;
                    }
                } else {
                    warn(WarningKind::UnknownEntity, i, "unknown entity &" + std::string(ent) + "; (kept as text)");
                    done = false;
                }
                if (done) { i = j + 1; continue; }
                out.append(reinterpret_cast<const char*>(d_) + i, j + 1 - i);
                i = j + 1;
                continue;
            }
            warn(WarningKind::UnknownEntity, i, "'&' that starts no entity (kept as text)");
            out.push_back('&');
            ++i;
        }
    }

    // ---- markup -----------------------------------------------------------
    // d_[pos] == '<'. Returns the position after the construct.
    size_t markup(size_t pos) {
        if (pos + 1 >= n_) throw FormatError("xtbl: input ends right after '<'", pos);
        const uint8_t c = d_[pos + 1];
        if (c == '!') return bang(pos);
        if (c == '?') {
            size_t j = pos + 2;
            while (j + 1 < n_ && !(d_[j] == '?' && d_[j + 1] == '>')) ++j;
            if (j + 1 >= n_) throw FormatError("xtbl: unterminated processing instruction / declaration", pos);
            return j + 2;
        }
        if (c == '/') return closeTag(pos);
        if (isWs(c) || c == '>' || c == '=' || c == '<') {
            warn(WarningKind::StrayLessThan, pos, "'<' that cannot start a tag kept as text");
            appendLiteral(pos, pos + 1);
            return pos + 1;
        }
        return openTag(pos);
    }

    void appendLiteral(size_t b, size_t e) {
        // Literal text bytes that must not be entity-decoded or whitespace-dropped semantics: reuse raw path.
        appendText(b, e, /*raw=*/true);
    }

    size_t bang(size_t pos) {
        if (pos + 3 < n_ && d_[pos + 2] == '-' && d_[pos + 3] == '-') {
            size_t j = pos + 4;
            while (j + 2 < n_ && !(d_[j] == '-' && d_[j + 1] == '-' && d_[j + 2] == '>')) ++j;
            if (j + 2 >= n_) throw FormatError("xtbl: unterminated comment", pos);
            return j + 3;
        }
        static const char kCdata[] = "<![CDATA[";
        if (pos + 8 < n_ && std::memcmp(d_ + pos, kCdata, 9) == 0) {
            size_t j = pos + 9;
            while (j + 2 < n_ && !(d_[j] == ']' && d_[j + 1] == ']' && d_[j + 2] == '>')) ++j;
            if (j + 2 >= n_) throw FormatError("xtbl: unterminated CDATA section", pos);
            appendText(pos + 9, j, /*raw=*/true);
            return j + 3;
        }
        // <!DOCTYPE ...> and friends: skip to the matching '>' (an internal subset [...] may contain '>').
        size_t j = pos + 2;
        int depth = 0;
        while (j < n_) {
            if (d_[j] == '[') ++depth;
            else if (d_[j] == ']') { if (depth > 0) --depth; }
            else if (d_[j] == '>' && depth == 0) return j + 1;
            ++j;
        }
        throw FormatError("xtbl: unterminated '<!' declaration", pos);
    }

    // Index of the '>' that ends a tag starting after `from`, respecting quoted attribute
    // values (a quote only counts right after '='); n_ if there is none.
    size_t findTagEnd(size_t from) const {
        bool afterEq = false;
        size_t i = from;
        while (i < n_) {
            const uint8_t c = d_[i];
            if (c == '>') return i;
            if ((c == '"' || c == '\'') && afterEq) {
                size_t j = i + 1;
                while (j < n_ && d_[j] != c) ++j;
                if (j < n_) { i = j + 1; afterEq = false; continue; }
            }
            if (c == '=') afterEq = true;
            else if (!isWs(c)) afterEq = false;
            ++i;
        }
        return n_;
    }

    // Parses `rest` (already trimmed, non-empty) as `name="value"` pairs. Returns false, with
    // no side effects, if it is not one; on success fills `out` (values entity-decoded).
    bool parseAttributes(std::string_view rest, size_t restAbs, std::vector<Attribute>& out) {
        struct Span { std::string_view name; size_t valBegin; size_t valEnd; };
        std::vector<Span> spans;
        size_t i = 0;
        const size_t len = rest.size();
        while (true) {
            while (i < len && isWs(static_cast<uint8_t>(rest[i]))) ++i;
            if (i >= len) break;
            const size_t ns = i;
            while (i < len && !isWs(static_cast<uint8_t>(rest[i])) && rest[i] != '=' && rest[i] != '"' && rest[i] != '\'') ++i;
            if (i == ns) return false;
            const std::string_view an = rest.substr(ns, i - ns);
            while (i < len && isWs(static_cast<uint8_t>(rest[i]))) ++i;
            if (i >= len || rest[i] != '=') return false;
            ++i;
            while (i < len && isWs(static_cast<uint8_t>(rest[i]))) ++i;
            if (i >= len || (rest[i] != '"' && rest[i] != '\'')) return false;
            const char q = rest[i++];
            const size_t vs = i;
            while (i < len && rest[i] != q) ++i;
            if (i >= len) return false;
            spans.push_back(Span{an, vs, i});
            ++i; // closing quote
        }
        if (spans.empty()) return false;
        for (const Span& s : spans) {
            std::string val;
            if (opt_.decodeEntities) decodeInto(restAbs + s.valBegin, restAbs + s.valEnd, val);
            else val.assign(rest.substr(s.valBegin, s.valEnd - s.valBegin));
            out.push_back(Attribute{std::string(s.name), std::move(val)});
        }
        return true;
    }

    size_t openTag(size_t pos) {
        const size_t gt = findTagEnd(pos + 1);
        if (gt >= n_) throw FormatError("xtbl: input ends in the middle of a tag", pos);
        std::string_view body(reinterpret_cast<const char*>(d_) + pos + 1, gt - (pos + 1));
        bool selfClose = false;
        if (!body.empty() && body.back() == '/') {
            selfClose = true;
            body.remove_suffix(1);
        }
        body = trimWs(body);
        std::string name;
        std::vector<Attribute> attrs;
        size_t k = 0;
        while (k < body.size() && !isWs(static_cast<uint8_t>(body[k]))) ++k;
        std::string_view token = body.substr(0, k);
        std::string_view rest = trimWs(body.substr(k));
        if (rest.empty()) {
            name.assign(token);
        } else {
            // offset of `rest` inside the input, for entity-warning offsets
            const size_t restAbs = static_cast<size_t>(rest.data() - reinterpret_cast<const char*>(d_));
            if (parseAttributes(rest, restAbs, attrs)) {
                name.assign(token);
            } else {
                // Not an attribute list: the whole tag text is the element name (may contain spaces).
                name.assign(body);
                warn(WarningKind::NameContainsWhitespace, pos, "element name \"" + name + "\" contains whitespace (whole tag text kept as the name)");
            }
        }
        checkNameControl(name, pos);

        Node* parent = stack_.empty() ? nullptr : stack_.back().node;
        if (!parent && doc_.root_) {
            warn(WarningKind::ExtraTopLevelElement, pos, "second top-level element <" + name + ">");
        }
        doc_.nodes_.emplace_back();
        Node* nd = &doc_.nodes_.back();
        nd->name_ = std::move(name);
        nd->attrs_ = std::move(attrs);
        nd->offset_ = pos;
        if (parent) {
            Frame& pf = stack_.back();
            if (!pf.hasChildren) {
                pf.hasChildren = true;
                if (allWs(parent->text_)) parent->text_.clear();
                else pf.mixed = true;
            }
            nd->parent_ = parent;
            nd->index_ = parent->children_.size();
            parent->children_.push_back(nd);
        } else {
            nd->index_ = doc_.topLevel_.size();
            doc_.topLevel_.push_back(nd);
            if (!doc_.root_) doc_.root_ = nd;
        }
        stack_.push_back(Frame{nd});
        if (selfClose) closeTop();
        return gt + 1;
    }

    void checkNameControl(const std::string& name, size_t pos) {
        for (char ch : name) {
            if (isControl(static_cast<uint8_t>(ch))) {
                warn(WarningKind::ControlCharacterInName, pos, "control byte in element name \"" + name + "\" (kept)");
                return;
            }
        }
    }

    void closeTop() {
        Frame f = stack_.back();
        stack_.pop_back();
        Node* nd = f.node;
        nd->hasText_ = !nd->text_.empty();
        if (f.mixed) ++doc_.mixed_;
        if (f.textAfterChild) ++doc_.textAfterChild_;
    }

    size_t closeTag(size_t pos) {
        const size_t gt = findTagEnd(pos + 2);
        if (gt >= n_) throw FormatError("xtbl: input ends in the middle of a close tag", pos);
        std::string_view body(reinterpret_cast<const char*>(d_) + pos + 2, gt - (pos + 2));
        body = trimWs(body);
        // A close tag never has attributes; if it looks like `name attr=".."`, keep only the name token.
        std::string name(body);
        checkNameControl(name, pos);

        if (stack_.empty()) {
            warn(WarningKind::StrayCloseTag, pos, "</" + name + "> with no open element ignored");
            return gt + 1;
        }
        // nearest open ancestor (innermost first) that matches exactly, else ignoring case
        int match = -1;
        bool exact = false;
        for (int i = static_cast<int>(stack_.size()) - 1; i >= 0; --i) {
            if (stack_[static_cast<size_t>(i)].node->name_ == name) { match = i; exact = true; break; }
        }
        if (match < 0) {
            for (int i = static_cast<int>(stack_.size()) - 1; i >= 0; --i) {
                if (NameEquals(stack_[static_cast<size_t>(i)].node->name_, name)) { match = i; break; }
            }
        }
        if (match < 0) {
            Node* nd = stack_.back().node;
            warn(WarningKind::MismatchedCloseTag, pos, "</" + name + "> matches no open element; it closed the innermost open element <" + nd->name_ + ">");
            closeTop();
            return gt + 1;
        }
        while (static_cast<int>(stack_.size()) - 1 > match) {
            Node* nd = stack_.back().node;
            warn(WarningKind::UnclosedElement, pos, "<" + nd->name_ + "> (offset " + std::to_string(nd->offset_) + ") closed implicitly by </" + name + ">");
            closeTop();
        }
        if (!exact) {
            warn(WarningKind::CloseTagCaseDiffers, pos, "</" + name + "> closed <" + stack_.back().node->name_ + "> (names differ in case only)");
        }
        closeTop();
        return gt + 1;
    }
};

Document ParseDocument(const uint8_t* data, size_t size, const ParseOptions& options) {
    Document doc;
    Parser p(doc, data, size, options);
    p.run();
    return doc;
}

Document ParseDocument(std::string_view bytes, const ParseOptions& options) {
    return ParseDocument(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), options);
}

// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------
const Node* FindChild(const Node* node, std::string_view name) {
    if (!node) return nullptr;
    for (const Node* c : node->children()) {
        if (NameEquals(c->name(), name)) return c;
    }
    return nullptr;
}

const Node* NextSibling(const Node* parent, const Node* cur, std::string_view name) {
    if (!cur) return nullptr;
    const Node* p = cur->parent();
    if (!p) return nullptr; // a top-level element has no siblings in this model
    if (parent && parent != p) return nullptr;
    const std::vector<const Node*>& kids = p->children();
    for (size_t i = cur->indexInParent() + 1; i < kids.size(); ++i) {
        if (NameEquals(kids[i]->name(), name)) return kids[i];
    }
    return nullptr;
}

size_t CountChildren(const Node* node, std::string_view name) {
    size_t n = 0;
    if (!node) return 0;
    for (const Node* c : node->children()) n += NameEquals(c->name(), name) ? 1 : 0;
    return n;
}

const Node* NthChild(const Node* node, std::string_view name, size_t n) {
    if (!node) return nullptr;
    for (const Node* c : node->children()) {
        if (NameEquals(c->name(), name)) {
            if (n == 0) return c;
            --n;
        }
    }
    return nullptr;
}

std::vector<const Node*> Children(const Node* node, std::string_view name) {
    std::vector<const Node*> out;
    if (!node) return out;
    for (const Node* c : node->children()) {
        if (NameEquals(c->name(), name)) out.push_back(c);
    }
    return out;
}

const std::string* ChildText(const Node* node, std::string_view name) {
    if (!node) return nullptr;
    if (name.empty()) return node->text();
    const Node* c = FindChild(node, name);
    return c ? c->text() : nullptr;
}

std::optional<std::string> CopyText(const Node* node, std::string_view name, size_t bufferSize) {
    const std::string* t = ChildText(node, name);
    if (!t || bufferSize == 0) return std::nullopt;
    return t->substr(0, bufferSize - 1);
}

// ---------------------------------------------------------------------------
// text -> value
// ---------------------------------------------------------------------------
uint32_t ParseEngineInteger(std::string_view s, size_t* consumed) {
    size_t i = 0;
    uint32_t v = 0;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        i = 2;
        while (i < s.size()) {
            const char h = s[i];
            int d = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : (h >= 'A' && h <= 'F') ? h - 'A' + 10 : -1;
            if (d < 0) break;
            v = v * 16u + static_cast<uint32_t>(d);
            ++i;
        }
    } else {
        while (i < s.size() && isDigit(s[i])) {
            v = v * 10u + static_cast<uint32_t>(s[i] - '0');
            ++i;
        }
    }
    if (consumed) *consumed = i;
    return v;
}

int32_t ParseInt32(std::string_view s) {
    bool neg = false;
    if (!s.empty() && s[0] == '-') { neg = true; s.remove_prefix(1); }
    const uint32_t v = ParseEngineInteger(s);
    return static_cast<int32_t>(neg ? (0u - v) : v);
}
uint32_t ParseUInt32(std::string_view s) { return ParseEngineInteger(s); }
uint16_t ParseUInt16(std::string_view s) { return static_cast<uint16_t>(ParseEngineInteger(s)); }
int16_t ParseInt16(std::string_view s) { return static_cast<int16_t>(ParseInt32(s)); }
uint8_t ParseUInt8(std::string_view s) { return static_cast<uint8_t>(ParseEngineInteger(s)); }
int8_t ParseInt8(std::string_view s) { return static_cast<int8_t>(ParseInt32(s)); }

bool ParseBool(std::string_view s) { return NameEquals(s, "true") || NameEquals(s, "yes"); }

float ParseFloat(std::string_view s) {
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) return 0.0f;
    size_t used = 0;
    const uint32_t ip = ParseEngineInteger(s.substr(i), &used);
    i += used;
    float value = static_cast<float>(ip);
    if (i < s.size() && s[i] == '.') {
        ++i;
        float w = 0.1f; // successive x0.1 weights, single precision
        while (i < s.size() && isDigit(s[i])) {
            const float term = static_cast<float>(s[i] - '0') * w;
            value = value + term;
            w = w * 0.1f;
            ++i;
        }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < s.size() && s[i] == '-') { eneg = true; ++i; }
        int n = 0;
        while (i < s.size() && isDigit(s[i])) {
            if (n < 1000) n = n * 10 + (s[i] - '0');
            ++i;
        }
        const double scale = std::pow(10.0, eneg ? -static_cast<double>(n) : static_cast<double>(n));
        value = static_cast<float>(static_cast<double>(value) * scale);
    }
    return neg ? -value : value;
}

// ---------------------------------------------------------------------------
// vec3, flags, enums
// ---------------------------------------------------------------------------
Vec3Result ReadVec3(const Node* vecNode) {
    Vec3Result r;
    r.present = vecNode != nullptr;
    r.x = ReadFloatAlways(vecNode, "X");
    r.y = ReadFloatAlways(vecNode, "Y");
    r.z = ReadFloatAlways(vecNode, "Z");
    return r;
}

Vec3Result ReadVec3Child(const Node* node, std::string_view name) {
    const Node* c = FindChild(node, name);
    if (!c) return Vec3Result{};
    return ReadVec3(c);
}

uint32_t FlagMask(const Node* node, const std::string_view* names, size_t count) {
    uint32_t mask = 0;
    if (!node) return 0;
    const size_t limit = count < 32 ? count : 32;
    for (const Node* c : node->children()) {
        if (!NameEquals(c->name(), "Flag")) continue;
        const std::string* t = c->text();
        if (!t) continue;
        for (size_t i = 0; i < limit; ++i) {
            if (NameEquals(*t, names[i])) { mask |= (1u << i); break; }
        }
    }
    return mask;
}
uint32_t FlagMask(const Node* node, const std::vector<std::string_view>& names) {
    return FlagMask(node, names.data(), names.size());
}
uint32_t FlagMask(const Node* node, std::initializer_list<std::string_view> names) {
    return FlagMask(node, names.begin(), names.size());
}

bool HasFlag(const Node* node, std::string_view flag) {
    if (!node) return false;
    for (const Node* c : node->children()) {
        if (!NameEquals(c->name(), "Flag")) continue;
        const std::string* t = c->text();
        if (t && NameEquals(*t, flag)) return true;
    }
    return false;
}

int EnumIndex(const Node* node, const std::string_view* names, size_t count) {
    if (!node) return -1;
    const std::string* t = node->text();
    if (!t) return -1;
    for (size_t i = 0; i < count; ++i) {
        if (NameEquals(*t, names[i])) return static_cast<int>(i);
    }
    return -1;
}
int EnumIndex(const Node* node, const std::vector<std::string_view>& names) {
    return EnumIndex(node, names.data(), names.size());
}
int EnumIndex(const Node* node, std::initializer_list<std::string_view> names) {
    return EnumIndex(node, names.begin(), names.size());
}

// ---------------------------------------------------------------------------
// name hash
// ---------------------------------------------------------------------------
namespace {
const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}
} // namespace

const uint32_t* NameHashTable() { return crcTable().data(); }

uint32_t NameHash(std::string_view text, uint32_t seed) {
    const auto& t = crcTable();
    uint32_t crc = seed;
    for (char ch : text) {
        const uint8_t b = lowerAscii(static_cast<uint8_t>(ch));
        crc = (crc >> 8) ^ t[(b ^ crc) & 0xFFu];
    }
    return crc;
}

uint32_t NameHashCaseSensitive(std::string_view text, uint32_t seed) {
    const auto& t = crcTable();
    uint32_t crc = seed;
    for (char ch : text) {
        const uint8_t b = static_cast<uint8_t>(ch);
        crc = (crc >> 8) ^ t[(b ^ crc) & 0xFFu];
    }
    return crc;
}

uint32_t NameHashOrZero(const std::string* text, uint32_t seed) {
    return text ? NameHash(*text, seed) : 0u;
}

} // namespace sr3xtbl

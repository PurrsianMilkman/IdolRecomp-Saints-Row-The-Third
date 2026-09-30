#pragma once

// sr3xtbl - foundation for the `.xtbl` / `.cte_xtbl` XML gameplay data tables.
//
// Sources: spec-xtbl-format.md (file conventions, section 7 tolerance quirks),
// spec-tables-traffic-ai.md section 1 (the engine's XML document model 1.2,
// the shared element accessors 1.3, the engine hashes 1.4),
// spec-vehicle-data.md section 7.2 (the same accessors found independently)
// and the shared-grammar sections of spec-tables-weapons-combat.md /
// -progression.md / -environment.md (which add the narrow integer widths, the
// bool spellings, "text is never trimmed by the lookup" and the accessor
// catalogue). Nothing here was derived from the game executable or from
// disassembly; the only real-data evidence is the population survey in
// tools/validation/validate_xtbl_population.cpp.
//
// ---------------------------------------------------------------------------
// 1. THE DOCUMENT MODEL (traffic-ai 1.2)
// ---------------------------------------------------------------------------
// The engine's node has four fields: element name, next sibling, first child,
// text (NULL for an element with no text); all name comparisons are
// case-insensitive. Node mirrors that: name(), children() (in document
// order; NextSibling walks them), text() (nullptr == the engine's NULL).
//
// Beyond the engine model, Node also records the parent, the byte offset of
// its start tag and its attributes. The engine has no attributes: 141 real
// entries carry exactly one, `<TableDescription source="...xml">`, a sibling
// metadata block the row readers never visit, so what the engine does with it
// is UNKNOWN. This parser keeps attributes out of the element name and
// exposes them separately.
//
// TEXT. text() is the concatenation of the element's own character data
// (entity-decoded, CDATA included, comments/PIs dropped), NOT including the
// text of child elements, and is never trimmed ("text is never trimmed by the
// lookup", spec-tables-environment.md 1.3):
//   * no character data at all (`<A></A>`, `<A/>`)                -> absent
//   * the element has child elements and its character data is
//     whitespace-only (the indentation between children)            -> absent
//   * a LEAF element whose text is only whitespace (`<A> </A>`)     -> present,
//     exactly as written (there is no basis in the specs for erasing it)
//   * MIXED CONTENT (child elements AND non-whitespace text): the
//     non-whitespace-only segments are concatenated in order and kept
//     verbatim (trailing whitespace included), whitespace-only segments
//     between children are dropped. This is legal XML, not a defect, so it
//     records no warning; it is counted in Document::mixedContentElements().
//     The shipped tables DO use it (an authoring-tool habit: an element with
//     a value AND sub-elements, e.g. `<front_color>0.98 0.98 0.98 1.0<R>255</R>
//     ...</front_color>` or `<Difficulty_Level>0.0<Damage_Received_Mult>...`), which
//     is exactly what the engine node model (one text pointer + children)
//     represents. How the engine assembles a mixed element's single text
//     pointer when text ALSO follows a child is not specified;
//     Document::textAfterChildElements() counts those (see the population
//     validator for the real-data figure).
// "Absent" (text() == nullptr) and "empty" cannot be distinguished by the
// engine ("text pointer of the child, or 0 if the child is absent or empty"),
// so this model has no empty-but-present string either; the two cases the
// engine CAN tell apart - child missing vs child present with no text - stay
// observable here through FindChild() vs ChildText().
//
// ---------------------------------------------------------------------------
// 2. WHAT THE PARSER TOLERATES (and how each recovery is recorded)
// ---------------------------------------------------------------------------
// It is a hand-written, forgiving reader in the spirit of the engine's own
// (spec-xtbl-format.md 7), NOT a conforming XML parser. Every recovery adds a
// Warning to Document::warnings(); a document with no warnings was accepted
// without any recovery.
//   * Mismatched close tag (`<RampExposure>False</Exposure>`): if the close
//     tag's name equals an open ancestor (exact, else ignoring case) every
//     element above it is implicitly closed (UnclosedElement per element) and
//     it is closed; if it equals NO open element, it closes the INNERMOST open
//     element (MismatchedCloseTag). A close tag with nothing open is ignored
//     (StrayCloseTag).
//   * Control characters in text (0x1F in `<Name>09_PLayer\x1f_Neg</Name>`):
//     the bytes are kept verbatim; one ControlCharacterInText warning per text
//     run. Names likewise (ControlCharacterInName).
//   * Element names containing a space (`<Xbox 360_Identifier>`): if the text
//     of a tag after its first token is a well-formed attribute list
//     (`name="value"` pairs) the first token is the name and the rest are
//     attributes; otherwise the WHOLE tag text (trimmed, a self-closing `/`
//     removed) is the element name, inner spaces included
//     (NameContainsWhitespace). Close tags are read the same way.
//   * End of input with elements still open: they are closed there
//     (UnclosedAtEof per element).
//   * `<` that cannot start a tag (followed by whitespace, `>`, `=`, `<`):
//     kept as literal text (StrayLessThan).
//   * `&` that starts no known entity: kept as literal text (UnknownEntity);
//     numeric references that are not a valid scalar value likewise
//     (BadNumericEntity).
//   * Text before/after the root element: dropped (TextOutsideRoot); a second
//     top-level element: kept in Document::topLevel() (ExtraTopLevelElement).
// It REJECTS (FormatError) input that is not XML-like at all: empty, no `<`
// as the first non-whitespace byte, a UTF-16 byte-order mark, no root element,
// or input that ends in the middle of a tag, comment, CDATA section,
// processing instruction or `<!` declaration.
// A UTF-8 byte-order mark, XML declarations / processing instructions,
// comments, `<!DOCTYPE ...>`, CDATA, CRLF/LF/bare-CR line endings and non-ASCII
// bytes are accepted (text bytes are passed through untouched; the parser does
// no UTF-8 validation or conversion).
//
// ---------------------------------------------------------------------------
// 3. ACCESSORS (traffic-ai 1.3)
// ---------------------------------------------------------------------------
// Free functions taking `const Node*`; a null node is accepted everywhere and
// reads as "absent" (the engine's null-node guard). Names match
// case-insensitively (ASCII). A `name` argument that is empty means
// "the node's own text" (the engine's NULL child name).
//
// ABSENCE IS ALWAYS OBSERVABLE. The engine has two flavours of scalar reader
// and this API keeps both, with different, honest return types:
//   * "write only if present" readers -> GetX(...) -> std::optional<T>: empty
//     iff the child element is missing OR has no text. (`if (auto v = GetX(..))
//     dst = *v;` is exactly the engine idiom.)
//   * "always write" readers -> ReadXAlways(...) -> Always<T>{value, present}.
//     When !present the engine stores an UNSPECIFIED value (it parses an
//     uninitialised scratch buffer; spec-tables-traffic-ai.md 1.3 and
//     spec-vehicle-data.md 7.2's correction of "0 if absent"). `value` is then
//     a deterministic 0 stand-in that a caller must NOT treat as engine
//     behaviour: check `present`. Shipped tables always supply these elements.
//
// Text -> number rules (ParseXxx below) are the engine's:
//   * integer text: decimal digits, or a 0x/0X prefix followed by hex digits;
//     stops at the first non-digit; NO overflow check (wraps modulo 2^32);
//     NO whitespace skipping; the signed readers strip exactly one leading '-'
//     first; the unsigned readers do not, so a leading '-' reads as 0.
//   * u8/u16/s8/s16 parse to 32 bits then truncate.
//   * bool: only `true` and `yes` (whole string, ASCII case-insensitive) are
//     true; every other present text (`false`, `no`, anything) is false.
//   * float: see ParseFloat.
//
// ---------------------------------------------------------------------------
// 4. THE ROW-KEY HASH (traffic-ai 1.4): NameHash()
// ---------------------------------------------------------------------------
// See the declaration; verified against real save data in
// tools/validation/validate_xtbl_population.cpp.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sr3xtbl/errors.h"

namespace sr3xtbl {

// ---------------------------------------------------------------------------
// Warnings
// ---------------------------------------------------------------------------
enum class WarningKind {
    MismatchedCloseTag,      // close tag names no open element: closed the innermost open element
    UnclosedElement,         // element implicitly closed by an ancestor's close tag
    StrayCloseTag,           // close tag with nothing open: ignored
    CloseTagCaseDiffers,     // close tag matched its open tag only ignoring ASCII case
    UnclosedAtEof,           // element still open at the end of the input
    ControlCharacterInText,  // byte < 0x20 other than TAB/CR/LF in text (kept)
    ControlCharacterInName,  // same, inside an element name (kept)
    NameContainsWhitespace,  // element name with inner whitespace (whole tag text kept as the name)
    TextOutsideRoot,         // non-whitespace text before/after the root element (dropped)
    ExtraTopLevelElement,    // a second top-level element (kept in Document::topLevel())
    StrayLessThan,           // '<' that cannot start a tag, kept as text
    UnknownEntity,           // '&' that starts no known entity, kept as text
    BadNumericEntity,        // &#...; that is not a valid Unicode scalar value, kept as text
};

// Stable identifier such as "MismatchedCloseTag".
const char* WarningKindName(WarningKind kind);

struct Warning {
    WarningKind kind;
    size_t offset = 0;   // byte offset in the input where it was detected
    std::string detail;  // human-readable, e.g. "</Exposure> closed <RampExposure>"
};

// ---------------------------------------------------------------------------
// Node / Document
// ---------------------------------------------------------------------------
struct Attribute {
    std::string name;
    std::string value;
};

class Document;

class Node {
public:
    const std::string& name() const { return name_; }
    // Text pointer; nullptr == the engine's NULL text. See the TEXT rules above.
    const std::string* text() const { return hasText_ ? &text_ : nullptr; }
    bool hasText() const { return hasText_; }
    const std::vector<const Node*>& children() const { return children_; }
    const Node* parent() const { return parent_; }
    size_t indexInParent() const { return index_; }
    size_t sourceOffset() const { return offset_; }
    const std::vector<Attribute>& attributes() const { return attrs_; }
    // Value of the first attribute with this name (ASCII case-insensitive), or nullptr.
    const std::string* attribute(std::string_view name) const;

private:
    friend class Parser;
    std::string name_;
    std::string text_;
    bool hasText_ = false;
    std::vector<const Node*> children_;
    std::vector<Attribute> attrs_;
    const Node* parent_ = nullptr;
    size_t index_ = 0;
    size_t offset_ = 0;
};

struct ParseOptions {
    // Decode &amp; &lt; &gt; &quot; &apos; and &#N; / &#xH; in text and attribute values.
    // Whether the engine's own reader does this is not stated by any spec (the shipped
    // data contains &lt; / &gt; / &amp; only, e.g. `Display Case&lt;001&gt;`).
    bool decodeEntities = true;
};

class Document {
public:
    Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = default;
    Document& operator=(Document&&) = default;

    // The outermost element (the first top-level element). Non-null after a
    // successful parse. For a `.xtbl` this is `<root>` (spec-xtbl-format.md 2).
    const Node* root() const { return root_; }
    // FindChild(root(), "Table"): the row container of the files with the <root><Table>
    // shape (1,962 of the 2,222 shipped entries); nullptr for the others (action_nodes.xtbl and
    // node_graph_files.xtbl take their rows directly from root(); the ~250 animation
    // `blend_tree` / `state_machine` files and a few settings files have no Table at all).
    const Node* table() const;
    const std::vector<const Node*>& topLevel() const { return topLevel_; }

    const std::vector<Warning>& warnings() const { return warnings_; }
    // Total recoveries, including any beyond the stored cap (kMaxStoredWarnings).
    size_t warningCount() const { return warningCount_; }
    static constexpr size_t kMaxStoredWarnings = 10000;

    bool hasUtf8Bom() const { return bom_; }
    size_t elementCount() const { return nodes_.size(); }
    // Elements with child elements AND non-whitespace text of their own (legal, common in
    // the shipped tables; not a warning). See the TEXT rules.
    size_t mixedContentElements() const { return mixed_; }
    // Of those, the ones that also have non-whitespace text AFTER a child element.
    size_t textAfterChildElements() const { return textAfterChild_; }

private:
    friend class Parser;
    std::deque<Node> nodes_;          // stable addresses
    const Node* root_ = nullptr;
    std::vector<const Node*> topLevel_;
    std::vector<Warning> warnings_;
    size_t warningCount_ = 0;
    size_t mixed_ = 0;
    size_t textAfterChild_ = 0;
    bool bom_ = false;
};

// Parses `size` bytes. Throws FormatError for input that is not XML-like (see
// above). The bytes need not be NUL-terminated and are not modified.
Document ParseDocument(const uint8_t* data, size_t size, const ParseOptions& options = {});
Document ParseDocument(std::string_view bytes, const ParseOptions& options = {});

// ---------------------------------------------------------------------------
// Name comparison and navigation (traffic-ai 1.3: 0x00DAB9E0, 0x00DAB9F0,
// 0x00DABA00, 0x00DC5070)
// ---------------------------------------------------------------------------
// ASCII case-insensitive equality, the engine's element-name comparison.
bool NameEquals(std::string_view a, std::string_view b);

// First child with that name, else nullptr.                       FindChild
const Node* FindChild(const Node* node, std::string_view name);
// The next sibling AFTER `cur` with that name - the row-iteration primitive:
//   for (n = FindChild(table, "Row"); n; n = NextSibling(table, n, "Row"))
// `cur` must be a child of `parent` (otherwise nullptr); a null `cur` gives nullptr.
const Node* NextSibling(const Node* parent, const Node* cur, std::string_view name);
// Number of children with that name.                              CountChildren
size_t CountChildren(const Node* node, std::string_view name);
// The n-th (0-based) child with that name, else nullptr.          NthChild
const Node* NthChild(const Node* node, std::string_view name, size_t n);
// All children with that name, in order (a convenience over the primitives above).
std::vector<const Node*> Children(const Node* node, std::string_view name);

// ---------------------------------------------------------------------------
// Text getters (0x00DABA10 / 0x00DABA70 / 0x00DABAB0)
// ---------------------------------------------------------------------------
// Text of the child (or of `node` itself when name is empty): nullptr if the
// node/child is absent OR has no text. Pointer into the document.  ChildText
const std::string* ChildText(const Node* node, std::string_view name);
// Bounded copy: at most bufferSize-1 bytes (the engine forces a NUL at
// dst[size-1]). nullopt when the child is absent or has no text (the engine
// leaves dst untouched, CopyTextOk returns false).                CopyText[Ok]
std::optional<std::string> CopyText(const Node* node, std::string_view name, size_t bufferSize);

// ---------------------------------------------------------------------------
// Text -> value (the engine's number grammar; pure functions, no node needed)
// ---------------------------------------------------------------------------
// The engine integer parser: decimal digits, or 0x/0X then hex digits, stopping at
// the first non-digit; unsigned accumulation that wraps modulo 2^32 (no overflow
// check); no sign, no whitespace skipping. `consumed` (optional) receives the number
// of bytes read. Text that does not start with a digit reads as 0 with consumed 0.
uint32_t ParseEngineInteger(std::string_view text, size_t* consumed = nullptr);

int32_t  ParseInt32(std::string_view text);   // one leading '-' stripped, then ParseEngineInteger
uint32_t ParseUInt32(std::string_view text);  // NO sign handling: "-5" reads as 0
uint16_t ParseUInt16(std::string_view text);  // parse to 32 bits, truncate
int16_t  ParseInt16(std::string_view text);
uint8_t  ParseUInt8(std::string_view text);
int8_t   ParseInt8(std::string_view text);
// true iff `text` equals "true" or "yes" in full, ASCII case-insensitively.
bool     ParseBool(std::string_view text);

// The engine float grammar:
//   optional '-'; then, if the token starts 0x/0X, the result is 0.0 (positive zero
//   here; the specs do not say what sign the engine gives it); a leading '.' is accepted
//   (".5" and "-.5" read 0.5 and -0.5); the integer part is read by the engine integer
//   parser (decimal digits, stopping at the first non-digit, no overflow check, so it
//   wraps modulo 2^32 like the integer readers); a '.' starts the fraction, each digit
//   weighted by successive x0.1 in SINGLE precision - i.e. w = 0.1f, then value += digit*w,
//   w *= 0.1f, all in float; 'e'/'E' starts a decimal exponent (an optional '-', then
//   decimal digits) applied as x10^n; anything after that is ignored; text that does not
//   start like a number reads as 0.
// Not specified, chosen here: the exponent step is one double-precision multiply by 10^n
// narrowed to float (so 1.5e-2 reads as the float nearest 0.015), and a '+' sign in the
// exponent is not handled (it ends the number, exponent 0).
float ParseFloat(std::string_view text);

// ---------------------------------------------------------------------------
// Scalar readers
// ---------------------------------------------------------------------------
template <class T>
struct Always {
    T value{};          // 0 when !present - NOT the engine's (unspecified) value
    bool present = false;
};

namespace detail {
template <class T, class Parse>
inline std::optional<T> getWith(const Node* node, std::string_view name, Parse parse) {
    const std::string* t = ChildText(node, name);
    if (!t) return std::nullopt;
    return static_cast<T>(parse(std::string_view(*t)));
}
template <class T, class Parse>
inline Always<T> alwaysWith(const Node* node, std::string_view name, Parse parse) {
    Always<T> r;
    const std::string* t = ChildText(node, name);
    if (t) { r.value = static_cast<T>(parse(std::string_view(*t))); r.present = true; }
    return r;
}
} // namespace detail

// "Write only if present" readers.
inline std::optional<int32_t>  GetInt32 (const Node* n, std::string_view name = {}) { return detail::getWith<int32_t >(n, name, ParseInt32 ); }
inline std::optional<uint32_t> GetUInt32(const Node* n, std::string_view name = {}) { return detail::getWith<uint32_t>(n, name, ParseUInt32); }
inline std::optional<int16_t>  GetInt16 (const Node* n, std::string_view name = {}) { return detail::getWith<int16_t >(n, name, ParseInt16 ); }
inline std::optional<uint16_t> GetUInt16(const Node* n, std::string_view name = {}) { return detail::getWith<uint16_t>(n, name, ParseUInt16); }
inline std::optional<int8_t>   GetInt8  (const Node* n, std::string_view name = {}) { return detail::getWith<int8_t  >(n, name, ParseInt8  ); }
inline std::optional<uint8_t>  GetUInt8 (const Node* n, std::string_view name = {}) { return detail::getWith<uint8_t >(n, name, ParseUInt8 ); }
inline std::optional<bool>     GetBool  (const Node* n, std::string_view name = {}) { return detail::getWith<bool    >(n, name, ParseBool  ); }
inline std::optional<float>    GetFloat (const Node* n, std::string_view name = {}) { return detail::getWith<float   >(n, name, ParseFloat ); }

// "Always write" readers (see the header comment: check `present`).
inline Always<int32_t>  ReadInt32Always (const Node* n, std::string_view name = {}) { return detail::alwaysWith<int32_t >(n, name, ParseInt32 ); }
inline Always<uint32_t> ReadUInt32Always(const Node* n, std::string_view name = {}) { return detail::alwaysWith<uint32_t>(n, name, ParseUInt32); }
inline Always<int16_t>  ReadInt16Always (const Node* n, std::string_view name = {}) { return detail::alwaysWith<int16_t >(n, name, ParseInt16 ); }
inline Always<uint16_t> ReadUInt16Always(const Node* n, std::string_view name = {}) { return detail::alwaysWith<uint16_t>(n, name, ParseUInt16); }
inline Always<int8_t>   ReadInt8Always  (const Node* n, std::string_view name = {}) { return detail::alwaysWith<int8_t  >(n, name, ParseInt8  ); }
inline Always<uint8_t>  ReadUInt8Always (const Node* n, std::string_view name = {}) { return detail::alwaysWith<uint8_t >(n, name, ParseUInt8 ); }
inline Always<bool>     ReadBoolAlways  (const Node* n, std::string_view name = {}) { return detail::alwaysWith<bool    >(n, name, ParseBool  ); }
inline Always<float>    ReadFloatAlways (const Node* n, std::string_view name = {}) { return detail::alwaysWith<float   >(n, name, ParseFloat ); }

// ---------------------------------------------------------------------------
// vec3 (0x00DACF20 / 0x00DACF60): children X, Y, Z, each read by the "always"
// float reader. `present` is whether the vector element itself exists (for the
// named form: the engine leaves the whole destination untouched when it does
// not); each component keeps its own `present` because a present vector may
// still lack a component.
// ---------------------------------------------------------------------------
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
struct Vec3Result {
    bool present = false;
    Always<float> x, y, z;
    bool complete() const { return present && x.present && y.present && z.present; }
    Vec3 value() const { return Vec3{x.value, y.value, z.value}; }
};
Vec3Result ReadVec3(const Node* vecNode);                              // children of vecNode itself
Vec3Result ReadVec3Child(const Node* node, std::string_view name);     // children of node/name

// ---------------------------------------------------------------------------
// Flag lists and enums (0x00DAC740 / 0x00DAC7D0 / 0x00DAC830)
// ---------------------------------------------------------------------------
// Bitmask over every child of `node` named "Flag": for each, the text is matched
// case-insensitively against names[0..count) and bit i is OR-ed in for the FIRST
// match. A Flag matching no name, or with no text, contributes nothing. Names beyond
// index 31 cannot be represented and are ignored.
uint32_t FlagMask(const Node* node, const std::string_view* names, size_t count);
uint32_t FlagMask(const Node* node, const std::vector<std::string_view>& names);
uint32_t FlagMask(const Node* node, std::initializer_list<std::string_view> names);
// True iff some "Flag" child of `node` has exactly this text (case-insensitive).
bool HasFlag(const Node* node, std::string_view flag);
// Index of the node's own text among names[] (case-insensitive, first match);
// -1 if the node is null, has no text, or nothing matches.
int EnumIndex(const Node* node, const std::string_view* names, size_t count);
int EnumIndex(const Node* node, const std::vector<std::string_view>& names);
int EnumIndex(const Node* node, std::initializer_list<std::string_view> names);

// ---------------------------------------------------------------------------
// The engine name hash (traffic-ai 1.4; also spec-tables-weapons-combat.md 1.4,
// spec-tables-progression.md 1.3, spec-save-format.md 9.8)
// ---------------------------------------------------------------------------
// Table-driven reflected CRC-32 (polynomial 0xEDB88320): for each byte b (ASCII
// upper-case letters folded to lower case),
//     crc = (crc >> 8) ^ T[(b ^ crc) & 0xFF]
// with `seed` as the starting register value and NO final XOR. Derived here from the
// specs' description of the routine ("table-driven reflected CRC-32 ... lower-cases each
// byte ... no final XOR", "the standard reflected table"), with the 256-entry table
// generated from the polynomial; nothing was taken from code. Every documented caller
// passes seed 0; a few chain one string's result as the seed of the next
// (spec-tables-traffic-ai.md section 5), and spec-tables-weapons-combat.md 1.4 mentions a
// caller with seed 0xFFFFFFFF, so the seed is a parameter.
uint32_t NameHash(std::string_view text, uint32_t seed = 0);
// The same routine WITHOUT the lower-casing (the engine's sibling 0x00D9E7E0).
uint32_t NameHashCaseSensitive(std::string_view text, uint32_t seed = 0);
// A NULL name hashes to 0 (spec-tables-weapons-combat.md 1.4); use with ChildText().
uint32_t NameHashOrZero(const std::string* text, uint32_t seed = 0);
// The 256-entry table, exposed for tests (T[1] == 0x77073096, T[2] == 0xEE0E612C).
const uint32_t* NameHashTable();

} // namespace sr3xtbl

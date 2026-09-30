// Synthetic tests for sr3xtbl. Every fixture is built from the TEXT of the specs, never
// from the library's own source:
//   * spec-xtbl-format.md 2-3 (the <root><Table><Row> shape, schema blocks) and 7 (the three
//     tolerated authoring defects, with the exact strings the spec quotes);
//   * spec-tables-traffic-ai.md 1.2 (node model: NULL text for an element with no text;
//     case-insensitive names), 1.3 (accessors, float grammar, bool, flags, enums), 1.4 (hash);
//   * spec-tables-weapons-combat.md 1.3 (narrow integer widths, "-1 if the node is NULL");
//   * spec-save-format.md 6.1 (CRC table: T[1] = 0x77073096, T[2] = 0xEE0E612C).
// Each test names the statement it rests on and the mistake it would catch. Validation
// against the real shipped files is tools/validation/validate_xtbl_population.cpp.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "sr3xtbl/xtbl.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"          \
                      << __LINE__ << "\n";                                   \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

// The input must be rejected with FormatError.
#define CHECK_REJECTS(text)                                                  \
    do {                                                                     \
        bool threw_ = false;                                                 \
        try { (void)sr3xtbl::ParseDocument(std::string_view(text)); }        \
        catch (const sr3xtbl::FormatError&) { threw_ = true; }               \
        if (!threw_) {                                                       \
            std::cerr << "CHECK FAILED: expected FormatError for " #text " at " __FILE__ ":" << __LINE__ << "\n"; \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

using namespace sr3xtbl;

Document P(std::string_view s) { return ParseDocument(s); }

size_t countKind(const Document& d, WarningKind k) {
    size_t n = 0;
    for (const Warning& w : d.warnings()) n += (w.kind == k);
    return n;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
bool near(float a, float b, float tol) { return (a > b ? a - b : b - a) <= tol; }

// ---------------------------------------------------------------------------
// 1. document model (traffic-ai 1.2), spec-xtbl-format.md 2-3
// ---------------------------------------------------------------------------
void testModel() {
    // The CRLF, tab-indented <root><Table><Row> shape of spec-xtbl-format.md 2-3.
    const char* xml =
        "<root>\r\n"
        "\t<Table>\r\n"
        "\t\t<Achievement>\r\n"
        "\t\t\t<Name>Dead Presidents</Name>\r\n"
        "\t\t\t<Empty></Empty>\r\n"
        "\t\t\t<SelfClosed/>\r\n"
        "\t\t\t<Spaced />\r\n"
        "\t\t</Achievement>\r\n"
        "\t\t<Achievement><Name>Beta</Name></Achievement>\r\n"
        "\t\t<Other><Name>x</Name></Other>\r\n"
        "\t\t<Achievement><Name>Gamma</Name></Achievement>\r\n"
        "\t</Table>\r\n"
        "</root>\r\n";
    Document d = P(xml);
    CHECK(d.warnings().empty());
    CHECK(d.root() != nullptr && d.root()->name() == "root");
    const Node* table = d.table();
    CHECK(table != nullptr && table->name() == "Table");
    CHECK(table == FindChild(d.root(), "table"));  // case-insensitive (1.2: _stricmp)
    CHECK(d.root()->parent() == nullptr && table->parent() == d.root());

    // 1.3: CountChildren / FindChild / NextSibling(parent, cur, name) / NthChild
    CHECK(CountChildren(table, "Achievement") == 3);
    CHECK(CountChildren(table, "ACHIEVEMENT") == 3);
    CHECK(CountChildren(table, "Other") == 1);
    CHECK(CountChildren(table, "Nope") == 0);
    const Node* r0 = FindChild(table, "Achievement");
    CHECK(r0 != nullptr);
    const Node* r1 = NextSibling(table, r0, "Achievement");
    CHECK(r1 != nullptr && r1 != r0);
    CHECK(*ChildText(r1, "Name") == "Beta");
    const Node* r2 = NextSibling(table, r1, "Achievement");  // skips <Other>
    CHECK(r2 != nullptr && *ChildText(r2, "Name") == "Gamma");
    CHECK(NextSibling(table, r2, "Achievement") == nullptr);
    CHECK(NextSibling(table, r0, "Other") == FindChild(table, "Other"));
    CHECK(NthChild(table, "Achievement", 0) == r0 && NthChild(table, "Achievement", 1) == r1 &&
          NthChild(table, "Achievement", 2) == r2 && NthChild(table, "Achievement", 3) == nullptr);
    CHECK(Children(table, "achievement").size() == 3);
    // NextSibling requires cur to be a child of parent (a wrong parent must not walk someone else's list)
    CHECK(NextSibling(r0, r1, "Achievement") == nullptr);
    CHECK(NextSibling(table, nullptr, "Achievement") == nullptr);
    CHECK(NextSibling(d.root(), d.root(), "Table") == nullptr);  // a top-level element has no siblings

    // 1.2 "text is NULL for an element with no text"
    const Node* name = FindChild(r0, "Name");
    CHECK(name->text() != nullptr && *name->text() == "Dead Presidents");
    CHECK(r0->text() == nullptr);                    // indentation between children is not text
    CHECK(table->text() == nullptr && d.root()->text() == nullptr);
    CHECK(FindChild(r0, "Empty") != nullptr && FindChild(r0, "Empty")->text() == nullptr);  // <A></A>
    CHECK(FindChild(r0, "SelfClosed") != nullptr && FindChild(r0, "SelfClosed")->text() == nullptr);
    CHECK(FindChild(r0, "Spaced") != nullptr);       // `<Spaced />`: the trailing space is not part of the name
    // absent (no such child) vs empty (child, no text): both give a null ChildText, but only one FindChild
    CHECK(ChildText(r0, "Empty") == nullptr && ChildText(r0, "Missing") == nullptr);
    CHECK(FindChild(r0, "Empty") != nullptr && FindChild(r0, "Missing") == nullptr);
    // ChildText with an empty name is the node's own text (the engine's NULL child name)
    CHECK(ChildText(name, {}) == name->text());
    CHECK(ChildText(nullptr, "Name") == nullptr && FindChild(nullptr, "x") == nullptr && CountChildren(nullptr, "x") == 0);

    // children() are in document order, indexInParent() consistent
    const auto& kids = table->children();
    CHECK(kids.size() == 4);
    for (size_t i = 0; i < kids.size(); ++i) CHECK(kids[i]->indexInParent() == i);
    CHECK(kids[0]->name() == "Achievement" && kids[2]->name() == "Other");

    // Document survives a move (node addresses are stable)
    const Node* rootBefore = d.root();
    Document d2 = std::move(d);
    CHECK(d2.root() == rootBefore && d2.root()->name() == "root");
}

void testShapes() {
    // spec-xtbl-format.md 2/3.2: schema blocks may sit before or after <Table> inside <root>
    Document a = P("<root><TableDescription source=\"x.xml\"><Name>X</Name></TableDescription><Table><Row/></Table><EntryCategories/></root>");
    CHECK(a.warnings().empty());
    CHECK(a.table() != nullptr && CountChildren(a.table(), "Row") == 1);
    CHECK(a.root()->children()[0]->name() == "TableDescription");
    // traffic-ai 1.2: action_nodes.xtbl / node_graph_files.xtbl have NO Table wrapper
    Document b = P("<root><Row><Name>a</Name></Row><Row><Name>b</Name></Row></root>");
    CHECK(b.table() == nullptr);
    CHECK(CountChildren(b.root(), "Row") == 2);
    // the attribute the real TableDescription carries is exposed, not folded into the name
    const Node* td = FindChild(a.root(), "TableDescription");
    CHECK(td->name() == "TableDescription");
    CHECK(td->attributes().size() == 1 && td->attributes()[0].name == "source" && td->attributes()[0].value == "x.xml");
    CHECK(td->attribute("SOURCE") != nullptr && *td->attribute("SOURCE") == "x.xml" && td->attribute("nope") == nullptr);
    Document c = P("<r><T a=\"1\" b='two &amp; three'/></r>");
    CHECK(c.warnings().empty());
    const Node* t = FindChild(c.root(), "T");
    CHECK(t->attributes().size() == 2 && *t->attribute("b") == "two & three");
}

// ---------------------------------------------------------------------------
// 2. whitespace / text rules
// ---------------------------------------------------------------------------
void testText() {
    Document d = P("<r><A>  x  </A><B> </B><C>\r\n\t</C><D>\n<E/>\n</D><F>text<G/>more</F><H>0.0<X>1</X>\r\n</H></r>");
    // text is never trimmed (spec-tables-environment.md 1.3: "text is never trimmed by the lookup")
    CHECK(*FindChild(d.root(), "A")->text() == "  x  ");
    // a LEAF with whitespace-only text keeps it (present, exactly as written)
    CHECK(FindChild(d.root(), "B")->text() != nullptr && *FindChild(d.root(), "B")->text() == " ");
    CHECK(FindChild(d.root(), "C")->text() != nullptr && *FindChild(d.root(), "C")->text() == "\r\n\t");
    // an element WITH children and whitespace-only text has NULL text
    CHECK(FindChild(d.root(), "D")->text() == nullptr);
    // mixed content (real: `<Difficulty_Level>0.0<Damage_Received_Mult>..`): legal, no warning, counted;
    // the text is the non-whitespace-only segments in order, verbatim
    CHECK(*FindChild(d.root(), "F")->text() == "textmore");
    CHECK(*FindChild(d.root(), "H")->text() == "0.0" && *ChildText(FindChild(d.root(), "H"), "X") == "1");
    CHECK(d.warnings().empty());
    CHECK(d.mixedContentElements() == 2 && d.textAfterChildElements() == 1);
    Document ml = P("<r><V>skybox_clouds\r\n\t\t<M>x</M>\r\n\t</V></r>");    // trailing whitespace of the value is kept
    CHECK(*FindChild(ml.root(), "V")->text() == "skybox_clouds\r\n\t\t" && ml.mixedContentElements() == 1 && ml.textAfterChildElements() == 0);

    // line endings and other bytes pass through untouched
    Document e = P("<r><A>a\rb\r\r\rc\nd</A></r>");   // bare CRs, as in a real Description
    CHECK(*FindChild(e.root(), "A")->text() == "a\rb\r\r\rc\nd");
    CHECK(e.warnings().empty());
    Document u = P("<r><A>Boss\xC3\xA9</A></r>");      // UTF-8 bytes preserved, no warning
    CHECK(*FindChild(u.root(), "A")->text() == "Boss\xC3\xA9" && u.warnings().empty());

    // XML constructs the shipped files do not use but a table tool could emit
    Document x = P("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!DOCTYPE root [<!ELEMENT root ANY>]>\n<!-- c1 -->\n<root><!-- c2 --><A>1<!-- c3 -->2</A><B><![CDATA[<raw> & stuff]]></B><?pi x?></root>\n<!-- tail -->\n");
    CHECK(x.hasUtf8Bom());
    CHECK(x.warnings().empty());
    CHECK(x.root()->name() == "root");
    CHECK(*FindChild(x.root(), "A")->text() == "12");
    CHECK(*FindChild(x.root(), "B")->text() == "<raw> & stuff");
    Document nb = P("<r/>");
    CHECK(!nb.hasUtf8Bom() && nb.root()->children().empty() && nb.root()->text() == nullptr);
    Document cd = P("<r><A><![CDATA[]]></A></r>");   // empty CDATA -> no text
    CHECK(FindChild(cd.root(), "A")->text() == nullptr);

    // entities: the five named ones and numeric references; the real data has &lt; &gt; &amp;
    Document en = P("<r><A>Display Case&lt;001&gt; &amp; &quot;q&quot; &apos;a&apos; &#65;&#x42;&#xE9;</A></r>");
    CHECK(*FindChild(en.root(), "A")->text() == "Display Case<001> & \"q\" 'a' AB\xC3\xA9");
    CHECK(en.warnings().empty());
    ParseOptions raw; raw.decodeEntities = false;
    Document rw = ParseDocument(std::string_view("<r><A>a&lt;b</A></r>"), raw);
    CHECK(*FindChild(rw.root(), "A")->text() == "a&lt;b");
    Document bad = P("<r><A>x &foo; y & z &#0; &#xD800; &#12a;</A></r>");
    CHECK(*FindChild(bad.root(), "A")->text() == "x &foo; y & z &#0; &#xD800; &#12a;");  // kept verbatim
    CHECK(countKind(bad, WarningKind::UnknownEntity) == 2);
    CHECK(countKind(bad, WarningKind::BadNumericEntity) == 3);

    // a '<' that cannot start a tag stays text
    Document lt = P("<r><A>1 < 2</A></r>");
    CHECK(*FindChild(lt.root(), "A")->text() == "1 < 2");
    CHECK(countKind(lt, WarningKind::StrayLessThan) == 1);

    // case-insensitive names everywhere (1.2)
    Document ci = P("<Root><ROW><name>v</name></ROW></Root>");
    CHECK(FindChild(FindChild(ci.root(), "row"), "NAME") != nullptr);
    CHECK(NameEquals("Table", "tABLE") && !NameEquals("Table", "Tables") && !NameEquals("a", "b"));
}

// ---------------------------------------------------------------------------
// 3. the three tolerated defects of spec-xtbl-format.md 7, plus the recovery rules
// ---------------------------------------------------------------------------
void testTolerance() {
    // (a) template.xtbl: `<RampExposure>False</Exposure>` - the close tag names no open element,
    //     so it closes the innermost open element (RampExposure); parsing carries on normally.
    {
        Document d = P("<root><Table><Template><RampExposure>False</Exposure><Next>1</Next></Template></Table></root>");
        CHECK(d.warningCount() == 1 && countKind(d, WarningKind::MismatchedCloseTag) == 1);
        const Node* t = FindChild(d.table(), "Template");
        CHECK(t != nullptr);
        CHECK(*ChildText(t, "RampExposure") == "False");
        CHECK(*ChildText(t, "Next") == "1");                   // a sibling of RampExposure, not nested in it
        CHECK(CountChildren(t, "RampExposure") == 1 && FindChild(FindChild(t, "RampExposure"), "Next") == nullptr);
        CHECK(d.elementCount() == 5);
        CHECK(d.warnings()[0].detail.find("Exposure") != std::string::npos);
    }
    // (b) 07_out.cte_xtbl: `<Name>09_PLayer\x1f_Neg</Name>` - the 0x1F stays in the text
    {
        const std::string xml = std::string("<root><Table><Light><Name>09_PLayer\x1f") + "_Neg</Name></Light></Table></root>";
        Document d = P(xml);
        CHECK(d.warningCount() == 1 && countKind(d, WarningKind::ControlCharacterInText) == 1);
        const std::string* n = ChildText(FindChild(d.table(), "Light"), "Name");
        CHECK(n != nullptr && *n == std::string("09_PLayer\x1f") + "_Neg" && n->size() == 14);
        CHECK(NameHash(*n) != NameHash("09_player_neg"));      // the byte is part of the row key
        // tab/CR/LF are not control characters for this purpose
        Document ok = P("<r><A>a\tb\r\nc</A></r>");
        CHECK(ok.warnings().empty());
    }
    // (c) xbox360_text.xtbl: `<Xbox 360_Identifier>` - the element name contains a space
    {
        Document d = P("<root><Table><Xbox 360_Identifier><Name>BTN_A_TXT</Name></Xbox 360_Identifier>"
                       "<Xbox 360_Identifier><Name>BTN_B_TXT</Name></Xbox 360_Identifier></Table></root>");
        CHECK(countKind(d, WarningKind::NameContainsWhitespace) == 2);
        CHECK(d.warningCount() == 2);                          // the close tags matched: no mismatch warning
        CHECK(CountChildren(d.table(), "Xbox 360_Identifier") == 2);
        CHECK(CountChildren(d.table(), "xbox 360_identifier") == 2);   // still case-insensitive
        CHECK(*ChildText(FindChild(d.table(), "Xbox 360_Identifier"), "Name") == "BTN_A_TXT");
        CHECK(FindChild(d.table(), "Xbox 360_Identifier")->name() == "Xbox 360_Identifier");
        CHECK(FindChild(d.table(), "Xbox") == nullptr);        // the name is NOT truncated at the space
        // a self-closing one, and one with a space before the '/'
        Document s = P("<r><A B/><A B /></r>");
        CHECK(CountChildren(s.root(), "A B") == 2);
    }

    // recovery rules beyond the three
    {   // close tag names an open ANCESTOR: the elements in between are closed implicitly
        Document d = P("<a><b><c>x</a><d/></b>");
        CHECK(countKind(d, WarningKind::UnclosedElement) == 2);   // c, b closed by </a>
        CHECK(d.topLevel().size() == 2 && d.topLevel()[1]->name() == "d");   // <d/> is after the root closed
        CHECK(countKind(d, WarningKind::ExtraTopLevelElement) == 1);
        CHECK(countKind(d, WarningKind::StrayCloseTag) == 1);      // the final </b>
        CHECK(*ChildText(FindChild(d.root(), "b"), "c") == "x");
    }
    {   // close tag differing only in case is accepted, with a warning
        Document d = P("<root><Name>x</name></root>");
        CHECK(countKind(d, WarningKind::CloseTagCaseDiffers) == 1 && d.warningCount() == 1);
        CHECK(*ChildText(d.root(), "Name") == "x");
    }
    {   // stray close tag with nothing open is ignored
        Document d = P("<a/></b>");
        CHECK(countKind(d, WarningKind::StrayCloseTag) == 1);
    }
    {   // the input ends between tags: the open elements are closed at EOF, each recorded
        Document d = P("<root><Table><Row><Name>x</Name>");
        CHECK(countKind(d, WarningKind::UnclosedAtEof) == 3);
        CHECK(*ChildText(FindChild(FindChild(d.table(), "Row"), "Name"), {}) == "x");
        Document e = P("<root>");
        CHECK(countKind(e, WarningKind::UnclosedAtEof) == 1 && e.root() != nullptr);
    }
    {   // text outside the root is dropped; comments/whitespace outside are fine
        Document d = P("<a/> junk <!-- c -->\n");
        CHECK(countKind(d, WarningKind::TextOutsideRoot) == 1);
        CHECK(d.root()->name() == "a");
    }
    {   // warnings are capped in storage but counted in full; deep nesting is not recursive
        std::string deep;
        const size_t depth = 30000;
        for (size_t i = 0; i < depth; ++i) deep += "<a>";
        Document d = P(deep);
        CHECK(d.elementCount() == depth);
        CHECK(d.warningCount() == depth && d.warnings().size() == Document::kMaxStoredWarnings);
    }
}

void testRejections() {
    // "not XML-like at all" must be an error, never an empty document
    CHECK_REJECTS("");
    CHECK_REJECTS("   \r\n\t ");
    CHECK_REJECTS("hello world");
    CHECK_REJECTS("plain text <root/>");                 // does not begin with '<'
    CHECK_REJECTS("<!-- only a comment -->");            // no root element
    CHECK_REJECTS("<?xml version=\"1.0\"?>");            // no root element
    CHECK_REJECTS("<!DOCTYPE root>");
    CHECK_REJECTS("<root><Table");                        // truncated in the middle of a tag
    CHECK_REJECTS("<root><Table>x</Tab");                 // ... of a close tag
    CHECK_REJECTS("<root>text<");                         // input ends right after '<'
    CHECK_REJECTS("<root><!-- never closed");
    CHECK_REJECTS("<root><![CDATA[never closed");
    CHECK_REJECTS("<root><?pi never closed");
    CHECK_REJECTS(std::string_view("\xFF\xFE<\0r\0o\0o\0t\0/\0>\0", 16));   // UTF-16 LE with BOM
    CHECK_REJECTS(std::string_view("\xFE\xFF\0<\0r\0/\0>", 10));           // UTF-16 BE with BOM
    // The FormatError carries an offset
    try { (void)P("<a><b"); CHECK(false); } catch (const FormatError& e) { CHECK(e.offset() == 3); }
    // an attribute whose quote never closes does not run away: the tag ends at the next '>' and is kept as a
    // whitespace-containing name (recorded), not silently misread
    {
        Document q = P("<root><A b=\"unterminated>x</A></root>");
        CHECK(countKind(q, WarningKind::NameContainsWhitespace) == 1);
    }
    // ... and every one of the accepted degenerate forms is accepted
    CHECK(P("<r/>").root() != nullptr);
    CHECK(P("\n\n<r></r>\n").root() != nullptr);
}

// ---------------------------------------------------------------------------
// 4. text -> value (traffic-ai 1.3)
// ---------------------------------------------------------------------------
void testIntegers() {
    // engine integer parser: decimal, or 0x/0X hex; stops at the first non-digit
    CHECK(ParseInt32("5") == 5 && ParseInt32("0") == 0 && ParseInt32("007") == 7);
    CHECK(ParseInt32("-5") == -5);                        // signed readers strip one leading '-'
    CHECK(ParseInt32("0x10") == 16 && ParseInt32("0X1f") == 31 && ParseInt32("0xFF") == 255);
    CHECK(ParseInt32("-0x10") == -16);
    CHECK(ParseInt32("12abc") == 12 && ParseInt32("0x1Fg") == 31 && ParseInt32("3.9") == 3);
    CHECK(ParseInt32("") == 0 && ParseInt32("abc") == 0 && ParseInt32("-") == 0 && ParseInt32("--5") == 0 && ParseInt32("0x") == 0);
    CHECK(ParseInt32(" 5") == 0);                          // no whitespace skipping (documented choice)
    // no overflow check: wraps modulo 2^32
    CHECK(ParseUInt32("4294967295") == 0xFFFFFFFFu && ParseUInt32("4294967296") == 0u && ParseInt32("4294967295") == -1);
    CHECK(ParseInt32("2147483648") == INT32_MIN);
    // UNSIGNED: no sign handling - a leading '-' parses as 0 (1.3: "a leading `-` parses as 0")
    CHECK(ParseUInt32("-5") == 0);
    CHECK(ParseUInt32("-0x10") == 0);
    CHECK(ParseUInt32("5") == 5 && ParseUInt32("0x10") == 16 && ParseUInt32("") == 0);
    // narrow readers parse to 32 bits then truncate (weapons-combat 1.3)
    CHECK(ParseUInt8("200") == 200 && ParseUInt8("300") == 44 && ParseUInt8("0x1FF") == 255 && ParseUInt8("-1") == 0);
    CHECK(ParseUInt16("70000") == 4464 && ParseUInt16("65535") == 65535);
    CHECK(ParseInt16("-1") == -1 && ParseInt16("-32768") == INT16_MIN && ParseInt8("-1") == -1 && ParseInt8("300") == 44);
    size_t used = 99;
    CHECK(ParseEngineInteger("123abc", &used) == 123 && used == 3);
    CHECK(ParseEngineInteger("0x1F.", &used) == 31 && used == 4);
    CHECK(ParseEngineInteger("abc", &used) == 0 && used == 0);
}

void testBool() {
    // 1.3: only `true` and `yes` (whole string, case-insensitive) are true; every other text is false
    for (const char* t : {"true", "True", "TRUE", "tRuE", "yes", "Yes", "YES"}) CHECK(ParseBool(t));
    for (const char* t : {"false", "False", "no", "NO", "1", "0", "on", "y", "t", "truee", "tru", " true", "true ", "yess", "", " ", "yes,no"}) CHECK(!ParseBool(t));
    Document d = P("<r><A>Yes</A><B>false</B><C>1</C><D></D></r>");
    CHECK(GetBool(d.root(), "A") == std::optional<bool>(true));
    CHECK(GetBool(d.root(), "B") == std::optional<bool>(false));
    CHECK(GetBool(d.root(), "C") == std::optional<bool>(false));   // a present text that is not true/yes reads false
    CHECK(!GetBool(d.root(), "D").has_value() && !GetBool(d.root(), "Z").has_value());
}

void testFloat() {
    // 1.3, "Engine float grammar"
    // "a token starting 0x/0X returns 0.0" - while the INTEGER reader honours the same text
    CHECK(ParseFloat("0x10") == 0.0f && ParseFloat("0X1F") == 0.0f && ParseFloat("-0x10") == 0.0f && ParseFloat("0x") == 0.0f);
    CHECK(ParseInt32("0x10") == 16);                       // (so a float reader that honoured 0x would read 16)
    // "a leading '.' is accepted"
    CHECK(sameBits(ParseFloat(".5"), 0.5f));
    CHECK(sameBits(ParseFloat("-.5"), -0.5f));
    CHECK(sameBits(ParseFloat("5."), 5.0f));
    CHECK(sameBits(ParseFloat("-5"), -5.0f) && sameBits(ParseFloat("12"), 12.0f) && sameBits(ParseFloat("0"), 0.0f));
    CHECK(sameBits(ParseFloat("1.5"), 1.5f) && sameBits(ParseFloat("-1.5"), -1.5f) && sameBits(ParseFloat("30.000000"), 30.0f));
    // "e/E starts a decimal exponent applied as x10^n"
    CHECK(sameBits(ParseFloat("1e3"), 1000.0f) && sameBits(ParseFloat("1E3"), 1000.0f) && sameBits(ParseFloat("2.5e2"), 250.0f));
    CHECK(sameBits(ParseFloat("-1e3"), -1000.0f));
    CHECK(near(ParseFloat("1.5e-2"), 0.015f, 1e-9f));
    CHECK(near(ParseFloat("1e-3"), 0.001f, 1e-9f));
    CHECK(sameBits(ParseFloat("1e"), 1.0f));               // exponent without digits: x10^0
    // the integer part is read by the integer parser and stops at the first non-digit; junk after the number is ignored
    CHECK(sameBits(ParseFloat("12.5.7"), 12.5f) && sameBits(ParseFloat("1.5abc"), 1.5f) && sameBits(ParseFloat("7x"), 7.0f));
    // text that does not start like a number reads as 0
    CHECK(ParseFloat("") == 0.0f && ParseFloat("abc") == 0.0f && ParseFloat("-") == 0.0f && ParseFloat(".") == 0.0f && ParseFloat(" 1.5") == 0.0f);
    // "each digit weighted by successive x0.1 in SINGLE precision": w = 0.1f, value += digit * w, w *= 0.1f.
    {
        float w = 0.1f, v = 0.0f;
        v = v + 0.0f * w; w = w * 0.1f;
        v = v + 2.0f * w;
        CHECK(sameBits(ParseFloat("0.02"), v));
        CHECK(!sameBits(ParseFloat("0.02"), 0.02f));       // the weighting is NOT correctly-rounded parsing
    }
    {
        float w = 0.1f, v = 3.0f;
        const int digits[] = {1, 4, 1, 5, 9};
        for (int dgt : digits) { v = v + static_cast<float>(dgt) * w; w = w * 0.1f; }
        CHECK(sameBits(ParseFloat("3.14159"), v));
        CHECK(near(ParseFloat("3.14159"), 3.14159f, 1e-5f));
    }
    // the shipped format: exactly six decimals (spec-xtbl-format.md 3.4)
    CHECK(near(ParseFloat("-30.000000"), -30.0f, 0.0f) && near(ParseFloat("0.000000"), 0.0f, 0.0f));
    CHECK(near(ParseFloat("12.345678"), 12.345678f, 1e-4f));
    // integer part wraps like the integer parser (no overflow check)
    CHECK(sameBits(ParseFloat("4294967296.5"), 0.5f));
}

// ---------------------------------------------------------------------------
// 5. node-level readers: absence stays observable
// ---------------------------------------------------------------------------
void testReaders() {
    Document d = P(
        "<Row>"
        "<A>5</A><B>-7</B><C>0x1F</C><D>true</D><E>1.5</E><F></F><G>300</G><H>abc</H>"
        "<V><X>1</X><Y>2.5</Y><Z>-3</Z></V><W><X>1</X></W><S>Hello</S>"
        "</Row>");
    const Node* row = d.root();
    // "write only if present" readers
    CHECK(GetInt32(row, "A") == std::optional<int32_t>(5));
    CHECK(GetInt32(row, "B") == std::optional<int32_t>(-7));
    CHECK(GetUInt32(row, "B") == std::optional<uint32_t>(0u));      // unsigned: '-' reads 0, but the element IS present
    CHECK(GetUInt32(row, "C") == std::optional<uint32_t>(31u));
    CHECK(GetBool(row, "D") == std::optional<bool>(true));
    CHECK(GetFloat(row, "E") == std::optional<float>(1.5f));
    CHECK(GetUInt8(row, "G") == std::optional<uint8_t>(static_cast<uint8_t>(44)));
    CHECK(GetUInt16(row, "G") == std::optional<uint16_t>(static_cast<uint16_t>(300)));
    CHECK(GetInt16(row, "B") == std::optional<int16_t>(static_cast<int16_t>(-7)));
    CHECK(GetInt8(row, "B") == std::optional<int8_t>(static_cast<int8_t>(-7)));
    CHECK(GetInt32(row, "H") == std::optional<int32_t>(0));         // present text that is not a number reads 0
    CHECK(GetInt32(row, "a") == std::optional<int32_t>(5));         // case-insensitive
    CHECK(!GetInt32(row, "F").has_value() && FindChild(row, "F") != nullptr);   // present element, no text -> not written
    CHECK(!GetInt32(row, "Missing").has_value() && FindChild(row, "Missing") == nullptr);
    CHECK(!GetInt32(nullptr, "A").has_value() && !GetFloat(nullptr, "A").has_value());
    // the node's own text (empty name)
    CHECK(GetInt32(FindChild(row, "A")) == std::optional<int32_t>(5));
    CHECK(GetFloat(FindChild(row, "E"), {}) == std::optional<float>(1.5f));

    // "always write" readers report presence instead of inventing a value
    Always<int32_t> a = ReadInt32Always(row, "A");
    CHECK(a.present && a.value == 5);
    Always<int32_t> m = ReadInt32Always(row, "Missing");
    CHECK(!m.present);                                            // engine: unspecified; here: flagged, never silently defaulted
    Always<int32_t> f = ReadInt32Always(row, "F");
    CHECK(!f.present);
    CHECK(ReadFloatAlways(row, "E").present && ReadFloatAlways(row, "E").value == 1.5f && !ReadFloatAlways(row, "Nope").present);
    CHECK(ReadBoolAlways(row, "D").present && ReadBoolAlways(row, "D").value);
    CHECK(ReadUInt32Always(row, "B").present && ReadUInt32Always(row, "B").value == 0u);
    CHECK(ReadUInt8Always(row, "G").value == 44 && ReadUInt16Always(row, "G").value == 300 && ReadInt16Always(row, "B").value == -7 && ReadInt8Always(row, "B").value == -7);

    // vec3: children X, Y, Z
    Vec3Result v = ReadVec3Child(row, "V");
    CHECK(v.present && v.complete() && v.value().x == 1.0f && v.value().y == 2.5f && v.value().z == -3.0f);
    Vec3Result w = ReadVec3Child(row, "W");                       // a present vector missing components
    CHECK(w.present && !w.complete() && w.x.present && !w.y.present && !w.z.present);
    Vec3Result none = ReadVec3Child(row, "Nope");                 // absent vector: nothing to write
    CHECK(!none.present && !none.complete());
    CHECK(ReadVec3(FindChild(row, "V")).complete());
    CHECK(!ReadVec3(nullptr).present);

    // bounded string copy: NUL forced at dst[size-1], so at most size-1 bytes
    CHECK(CopyText(row, "S", 4) == std::optional<std::string>("Hel"));
    CHECK(CopyText(row, "S", 6) == std::optional<std::string>("Hello"));
    CHECK(CopyText(row, "S", 100) == std::optional<std::string>("Hello"));
    CHECK(!CopyText(row, "F", 10).has_value() && !CopyText(row, "Missing", 10).has_value() && !CopyText(row, "S", 0).has_value());
}

void testFlagsAndEnums() {
    // 1.3: flag-list bitmask over ALL children named Flag, case-insensitive against the name list, OR bit i
    Document d = P("<Row><Flags><Flag>On Foot</Flag><Flag>IN VEHICLE</Flag><Flag>bogus</Flag><Flag/><Other>in water</Other></Flags>"
                   "<Empty/><Type>Sedan</Type><NoText></NoText><Dup>b</Dup></Row>");
    const Node* flags = FindChild(d.root(), "Flags");
    const std::vector<std::string_view> names = {"on foot", "in vehicle", "in water", "must have target"};
    CHECK(FlagMask(flags, names) == 0x3u);
    CHECK(FlagMask(flags, {"in water", "on foot"}) == 0x2u);        // bit index = position in THIS list
    CHECK(FlagMask(flags, {"nothing"}) == 0u);
    CHECK(FlagMask(FindChild(d.root(), "Empty"), names) == 0u);
    CHECK(FlagMask(nullptr, names) == 0u);
    CHECK(HasFlag(flags, "on foot") && HasFlag(flags, "ON FOOT") && HasFlag(flags, "In Vehicle") && !HasFlag(flags, "in water") && !HasFlag(flags, "bogusx"));
    CHECK(!HasFlag(nullptr, "x"));
    // a name past bit 31 cannot be represented and is ignored (no undefined shift)
    {
        std::string xml = "<R><Flags><Flag>n0</Flag><Flag>n40</Flag></Flags></R>";
        Document e = P(xml);
        std::vector<std::string_view> many;
        std::vector<std::string> storage;
        for (int i = 0; i < 41; ++i) storage.push_back("n" + std::to_string(i));
        for (const std::string& s : storage) many.push_back(s);
        CHECK(FlagMask(FindChild(e.root(), "Flags"), many) == 1u);
    }
    // enum index: position of the node's text in the list, case-insensitive; -1 for null node / no match / no text
    const std::vector<std::string_view> types = {"Compact", "Sedan", "Luxury"};
    CHECK(EnumIndex(FindChild(d.root(), "Type"), types) == 1);
    CHECK(EnumIndex(FindChild(d.root(), "Type"), {"sedan"}) == 0);
    CHECK(EnumIndex(FindChild(d.root(), "Type"), {"Compact", "SEDAN", "Sedan"}) == 1);   // first match wins
    CHECK(EnumIndex(FindChild(d.root(), "Type"), {"Truck", "Bus"}) == -1);
    CHECK(EnumIndex(FindChild(d.root(), "NoText"), types) == -1);
    CHECK(EnumIndex(nullptr, types) == -1);
    CHECK(EnumIndex(FindChild(d.root(), "Nope"), types) == -1);
    CHECK(EnumIndex(FindChild(d.root(), "Dup"), {"a", "b"}) == 1);
}

// ---------------------------------------------------------------------------
// 6. the row-key hash (traffic-ai 1.4)
// ---------------------------------------------------------------------------
void testHash() {
    const uint32_t* T = NameHashTable();
    // spec-save-format.md 6.1: the reflected CRC table, T[1] = 0x77073096, T[2] = 0xEE0E612C
    CHECK(T[0] == 0u && T[1] == 0x77073096u && T[2] == 0xEE0E612Cu && T[255] == 0x2D02EF8Du);
    // The routine is the standard reflected CRC-32 register update, so with seed 0xFFFFFFFF and a
    // final complement it must give the published CRC-32 check values.
    CHECK(~NameHashCaseSensitive("123456789", 0xFFFFFFFFu) == 0xCBF43926u);
    CHECK(~NameHashCaseSensitive("hello", 0xFFFFFFFFu) == 0x3610A686u);
    CHECK(~NameHashCaseSensitive("", 0xFFFFFFFFu) == 0u);
    // as the engine uses it: seed 0, NO final XOR
    CHECK(NameHash("") == 0u && NameHash("", 0xDEADBEEFu) == 0xDEADBEEFu);
    CHECK(NameHash("a") == T[('a' ^ 0) & 0xFF]);                  // one byte: crc = (0 >> 8) ^ T[byte]
    CHECK(~NameHashCaseSensitive("a", 0xFFFFFFFFu) == 0xE8B7BE43u);   // the published CRC-32 of "a"
    // lower-cases each byte (ASCII), and the case-sensitive sibling does not
    CHECK(NameHash("Cash_Bonus_Modifier") == NameHash("cash_bonus_modifier") && NameHash("ABC") == NameHash("abc"));
    CHECK(NameHashCaseSensitive("ABC") != NameHashCaseSensitive("abc") && NameHashCaseSensitive("abc") == NameHash("abc"));
    CHECK(NameHash("\xC3\xA9") == NameHashCaseSensitive("\xC3\xA9"));   // bytes >= 0x80 untouched
    // the seed is the starting register: chaining one string's result as the seed of the next
    // equals hashing the concatenation (traffic-ai 1.4: "a few chain the result of one string as the seed")
    CHECK(NameHash("ab" "cd") == NameHash("cd", NameHash("ab")));
    CHECK(NameHash("Name_A") != NameHash("Name_A", 1u));
    // "A NULL name hashes to 0" (weapons-combat 1.4)
    CHECK(NameHashOrZero(nullptr) == 0u && NameHashOrZero(nullptr, 77u) == 0u);
    const std::string s = "Hello";
    CHECK(NameHashOrZero(&s) == NameHash("hello"));
    // through the node API: hash of a row's Name text
    Document d = P("<T><Row><Name>DLC_Car_Mass</Name></Row><Row/></T>");
    CHECK(NameHashOrZero(ChildText(FindChild(d.root(), "Row"), "Name")) == NameHash("dlc_car_mass"));
    CHECK(NameHashOrZero(ChildText(NextSibling(d.root(), FindChild(d.root(), "Row"), "Row"), "Name")) == 0u);
}

} // namespace

int main() {
    try {
        testModel();
        testShapes();
        testText();
        testTolerance();
        testRejections();
        testIntegers();
        testBool();
        testFloat();
        testReaders();
        testFlagsAndEnums();
        testHash();
    } catch (const std::exception& e) {
        std::cerr << "unexpected exception: " << e.what() << "\n";
        return 1;
    }
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "sr3xtbl synthetic tests: all passed\n";
    return 0;
}

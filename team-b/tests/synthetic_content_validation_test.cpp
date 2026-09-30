// Unit tests for the .xtbl-family content validator (content_validation.h)
// in isolation, using raw byte content - no Container/archive involved.
//
// refineWithXtblValidation is a PERMANENT NO-OP (HANDOFF.md §9.78):
// Container::decompressEntry never produces DecodeStatus::OkUnconfirmedContent
// any more, so there is no live OkUnconfirmedContent -> ContentValidated/
// ContentValidationFailed transition anywhere in this project any more -
// see the tests below, which assert the no-op behavior directly.

#include <iostream>
#include <string>
#include <vector>

#include "vpp/content_validation.h"

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

std::vector<uint8_t> bytesOf(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace

int main() {
    // --- looksLikeXtblFilename ---
    CHECK(vpp::looksLikeXtblFilename("achievements.xtbl"));
    CHECK(vpp::looksLikeXtblFilename("some_cutscene.cte_xtbl")); // spec Sec4: distinct real extension
    CHECK(vpp::looksLikeXtblFilename("WEIRD_CASE.XTBL"));        // case-insensitive
    CHECK(vpp::looksLikeXtblFilename("xtbl"));                   // degenerate but matches the simple "ends with xtbl" rule
    CHECK(!vpp::looksLikeXtblFilename("texture.dds"));
    CHECK(!vpp::looksLikeXtblFilename("model.cmesh_pc"));
    CHECK(!vpp::looksLikeXtblFilename("ab")); // too short to even contain "xtbl"

    // --- validateXtblContent: well-formed cases ---
    {
        auto v = vpp::validateXtblContent(bytesOf("<root>\r\n\t<Table>\r\n\t\t<Foo>bar</Foo>\r\n\t"
                                                    "</Table>\r\n</root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }
    {
        // Self-closing tags must not be treated as needing a separate close.
        auto v = vpp::validateXtblContent(bytesOf("<root><Table><Foo/></Table></root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }
    {
        // Comments and CDATA must be skipped, not misparsed as tags.
        auto v = vpp::validateXtblContent(
            bytesOf("<root><!-- a comment with <fake> tags --><Table>"
                    "<![CDATA[ <also> fake ]]></Table></root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }
    {
        // A '>' inside a quoted attribute value must not end the tag early.
        auto v = vpp::validateXtblContent(
            bytesOf("<root><Table attr=\"a>b\"><Foo>x</Foo></Table></root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }
    {
        // Multiple sibling rows under Table, per spec Sec3.1.
        auto v = vpp::validateXtblContent(
            bytesOf("<root><Table><Achievement><Name>A</Name></Achievement>"
                    "<Achievement><Name>B</Name></Achievement></Table></root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }

    // --- validateXtblContent: the exact corruption signature spec Sec1
    // describes (truncated mid-tag, missing closing elements) ---
    {
        // Truncated mid-tag.
        auto v = vpp::validateXtblContent(bytesOf("<root><Table><Foo>bar</Fo"));
        CHECK(v.status == vpp::XtblValidation::NotWellFormedXml);
    }
    {
        // Missing closing elements (content just stops).
        auto v = vpp::validateXtblContent(bytesOf("<root><Table><Foo>bar</Foo>"));
        CHECK(v.status == vpp::XtblValidation::NotWellFormedXml);
    }
    {
        // Mismatched closing tag name.
        auto v = vpp::validateXtblContent(bytesOf("<root><Table><Foo>bar</Bar></Table></root>"));
        CHECK(v.status == vpp::XtblValidation::NotWellFormedXml);
    }
    {
        // Closing tag with nothing open at all.
        auto v = vpp::validateXtblContent(bytesOf("</root>"));
        CHECK(v.status == vpp::XtblValidation::NotWellFormedXml);
    }
    {
        // Unterminated '<' right at the end.
        auto v = vpp::validateXtblContent(bytesOf("<root><Table></Table></root><"));
        CHECK(v.status == vpp::XtblValidation::NotWellFormedXml);
    }

    // --- validateXtblContent: content that isn't <root><Table> shaped is
    // WellFormed as long as its tags are well-formed XML. The old
    // WrongRootShape check assumed every valid .xtbl has that shape, which
    // HANDOFF.md §9.79 found is false for 260/2,222 real files (blend_tree
    // files, state_machine files, action_nodes.xtbl, node_graph_files.xtbl,
    // and others), so the check was removed rather than merely left
    // dormant - see XtblValidation's comment in content_validation.h. ---
    {
        auto v = vpp::validateXtblContent(bytesOf("<notroot><Table></Table></notroot>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }
    {
        auto v = vpp::validateXtblContent(bytesOf("<root><NotTable></NotTable></root>"));
        CHECK(v.status == vpp::XtblValidation::WellFormed);
    }

    // --- refineWithXtblValidation: PERMANENT NO-OP (HANDOFF.md §9.78 -
    // decompressEntry never produces OkUnconfirmedContent any more), passes
    // EVERY status through completely unchanged, including
    // OkUnconfirmedContent itself. ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::Ok;
        in.data = bytesOf("this is not even XML at all");
        vpp::DecompressResult out = vpp::refineWithXtblValidation(in);
        CHECK(out.status == vpp::DecodeStatus::Ok); // untouched - Ok never gets refined
        CHECK(out.data == in.data);
    }
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::RecoveredSharedStream;
        in.data = bytesOf("also not XML");
        vpp::DecompressResult out = vpp::refineWithXtblValidation(in);
        CHECK(out.status == vpp::DecodeStatus::RecoveredSharedStream); // untouched
    }

    // --- refineWithXtblValidation: OkUnconfirmedContent + well-formed
    // content -> unchanged (no longer upgraded to ContentValidated). ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = bytesOf("<root><Table><Foo>bar</Foo></Table></root>");
        vpp::DecompressResult out = vpp::refineWithXtblValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent);
        CHECK(out.data == in.data);
    }

    // --- refineWithXtblValidation: OkUnconfirmedContent + corrupted
    // content -> STILL unchanged (no longer downgraded to
    // ContentValidationFailed or cleared - the check that used to do that
    // never runs any more). ---
    {
        vpp::DecompressResult in;
        in.status = vpp::DecodeStatus::OkUnconfirmedContent;
        in.data = bytesOf("<root><Table><Foo>bar</Fo"); // truncated mid-tag
        vpp::DecompressResult out = vpp::refineWithXtblValidation(in);
        CHECK(out.status == vpp::DecodeStatus::OkUnconfirmedContent);
        CHECK(out.data == in.data);
    }

    if (g_failures == 0) {
        std::cout << "All content-validation tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}

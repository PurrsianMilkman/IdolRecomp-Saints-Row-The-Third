// Minimal, purpose-built well-formedness scanner for .xtbl-family
// content. This is deliberately NOT a general-purpose validating XML
// parser. It does not handle: XML declarations/processing instructions
// beyond skipping over them, DOCTYPE internal subsets, custom entity
// declarations or references, XML namespaces, or attribute-value
// escaping beyond matching quote characters so a '>' inside a quoted
// attribute value doesn't prematurely end a tag.
//
// What it DOES check, matching exactly what spec-xtbl-format.md §1
// documents as the real, observed corruption signature for silently-
// corrupted non-first entries in mode-(a) containers ("truncated mid-tag,
// missing closing elements"):
//   - every '<' that starts a tag has a matching '>' before the content
//     ends (catches truncation mid-tag)
//   - opening and closing tags nest in proper LIFO order, with matching
//     names (catches a tag closed with the wrong name, or content that
//     jumps into the middle of a different tag structure)
//   - every opened tag is eventually closed, or is self-closing
//     ("<Foo/>") - nothing is left open when the content ends (catches
//     missing closing elements)
//
// Comments (<!-- ... -->) and CDATA sections (<![CDATA[ ... ]]>) are
// skipped over rather than parsed - the spec's samples didn't need them,
// but a legitimate file that happens to use one shouldn't be
// misclassified as corrupt on that account alone.
//
// HISTORY: this used to ALSO require the outermost element to be exactly
// <root>, directly containing a <Table> element as its first child
// (spec-xtbl-format.md §2's convention at the time), surfaced as
// XtblValidation::WrongRootShape. That check was removed: HANDOFF.md §9.79
// (real-data measurement, the sr3xtbl foundation) found 260 of 2,222 real
// shipped .xtbl-family files legitimately have no <Table> child at all
// (blend_tree files, state_machine files, action_nodes.xtbl,
// node_graph_files.xtbl, and others) - the check would have flagged all
// 260 as corrupt. It was already effectively dormant by then anyway: it
// only ever ran on a DecodeStatus::OkUnconfirmedContent result, and
// Container::decompressEntry stopped producing that status once the
// container-offset bug it was compensating for was fixed (HANDOFF.md
// §9.78) - see refineWithXtblValidation below.

#include "vpp/content_validation.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace vpp {

namespace {

bool isNameChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.' || c == ':';
}

// Finds the next occurrence of `needle` at or after `from`. Returns
// std::string::npos if not found.
size_t findSequence(const char* data, size_t size, size_t from, const char* needle) {
    size_t needleLen = std::strlen(needle);
    if (needleLen == 0 || from >= size || needleLen > size - from) {
        return std::string::npos;
    }
    for (size_t i = from; i + needleLen <= size; ++i) {
        if (std::memcmp(data + i, needle, needleLen) == 0) {
            return i;
        }
    }
    return std::string::npos;
}

bool matchesAt(const char* data, size_t size, size_t at, const char* needle) {
    size_t needleLen = std::strlen(needle);
    return at + needleLen <= size && std::memcmp(data + at, needle, needleLen) == 0;
}

// Finds the next '>' at or after `from` that isn't inside a single- or
// double-quoted attribute value. Returns std::string::npos if none found.
size_t findUnquotedGT(const char* data, size_t size, size_t from) {
    char quote = 0;
    for (size_t i = from; i < size; ++i) {
        char c = data[i];
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (c == '>') {
            return i;
        }
    }
    return std::string::npos;
}

} // namespace

bool looksLikeXtblFilename(const std::string& filename) {
    if (filename.size() < 4) {
        return false;
    }
    std::string tail = filename.substr(filename.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == "xtbl";
}

XtblValidationResult validateXtblContent(const std::vector<uint8_t>& content) {
    XtblValidationResult result;

    const char* data = reinterpret_cast<const char*>(content.data());
    size_t size = content.size();
    size_t pos = 0;

    std::vector<std::string> stack;
    bool sawAnyTag = false;

    while (pos < size) {
        if (data[pos] != '<') {
            ++pos;
            continue;
        }

        size_t tagStart = pos;
        ++pos; // consume '<'
        if (pos >= size) {
            result.status = XtblValidation::NotWellFormedXml;
            result.diagnostic = "content ends with an unterminated '<' at byte " +
                                 std::to_string(tagStart) + " (truncated mid-tag)";
            return result;
        }

        if (data[pos] == '?') {
            size_t end = findSequence(data, size, pos, "?>");
            if (end == std::string::npos) {
                result.status = XtblValidation::NotWellFormedXml;
                result.diagnostic = "processing instruction starting at byte " +
                                     std::to_string(tagStart) +
                                     " has no closing '?>' (truncated mid-tag)";
                return result;
            }
            pos = end + 2;
            continue;
        }

        if (data[pos] == '!') {
            if (matchesAt(data, size, pos, "!--")) {
                size_t end = findSequence(data, size, pos, "-->");
                if (end == std::string::npos) {
                    result.status = XtblValidation::NotWellFormedXml;
                    result.diagnostic = "comment starting at byte " + std::to_string(tagStart) +
                                         " has no closing '-->' (truncated mid-tag)";
                    return result;
                }
                pos = end + 3;
                continue;
            }
            if (matchesAt(data, size, pos, "![CDATA[")) {
                size_t end = findSequence(data, size, pos, "]]>");
                if (end == std::string::npos) {
                    result.status = XtblValidation::NotWellFormedXml;
                    result.diagnostic = "CDATA section starting at byte " +
                                         std::to_string(tagStart) +
                                         " has no closing ']]>' (truncated mid-tag)";
                    return result;
                }
                pos = end + 3;
                continue;
            }
            // DOCTYPE or similar - not expected per spec, but skip
            // gracefully to the next unquoted '>' rather than treating
            // unanticipated-but-legitimate syntax as corruption.
            size_t end = findUnquotedGT(data, size, pos);
            if (end == std::string::npos) {
                result.status = XtblValidation::NotWellFormedXml;
                result.diagnostic = "declaration starting at byte " + std::to_string(tagStart) +
                                     " has no closing '>' (truncated mid-tag)";
                return result;
            }
            pos = end + 1;
            continue;
        }

        bool isClosing = (data[pos] == '/');
        if (isClosing) {
            ++pos;
        }

        size_t nameStart = pos;
        while (pos < size && isNameChar(static_cast<unsigned char>(data[pos]))) {
            ++pos;
        }
        if (pos == nameStart) {
            result.status = XtblValidation::NotWellFormedXml;
            result.diagnostic = "tag with no name at byte " + std::to_string(tagStart);
            return result;
        }
        std::string tagName(data + nameStart, pos - nameStart);

        size_t gt = findUnquotedGT(data, size, pos);
        if (gt == std::string::npos) {
            result.status = XtblValidation::NotWellFormedXml;
            result.diagnostic = "tag '<" + tagName + ">' starting at byte " +
                                 std::to_string(tagStart) +
                                 " has no closing '>' before the content ends (truncated "
                                 "mid-tag)";
            return result;
        }
        bool selfClosing = (data[gt - 1] == '/');
        pos = gt + 1;

        if (isClosing) {
            if (stack.empty() || stack.back() != tagName) {
                result.status = XtblValidation::NotWellFormedXml;
                std::string openContext =
                    stack.empty() ? std::string("nothing is currently open")
                                  : ("the currently-open tag is '<" + stack.back() + ">'");
                result.diagnostic = "closing tag '</" + tagName + ">' at byte " +
                                     std::to_string(tagStart) + " does not match - " +
                                     openContext +
                                     " - mismatched or missing closing element(s)";
                return result;
            }
            stack.pop_back();
        } else {
            sawAnyTag = true;
            if (!selfClosing) {
                stack.push_back(std::move(tagName));
            }
        }
    }

    if (!stack.empty()) {
        result.status = XtblValidation::NotWellFormedXml;
        result.diagnostic = std::to_string(stack.size()) +
                             " tag(s) never closed by the end of the content (innermost open: "
                             "'<" +
                             stack.back() + ">') - missing closing element(s)";
        return result;
    }
    if (!sawAnyTag) {
        result.status = XtblValidation::NotWellFormedXml;
        result.diagnostic = "no element tags found in content at all";
        return result;
    }

    result.status = XtblValidation::WellFormed;
    return result;
}

DecompressResult refineWithXtblValidation(DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78), so this function is now a permanent no-op -
    // retained for API compatibility with its several unconditional
    // callers (vpp_dump.cpp, vpp_extract.cpp,
    // tools/validation/validate_container_decode.cpp). Its removed
    // structural check (see validateXtblContent's/XtblValidation's
    // comments above) assumed every valid .xtbl has a <root><Table>
    // shape, which HANDOFF.md §9.79 found is false for 260/2,222 real
    // files - that check would have produced false positives even on the
    // rare call where it used to run, so retiring it is a correctness fix
    // as well as a dead-code removal.
    return result;
}

} // namespace vpp

#include "sr3conversation/content_validation.h"

#include <algorithm>
#include <cctype>

#include "sr3conversation/conversation.h"

namespace sr3conversation {

bool looksLikeCtdgFilename(const std::string& filename) {
    // One confirmed extension (spec Sec2: no secondary/g-side extension
    // registered), so an exact match is better justified than a prefix
    // heuristic - same reasoning as sr3texture/sr3foliage.
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "ctdg_pc";
}

CtdgValidationResult validateCtdgContent(const std::vector<uint8_t>& content) {
    CtdgValidationResult result;

    try {
        Conversation::parse(ByteView(content.data(), content.size()));
    } catch (const FormatError& ex) {
        result.status = CtdgValidation::NotWellFormed;
        result.diagnostic = std::string("failed to parse as a valid .ctdg_pc: ") + ex.what();
        return result;
    }

    result.status = CtdgValidation::WellFormed;
    return result;
}

vpp::DecompressResult refineWithCtdgValidation(vpp::DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op.
    return result;
}

} // namespace sr3conversation

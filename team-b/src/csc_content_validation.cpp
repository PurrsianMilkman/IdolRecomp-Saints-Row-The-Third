#include "sr3cutscene/content_validation.h"

#include <algorithm>
#include <cctype>

#include "sr3cutscene/camera_script.h"

namespace sr3cutscene {

bool looksLikeCscFilename(const std::string& filename) {
    // One confirmed extension (spec Sec2: 0 .gsc_pc files, registration
    // declares no secondary extension).
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "csc_pc";
}

CscValidationResult validateCscContent(const std::vector<uint8_t>& content) {
    CscValidationResult result;

    try {
        CameraScript::parse(ByteView(content.data(), content.size()));
    } catch (const FormatError& ex) {
        result.status = CscValidation::NotWellFormed;
        result.diagnostic = std::string("failed to parse as a valid .csc_pc: ") + ex.what();
        return result;
    }

    result.status = CscValidation::WellFormed;
    return result;
}

vpp::DecompressResult refineWithCscValidation(vpp::DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op.
    return result;
}

} // namespace sr3cutscene

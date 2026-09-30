#include "sr3foliage/content_validation.h"

#include <algorithm>
#include <cctype>

#include "sr3foliage/foliage_mesh.h"

namespace sr3foliage {

bool looksLikeCfmeshFilename(const std::string& filename) {
    // Exactly one confirmed extension (spec-foliage-format.md Sec2: no
    // secondary/g-side extension registered, 0/19 .gfmesh_pc observed), so
    // an exact-match check is better justified here than a prefix
    // heuristic - same reasoning as sr3texture::looksLikeCpegFilename.
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "cfmesh_pc";
}

CfmeshValidationResult validateCfmeshContent(const std::vector<uint8_t>& content) {
    CfmeshValidationResult result;

    try {
        FoliageMesh::parse(ByteView(content.data(), content.size()));
    } catch (const FormatError& ex) {
        result.status = CfmeshValidation::NotWellFormed;
        result.diagnostic = std::string("failed to parse as a valid .cfmesh_pc: ") + ex.what();
        return result;
    }

    result.status = CfmeshValidation::WellFormed;
    return result;
}

vpp::DecompressResult refineWithCfmeshValidation(vpp::DecompressResult result) {
    // OkUnconfirmedContent is never produced by decompressEntry any more
    // (HANDOFF.md §9.78); this function is retained for API compatibility
    // but is now a permanent no-op.
    return result;
}

} // namespace sr3foliage

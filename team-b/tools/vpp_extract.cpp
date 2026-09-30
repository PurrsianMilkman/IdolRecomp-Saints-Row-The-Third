// Recursively extracts a .vpp_pc / .str2_pc archive to a real directory
// tree: each nested container becomes a subdirectory (named after its own
// directory-entry filename, e.g. "Running Man.str2_pc/"), and every leaf
// entry - raw content, or successfully decompressed/recovered content -
// is written as a real file inside the appropriate directory. A single
// entry failing to extract is reported and skipped; it does not abort
// extraction of the rest of the archive.
//
// Usage: vpp_extract <archive.vpp_pc|archive.str2_pc> <output_dir>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sr3conversation/content_validation.h"
#include "sr3cutscene/content_validation.h"
#include "sr3foliage/content_validation.h"
#include "sr3fxo/content_validation.h"
#include "sr3texture/content_validation.h"
#include "vpp/container.h"
#include "vpp/content_validation.h"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("could not open file: " + path);
    }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) {
        throw std::runtime_error("failed reading file: " + path);
    }
    return buf;
}

// Most filesystems (Windows in particular) forbid these characters in a
// path component; swap them for '_' so every entry name produces a
// writable file/directory name without altering the clean names actually
// observed in real archives so far.
std::string sanitizeComponent(const std::string& name) {
    static const std::string forbidden = "<>:\"/\\|?*";
    std::string out = name;
    for (char& c : out) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (forbidden.find(c) != std::string::npos || uc < 0x20) {
            c = '_';
        }
    }
    if (out.empty()) {
        out = "_";
    }
    return out;
}

void writeFile(const fs::path& path, const uint8_t* data, size_t size) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("could not create file: " + path.string());
    }
    if (size > 0) {
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
}

struct Stats {
    size_t containers = 0;
    size_t nestedOpened = 0;
    size_t rawLeafWritten = 0;
    size_t decompressedWritten = 0;
    size_t unconfirmedWritten = 0;  // written, but content not independently verified - see DecodeStatus::OkUnconfirmedContent
    size_t contentValidatedWritten = 0; // written AND passed a format-specific structural check - see DecodeStatus::ContentValidated
    size_t contentValidationFailures = 0; // NOT written - confirmed corrupt by a structural check, see DecodeStatus::ContentValidationFailed
    size_t recoveredWritten = 0;
    size_t decodeFailures = 0;
};

void extract(const vpp::Container& c, const fs::path& outDir, Stats& stats, int depth) {
    stats.containers++;
    fs::create_directories(outDir);
    std::string indent(static_cast<size_t>(depth) * 2, ' ');

    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        fs::path outPath = outDir / sanitizeComponent(e.name);

        try {
            if (e.payload.kind == vpp::PayloadKind::Raw) {
                bool openedAsContainer = false;
                try {
                    vpp::Container nested = c.openNested(i);
                    std::cout << indent << "[dir]  " << e.name << "/\n";
                    stats.nestedOpened++;
                    extract(nested, outPath, stats, depth + 1);
                    openedAsContainer = true;
                } catch (const vpp::FormatError&) {
                    // Not a nested container - genuine leaf raw content.
                    // Fall through and write it as a plain file below.
                }
                if (!openedAsContainer) {
                    vpp::ByteView raw = c.rawEntryBytes(i);
                    writeFile(outPath, raw.data(), raw.size());
                    std::cout << indent << "[raw]  " << e.name << " (" << raw.size()
                              << " bytes)\n";
                    stats.rawLeafWritten++;
                }
                continue;
            }

            // Compressed
            vpp::DecompressResult r = c.decompressEntry(i);
            if (vpp::looksLikeXtblFilename(e.name)) {
                // Corroborate or refute an OkUnconfirmedContent result
                // with a real structural check (spec-xtbl-format.md
                // Sec2-3) before deciding how to handle it below - a
                // no-op for every other status.
                r = vpp::refineWithXtblValidation(std::move(r));
            } else if (sr3fxo::looksLikeFxoFilename(e.name)) {
                // Same pattern for .fxo_pc (spec-fxo-format.md Sec3-4):
                // corroborate/refute via the D3D9 version/end-token
                // structural check before deciding how to handle it.
                r = sr3fxo::refineWithFxoValidation(std::move(r));
            } else if (sr3texture::looksLikeCpegFilename(e.name)) {
                // Same pattern for .cpeg_pc/.cvbm_pc (spec-texture-format.md
                // Sec2-3.1): corroborate/refute via the header/record/
                // filename-table exact-fit structural check.
                r = sr3texture::refineWithCpegValidation(std::move(r));
            } else if (sr3foliage::looksLikeCfmeshFilename(e.name)) {
                // Same pattern for .cfmesh_pc (spec-foliage-format.md
                // Sec3/Sec9): corroborate/refute via a full FoliageMesh::
                // parse() - see sr3foliage/content_validation.h for why a
                // parse() success is itself the structural check here.
                r = sr3foliage::refineWithCfmeshValidation(std::move(r));
            } else if (sr3conversation::looksLikeCtdgFilename(e.name)) {
                // Same pattern for .ctdg_pc (spec-conversation-format.md
                // Sec1/Sec3). The strongest check of the set: magic +
                // version + an exact-size identity accounting for every
                // byte, with no offsets anywhere to leave slack.
                r = sr3conversation::refineWithCtdgValidation(std::move(r));
            } else if (sr3cutscene::looksLikeCscFilename(e.name)) {
                // Same pattern for .csc_pc (spec-cutscene-camera-format.md
                // Sec1/Sec3) - note this format has NO magic, so the
                // evidence is the size identity plus a bounds walk over
                // every channel pointer.
                r = sr3cutscene::refineWithCscValidation(std::move(r));
            }
            switch (r.status) {
                case vpp::DecodeStatus::Ok:
                    writeFile(outPath, r.data.data(), r.data.size());
                    std::cout << indent << "[zlib] " << e.name << " (" << r.data.size()
                              << " bytes)\n";
                    stats.decompressedWritten++;
                    break;
                case vpp::DecodeStatus::OkUnconfirmedContent:
                    // Written - it may well be correct - but flagged
                    // distinctly per Team A's finding: this exact shape
                    // (non-first entry, multi-entry mode (a) container)
                    // has been observed to pass length/no-error checks
                    // while actually being corrupted, and this library
                    // cannot yet tell the two cases apart on its own. No
                    // format-specific validator was available/applicable
                    // for this entry (see ContentValidated/
                    // ContentValidationFailed below for when one is).
                    writeFile(outPath, r.data.data(), r.data.size());
                    std::cerr << indent << "[UNCONFIRMED] " << e.name << " (" << r.data.size()
                              << " bytes) - content not independently verified, see "
                                 "DecodeStatus::OkUnconfirmedContent - " << r.diagnostic << "\n";
                    stats.unconfirmedWritten++;
                    break;
                case vpp::DecodeStatus::ContentValidated:
                    // A format-specific structural check corroborated
                    // this - real evidence of correctness, not just an
                    // absence of errors. Which check ran (xtbl-family vs
                    // fxo) is named in r.diagnostic itself rather than
                    // hard-coded here, since this one case covers both.
                    writeFile(outPath, r.data.data(), r.data.size());
                    std::cout << indent << "[zlib+validated] " << e.name << " (" << r.data.size()
                              << " bytes) " << r.diagnostic << "\n";
                    stats.contentValidatedWritten++;
                    break;
                case vpp::DecodeStatus::ContentValidationFailed:
                    // CONFIRMED corrupt, not merely unconfirmed - do NOT
                    // write it to disk as if it were usable content.
                    std::cerr << indent << "[CONFIRMED CORRUPT] " << e.name
                              << ": NOT extracted - " << r.diagnostic << "\n";
                    stats.contentValidationFailures++;
                    break;
                case vpp::DecodeStatus::RecoveredSharedStream:
                case vpp::DecodeStatus::RecoveredSharedStreamLongChain:
                    writeFile(outPath, r.data.data(), r.data.size());
                    std::cout << indent << "[zlib*] " << e.name << " (" << r.data.size()
                              << " bytes, mode (b) recovery"
                              << (r.status == vpp::DecodeStatus::RecoveredSharedStreamLongChain
                                      ? ", LONG CHAIN - unverified shape, review recommended"
                                      : "")
                              << ")\n";
                    stats.recoveredWritten++;
                    break;
                case vpp::DecodeStatus::SizeMismatch:
                    // Real decoder output, just short of the confirmed
                    // size - still worth having on disk, clearly flagged.
                    writeFile(outPath, r.data.data(), r.data.size());
                    std::cerr << indent << "[WARN] " << e.name << ": size mismatch, wrote "
                              << r.data.size() << " bytes anyway - " << r.diagnostic << "\n";
                    stats.decodeFailures++;
                    break;
                case vpp::DecodeStatus::NoValidZlibHeaderAtOffset:
                case vpp::DecodeStatus::ZlibStreamError:
                    std::cerr << indent << "[FAIL] " << e.name << ": not extracted - "
                              << r.diagnostic << "\n";
                    stats.decodeFailures++;
                    break;
            }
        } catch (const std::exception& ex) {
            // One bad entry should not abort extraction of the rest of
            // the archive.
            std::cerr << indent << "[FAIL] " << e.name << ": " << ex.what() << "\n";
            stats.decodeFailures++;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: vpp_extract <archive.vpp_pc|archive.str2_pc> <output_dir>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));

        fs::path outDir(argv[2]);
        Stats stats;
        std::cout << "Extracting " << argv[1] << " -> " << outDir.string() << "\n";
        extract(root, outDir, stats, 0);

        std::cout << "\nDone. containers=" << stats.containers
                  << " nestedOpened=" << stats.nestedOpened
                  << " rawLeafWritten=" << stats.rawLeafWritten
                  << " decompressedWritten=" << stats.decompressedWritten
                  << " unconfirmedWritten=" << stats.unconfirmedWritten
                  << " contentValidatedWritten=" << stats.contentValidatedWritten
                  << " contentValidationFailures=" << stats.contentValidationFailures
                  << " recoveredWritten=" << stats.recoveredWritten
                  << " decodeFailures=" << stats.decodeFailures << "\n";
        if (stats.unconfirmedWritten > 0) {
            std::cout << stats.unconfirmedWritten
                      << " file(s) were written but are content-UNCONFIRMED (non-first "
                         "entry in a multi-entry mode (a) container, no format-specific "
                         "validator available) - see stderr above for which ones.\n";
        }
        if (stats.contentValidationFailures > 0) {
            std::cout << stats.contentValidationFailures
                      << " file(s) were NOT written - CONFIRMED corrupt by a structural "
                         "check (not just unconfirmed) - see stderr above for which "
                         "ones.\n";
        }

        return (stats.decodeFailures > 0 || stats.contentValidationFailures > 0) ? 2 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}

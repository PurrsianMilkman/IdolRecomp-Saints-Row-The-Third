// Small demo/debug CLI: opens a .vpp_pc or .str2_pc file and recursively
// walks it, printing the directory tree and attempting to decompress or
// descend into every entry.
//
// Usage: vpp_dump <archive.vpp_pc|archive.str2_pc>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3conversation/content_validation.h"
#include "sr3cutscene/content_validation.h"
#include "sr3foliage/content_validation.h"
#include "sr3fxo/content_validation.h"
#include "sr3texture/content_validation.h"
#include "vpp/container.h"
#include "vpp/content_validation.h"

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

void walk(const vpp::Container& c, int depth) {
    std::string indent(static_cast<size_t>(depth) * 2, ' ');

    for (size_t i = 0; i < c.entries().size(); ++i) {
        const vpp::Entry& e = c.entries()[i];
        std::cout << indent << "- " << e.name << " (hash=0x" << std::hex
                  << e.nameHash << std::dec << ")";

        switch (e.payload.kind) {
            case vpp::PayloadKind::Raw: {
                std::cout << " [raw @0x" << std::hex << e.payload.offset << std::dec
                          << ", len=" << e.payload.length << "]\n";
                try {
                    vpp::Container nested = c.openNested(i);
                    std::cout << indent << "  -> nested container, version="
                              << nested.header().version
                              << ", entries=" << nested.header().entryCount << "\n";
                    walk(nested, depth + 1);
                } catch (const std::exception& ex) {
                    std::cout << indent << "  (leaf content, not a nested container: "
                              << ex.what() << ")\n";
                }
                break;
            }
            case vpp::PayloadKind::Compressed: {
                std::cout << " [zlib @0x" << std::hex << e.payload.offset << std::dec
                          << ", compressedLen=" << e.payload.length
                          << ", expectedDecompressedLen=" << e.payload.decompressedLength
                          << "]\n";
                vpp::DecompressResult r = c.decompressEntry(i);
                if (vpp::looksLikeXtblFilename(e.name)) {
                    r = vpp::refineWithXtblValidation(std::move(r));
                } else if (sr3fxo::looksLikeFxoFilename(e.name)) {
                    r = sr3fxo::refineWithFxoValidation(std::move(r));
                } else if (sr3texture::looksLikeCpegFilename(e.name)) {
                    r = sr3texture::refineWithCpegValidation(std::move(r));
                } else if (sr3foliage::looksLikeCfmeshFilename(e.name)) {
                    r = sr3foliage::refineWithCfmeshValidation(std::move(r));
                } else if (sr3conversation::looksLikeCtdgFilename(e.name)) {
                    r = sr3conversation::refineWithCtdgValidation(std::move(r));
                } else if (sr3cutscene::looksLikeCscFilename(e.name)) {
                    r = sr3cutscene::refineWithCscValidation(std::move(r));
                }
                switch (r.status) {
                    case vpp::DecodeStatus::Ok:
                        std::cout << indent << "  -> decompressed " << r.data.size()
                                  << " bytes\n";
                        break;
                    case vpp::DecodeStatus::OkUnconfirmedContent:
                        std::cout << indent << "  -> decompressed " << r.data.size()
                                  << " bytes [UNCONFIRMED - non-first entry in multi-entry "
                                     "mode (a) container, content not independently "
                                     "verified, see Team A finding] " << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::ContentValidated:
                        // Which check ran (xtbl-family, fxo, cpeg/cvbm, or
                        // cfmesh) is named in r.diagnostic itself, since this
                        // one status covers all four.
                        std::cout << indent << "  -> decompressed " << r.data.size()
                                  << " bytes [CONTENT-VALIDATED] " << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::ContentValidationFailed:
                        std::cout << indent << "  [CONFIRMED CORRUPT] " << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::RecoveredSharedStream:
                        std::cout << indent << "  -> decompressed " << r.data.size()
                                  << " bytes [via mode (b) heuristic recovery] " << r.diagnostic
                                  << "\n";
                        break;
                    case vpp::DecodeStatus::RecoveredSharedStreamLongChain:
                        std::cout << indent << "  -> decompressed " << r.data.size()
                                  << " bytes [via mode (b) heuristic recovery, LONG CHAIN] "
                                  << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::SizeMismatch:
                        std::cout << indent << "  [size mismatch] " << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::NoValidZlibHeaderAtOffset:
                        std::cout << indent << "  [needs further RE] " << r.diagnostic << "\n";
                        break;
                    case vpp::DecodeStatus::ZlibStreamError:
                        std::cout << indent << "  [decode error] " << r.diagnostic << "\n";
                        break;
                }
                break;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: vpp_dump <archive.vpp_pc|archive.str2_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        vpp::Container root(vpp::ByteView(bytes.data(), bytes.size()));
        std::cout << argv[1] << ": magic OK, version=" << root.header().version
                  << ", entries=" << root.header().entryCount
                  << ", totalSize(header)=" << root.header().totalSize
                  << ", actualBytes=" << bytes.size() << "\n";
        walk(root, 0);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}

// Small inspection CLI for .ctdg_pc mission conversations
// (spec-conversation-format.md). Shows the header, the embedded authoring
// source name, the distinct speaker set the engine's consumer would build,
// and every dialogue turn in file order (which is conversation order).
//
// Usage: ctdg_dump <file.ctdg_pc>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "sr3conversation/conversation.h"

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ctdg_dump <file.ctdg_pc>\n";
        return 1;
    }

    try {
        std::vector<uint8_t> bytes = readFile(argv[1]);
        sr3conversation::ByteView content(bytes.data(), bytes.size());
        sr3conversation::Conversation c = sr3conversation::Conversation::parse(content);

        std::cout << argv[1] << ": " << bytes.size() << " bytes\n";
        std::cout << "version: " << c.version() << "\n";
        std::cout << "source name: \"" << c.sourceName() << "\""
                  << (c.sourceNameTruncated() ? "  [TRUNCATED - filled all 32 bytes with no "
                                                "terminator; a label, not a key]"
                                              : "")
                  << "\n";

        std::vector<uint32_t> speakers = c.distinctSpeakerIds();
        std::cout << "distinct speakers: " << speakers.size()
                  << " (the set the engine validates once before playback)\n";
        for (uint32_t s : speakers) {
            std::cout << "  - 0x" << std::hex << std::setw(8) << std::setfill('0') << s << std::dec
                      << std::setfill(' ') << "  [rotate-6/XOR hash; pre-image name is OPEN, "
                         "spec Sec6.1]\n";
        }

        std::cout << "dialogue turns: " << c.turns().size() << " (file order IS conversation order)\n";
        for (size_t i = 0; i < c.turns().size(); ++i) {
            const auto& t = c.turns()[i];
            std::cout << "  " << std::setw(2) << i << ": speaker=0x" << std::hex << std::setw(8)
                      << std::setfill('0') << t.speakerId << " line=0x" << std::setw(8)
                      << t.lineId << std::dec << std::setfill(' ')
                      << "  field_0x08=" << t.field_0x08_raw;
            if (t.field_0x08_raw != 0) {
                std::cout << " (as int32: " << t.field_0x08_asInt32()
                          << " -- meaning is HYPOTHESIS, spec Sec4)";
            }
            std::cout << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}

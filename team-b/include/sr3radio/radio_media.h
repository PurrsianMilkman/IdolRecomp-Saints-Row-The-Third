// sr3radio media: radio cue -> Wwise media record in sounds_common.vpp_pc, plus Vorbis decode.
// Source: spec-audio-runtime.md Sec14.6.3 "Item -> Wwise" / "Cue -> media file" (FINAL) and 14.6.4 item 3.
//   cue FNV-1 -> switch container (per content type, in radio_misc.bnk_pc)
//       -> package whose switch value == cue id -> kind-2 Sound -> source media id
//       -> record with that `tag` in a radio_*_media.bnk_pc (VWSBPC) -> RIFF/Wwise-Vorbis.
// The archive is memory-mapped; nothing is decompressed or decoded until one item is requested, so
// memory per call is one song of PCM (never a whole bank).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sr3audioruntime/real_vorbis_decode.h"
#include "sr3radio/radio_content.h"
#include "sr3radio/radio_sim.h"
#include "vppcache/file_blob.h"

namespace vpp { class Container; }

namespace sr3radio {

struct MediaLoc {
    uint32_t soundId = 0;       // kind-2 Sound HIRC id
    uint32_t mediaId = 0;       // == record tag
    uint32_t entryIndex = 0;    // archive entry of the *_media.bnk_pc
    uint32_t offset = 0;        // record offset inside that entry
    uint32_t size = 0;
};

// Switch container id per content type (spec 14.6.3 table); 0 = none shipped.
uint32_t switchContainerFor(ContentType t);

class RadioMedia {
public:
    RadioMedia();
    ~RadioMedia();
    // Opens `<dir>/sounds_common.vpp_pc` style archive. False + err on failure.
    bool open(const std::string& soundsCommonPath, std::string& err);
    bool isOpen() const { return container_ != nullptr; }
    bool resolve(uint32_t cueId, ContentType type, MediaLoc& out, std::string& why) const;
    // Decodes the item's whole RIFF stream (one song at most ~40 MB of PCM). Thread-safe (const).
    bool decode(const MediaLoc& loc, sr3audioruntime::DecodeAttemptResult& out, std::string& why) const;
    size_t mediaRecordCount() const { return tagIndex_.size(); }

private:
    struct Rec { uint32_t entry, offset, size; };
    vppcache::FileBlob blob_;
    std::unique_ptr<vpp::Container> container_;
    std::vector<uint8_t> miscBankStorage_;
    const uint8_t* miscBankData_ = nullptr; size_t miscBankSize_ = 0;
    struct Hirc;
    std::unique_ptr<Hirc> hirc_;
    std::unordered_map<uint32_t, Rec> tagIndex_;
    std::map<uint32_t, std::pair<const uint8_t*, size_t>> entryBytes_;  // entry index -> raw bytes
};

// misc_tables.vpp_pc opened (memory-mapped) as an XtblProvider source; entry names matched
// case-insensitively (retail has "Kabron_radio.xtbl" vs "kabron" style differences).
class XtblArchive {
public:
    XtblArchive();
    ~XtblArchive();
    bool open(const std::string& path, std::string& err);
    bool get(const std::string& name, std::vector<uint8_t>& bytes) const;
    XtblProvider provider() const { return [this](const std::string& n, std::vector<uint8_t>& b) { return get(n, b); }; }
private:
    vppcache::FileBlob blob_;
    std::unique_ptr<vpp::Container> container_;
};

}  // namespace sr3radio

// sr3radio media resolution/decode. See include/sr3radio/radio_media.h.
#include "sr3radio/radio_media.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>

#include "sr3audio/hirc.h"
#include "sr3audio/media_bank.h"
#include "sr3audioruntime/real_vorbis_decode.h"
#include "vpp/container.h"

namespace sr3radio {

uint32_t switchContainerFor(ContentType t) {
    switch (t) {
        case ContentType::Song: return 0x2C2AE6FBu;
        case ContentType::Commercial: return 0x0E3DD438u;
        case ContentType::Sweeper: return 0x3E02AE54u;
        case ContentType::Intro: return 0x019B5EC6u;
        case ContentType::Outro: return 0x2200F032u;
        case ContentType::NewsSong: return 0x0E3D68D2u;
        default: return 0;   // call-in / Police-FBI: no event in any shipped bank; sing-along not postable
    }
}

struct RadioMedia::Hirc {
    std::vector<sr3audio::HircItem> items;
    std::unordered_map<uint32_t, size_t> byId;
};

RadioMedia::RadioMedia() = default;
RadioMedia::~RadioMedia() = default;

namespace {
bool endsWith(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
}  // namespace

bool RadioMedia::open(const std::string& path, std::string& err) {
    try {
        blob_ = vppcache::FileBlob::load(path);
        container_ = std::make_unique<vpp::Container>(blob_.view());
    } catch (const std::exception& ex) { err = std::string("open ") + path + ": " + ex.what(); container_.reset(); return false; }

    const auto& entries = container_->entries();
    std::vector<uint32_t> mediaEntries;
    size_t miscIdx = (size_t)-1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == "radio_misc.bnk_pc") miscIdx = i;
        else if (entries[i].name.rfind("radio_", 0) == 0 && endsWith(entries[i].name, "_media.bnk_pc")) mediaEntries.push_back((uint32_t)i);
    }
    if (miscIdx == (size_t)-1) { err = "radio_misc.bnk_pc not found in archive"; container_.reset(); return false; }

    auto bytesOf = [&](size_t idx, std::vector<uint8_t>* store, const uint8_t*& data, size_t& size) {
        if (entries[idx].payload.kind == vpp::PayloadKind::Raw) {
            auto v = container_->rawEntryBytes(idx); data = v.data(); size = v.size();
        } else {
            auto r = container_->decompressEntry(idx); *store = std::move(r.data); data = store->data(); size = store->size();
        }
    };
    try {
        bytesOf(miscIdx, &miscBankStorage_, miscBankData_, miscBankSize_);
        auto chunk = sr3audio::findHircChunk(vpp::ByteView(miscBankData_, miscBankSize_));
        if (!chunk) { err = "radio_misc.bnk_pc has no HIRC chunk"; container_.reset(); return false; }
        hirc_ = std::make_unique<Hirc>();
        hirc_->items = sr3audio::walkHircItems(*chunk);
        for (size_t i = 0; i < hirc_->items.size(); ++i) hirc_->byId.emplace(hirc_->items[i].id, i);

        for (uint32_t mi : mediaEntries) {
            if (entries[mi].payload.kind != vpp::PayloadKind::Raw) continue;  // all shipped radio media are raw (mmap, zero-copy)
            auto v = container_->rawEntryBytes(mi);
            sr3audio::MediaBank mb = sr3audio::MediaBank::parse(v);
            auto recs = mb.walk(entries[mi].payload.decompressedLength);
            entryBytes_[mi] = {v.data(), v.size()};
            for (const auto& r : recs) tagIndex_.emplace(r.tag, Rec{mi, r.offset, r.size});
        }
    } catch (const std::exception& ex) { err = std::string("radio bank index: ") + ex.what(); container_.reset(); return false; }
    return true;
}

bool RadioMedia::resolve(uint32_t cueId, ContentType type, MediaLoc& out, std::string& why) const {
    why.clear();
    if (!container_ || !hirc_) { why = "not open"; return false; }
    const uint32_t cid = switchContainerFor(type);
    if (!cid) { why = "no switch container for this content type"; return false; }
    auto cit = hirc_->byId.find(cid);
    if (cit == hirc_->byId.end()) { why = "switch container missing from radio_misc.bnk_pc"; return false; }
    const sr3audio::HircItem& sc = hirc_->items[cit->second];
    const uint8_t* b = sc.body.data();
    const size_t n = sc.body.size();
    // Layout-agnostic package scan (byte-level, validated against the spec 14.6.4 item 3 vectors):
    // a package is {switch value == cue id, u32 count, count x u32 child ids}; the first child that is a
    // kind-2 Sound is the track. (The cue also appears once as the container's default switch value,
    // which is not followed by a count + Sound id and is skipped by the Sound check.)
    for (size_t o = 0; o + 12 <= n; ++o) {
        if (rd32(b + o) != cueId) continue;
        const uint32_t count = rd32(b + o + 4);
        if (count == 0 || count > 32 || o + 8 + 4ull * count > n) continue;
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t child = rd32(b + o + 8 + 4ull * k);
            auto sit = hirc_->byId.find(child);
            if (sit == hirc_->byId.end()) continue;
            const sr3audio::HircItem& snd = hirc_->items[sit->second];
            if (snd.kind != sr3audio::kHircKindSound) continue;
            sr3audio::SoundSourceDescriptor d;
            try { d = sr3audio::parseSoundHircItem(snd); } catch (const std::exception&) { continue; }
            auto tit = tagIndex_.find(d.sourceMediaId);
            if (tit == tagIndex_.end()) { char buf[96]; std::snprintf(buf, sizeof buf, "sound 0x%08X media id 0x%08X not in any radio media bank", child, d.sourceMediaId); why = buf; continue; }
            out.soundId = child; out.mediaId = d.sourceMediaId;
            out.entryIndex = tit->second.entry; out.offset = tit->second.offset; out.size = tit->second.size;
            return true;
        }
    }
    if (why.empty()) why = "cue not found in switch container";
    return false;
}

bool RadioMedia::decode(const MediaLoc& loc, sr3audioruntime::DecodeAttemptResult& out, std::string& why) const {
    auto it = entryBytes_.find(loc.entryIndex);
    if (it == entryBytes_.end() || (size_t)loc.offset + loc.size > it->second.second) { why = "bad media location"; return false; }
    out = sr3audioruntime::attemptWwiseVorbisDecode(it->second.first + loc.offset, loc.size);
    if (!out.ok) { why = "decode failed: " + out.diagnostic; return false; }
    return true;
}

XtblArchive::XtblArchive() = default;
XtblArchive::~XtblArchive() = default;

bool XtblArchive::open(const std::string& path, std::string& err) {
    try {
        blob_ = vppcache::FileBlob::load(path);
        container_ = std::make_unique<vpp::Container>(blob_.view());
    } catch (const std::exception& ex) { err = std::string("open ") + path + ": " + ex.what(); container_.reset(); return false; }
    return true;
}

bool XtblArchive::get(const std::string& name, std::vector<uint8_t>& bytes) const {
    if (!container_) return false;
    auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    const std::string want = lower(name);
    const auto& entries = container_->entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (lower(entries[i].name) != want) continue;
        try {
            if (entries[i].payload.kind == vpp::PayloadKind::Raw) {
                auto v = container_->rawEntryBytes(i); bytes.assign(v.data(), v.data() + v.size());
            } else bytes = container_->decompressEntry(i).data;
        } catch (const std::exception&) { return false; }
        return true;
    }
    return false;
}

}  // namespace sr3radio

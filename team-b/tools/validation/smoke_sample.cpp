// Quick smoke test for sr3anim::sampleClipAtTime() against a real clip,
// before wiring it into the renderer. Prints a handful of bones' sampled
// rotation/translation at a few times so the numbers can be eyeballed for
// sanity (unit quaternions, small/plausible translation deltas) ahead of
// the full sr3_viewer animpose command.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "sr3anim/animation.h"
#include "sr3anim/payload.h"
#include "sr3anim/sample.h"
#include "vpp/container.h"

namespace {
std::vector<uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    std::streamsize n = f.tellg();
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (n > 0) { f.seekg(0); f.read(reinterpret_cast<char*>(b.data()), n); }
    return b;
}
bool endsWith(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
bool entryBytes(const vpp::Container& c, size_t i, std::vector<uint8_t>& out) {
    if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
        vpp::ByteView r = c.rawEntryBytes(i);
        out.assign(r.data(), r.data() + r.size());
        return true;
    }
    auto r = c.decompressEntry(i);
    if (r.status != vpp::DecodeStatus::Ok && r.status != vpp::DecodeStatus::OkUnconfirmedContent &&
        r.status != vpp::DecodeStatus::ContentValidated &&
        r.status != vpp::DecodeStatus::RecoveredSharedStream &&
        r.status != vpp::DecodeStatus::RecoveredSharedStreamLongChain) return false;
    out = std::move(r.data);
    return true;
}
bool findClip(const vpp::Container& c, const std::string& name, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].name == name) return entryBytes(c, i, out);
    }
    for (size_t i = 0; i < c.entries().size(); ++i) {
        if (c.entries()[i].payload.kind == vpp::PayloadKind::Raw) {
            try {
                if (findClip(c.openNested(i), name, out)) return true;
            } catch (const std::exception&) {}
        }
    }
    return false;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: smoke_sample <preload_anim.vpp_pc> <clip.anim_pc>\n");
        return 1;
    }
    std::vector<uint8_t> archive = readFile(argv[1]);
    std::vector<uint8_t> clipBytes;
    {
        vpp::Container c(vpp::ByteView(archive.data(), archive.size()));
        if (!findClip(c, argv[2], clipBytes)) {
            std::fprintf(stderr, "clip not found: %s\n", argv[2]);
            return 1;
        }
    }
    vpp::ByteView bytes(clipBytes.data(), clipBytes.size());
    sr3anim::Animation a = sr3anim::Animation::parse(bytes);
    sr3anim::Payload p = sr3anim::Payload::walk(bytes, a);
    std::printf("clip: %s  duration=%u  tracks=%zu  walkComplete=%s landedOnEnd=%s\n", argv[2],
                a.durationTotal(), p.tracks().size(), p.walkComplete() ? "yes" : "no",
                p.landedOnDeclaredEnd() ? "yes" : "no");

    const size_t boneCount = 69; // brad.rig_pc has 68 bones; pad by one, harmless
    float duration = static_cast<float>(a.durationTotal());
    float times[4] = {0.0f, duration / 3.0f, 2.0f * duration / 3.0f, duration};
    for (float t : times) {
        std::vector<sr3anim::BoneSample> samples = sr3anim::sampleClipAtTime(bytes, a, p, boneCount, t);
        std::printf("\n-- t=%.2f --\n", t);
        for (size_t b = 44; b < boneCount && b < 50; ++b) {
            const auto& s = samples[b];
            float qn = std::sqrt(s.rotation.x * s.rotation.x + s.rotation.y * s.rotation.y +
                                  s.rotation.z * s.rotation.z + s.rotation.w * s.rotation.w);
            std::printf("  bone %2zu: rotTrack=%s quat=(%.4f,%.4f,%.4f,%.4f) |q|=%.5f  "
                        "transTrack=%s delta=(%.4f,%.4f,%.4f)\n",
                        b, s.hasRotationTrack ? "y" : "n", s.rotation.x, s.rotation.y, s.rotation.z,
                        s.rotation.w, qn, s.hasTranslationTrack ? "y" : "n", s.translationDelta[0],
                        s.translationDelta[1], s.translationDelta[2]);
        }
    }
    return 0;
}

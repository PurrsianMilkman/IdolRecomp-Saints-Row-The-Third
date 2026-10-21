#include "sr3zone/zone_collision_mesh.h"

#include <cmath>
#include <cstring>

namespace sr3zone {

namespace {
uint32_t rd32(vpp::ByteView b, size_t at) { return b.readU32LE(at); }
float rdf(vpp::ByteView b, size_t at) {
    const uint32_t u = b.readU32LE(at);
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
size_t alignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

// Tries to read a validated mesh whose `u32 n2` sits at `c` with n1 == n2 == n and whose blob length word is at `q`.
bool tryMesh(vpp::ByteView czn, size_t n, size_t c, size_t q, const float bmin[3], const float bmax[3],
             uint32_t blobLen, CollisionMesh& m) {
    if (c < 4 + 12 * n) return false;
    if (rd32(czn, c) != n) return false;
    if (alignUp(c + 4 + 2 * n, 16) != q) return false;
    const size_t n1At = c - 4 - 12 * n;
    if (rd32(czn, n1At) != n) return false;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < n; ++i)
        for (size_t k = 0; k < 3; ++k) {
            const float f = rdf(czn, n1At + 4 + i * 12 + 4 * k);
            if (!(f == f)) return false;
            if (f < lo[k]) lo[k] = f;
            if (f > hi[k]) hi[k] = f;
        }
    for (size_t k = 0; k < 3; ++k)
        if (std::fabs(lo[k] - bmin[k]) > 0.02f || std::fabs(hi[k] - bmax[k]) > 0.02f) return false;
    m.v.resize(n);
    for (size_t i = 0; i < n; ++i)
        for (size_t k = 0; k < 3; ++k) m.v[i][k] = rdf(czn, n1At + 4 + i * 12 + 4 * k);
    m.s.resize(n);
    for (size_t i = 0; i < n; ++i) m.s[i] = czn.readU16LE(c + 4 + 2 * i);
    for (size_t k = 0; k < 3; ++k) { m.bmin[k] = bmin[k]; m.bmax[k] = bmax[k]; }
    m.n1Offset = n1At;
    m.blobLength = blobLen;
    m.blobOffset = q + 16;
    EnumerateMoppKeys(czn, m.blobOffset, m.keys);
    return true;
}
}  // namespace

bool EnumerateMoppKeys(vpp::ByteView czn, size_t blob, std::vector<uint32_t>& keys) {
    keys.clear();
    if (blob + 0x20 > czn.size()) return false;
    const uint32_t ver = rd32(czn, blob + 4);
    if (ver < 300) return false;
    const size_t size = rd32(czn, blob + 0x18);
    const size_t base = ver == 300 ? blob + 0x1c : blob + 0x20;
    if (size == 0 || base + size > czn.size()) return false;
    auto B = [&](size_t pc, size_t i) -> uint32_t { return czn.at(base + pc + i); };
    struct Item { size_t pc; uint32_t off; };
    std::vector<Item> stack;
    stack.push_back({0, 0});
    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        size_t pc = it.pc;
        uint32_t off = it.off;
        for (;;) {
            if (pc >= size) return false;
            const uint32_t op = B(pc, 0);
            auto need = [&](size_t len) { return pc + len <= size; };
            if (op == 0x00) break;
            if (op >= 0x01 && op <= 0x04) { if (!need(4)) return false; pc += 4; }
            else if (op == 0x05) { if (!need(2)) return false; pc += 2 + B(pc, 1); }
            else if (op == 0x06) { if (!need(3)) return false; pc += 3 + ((B(pc, 1) << 8) | B(pc, 2)); }
            else if (op == 0x07) { if (!need(4)) return false; pc += 4 + ((B(pc, 1) << 16) | (B(pc, 2) << 8) | B(pc, 3)); }
            else if (op == 0x09) { if (!need(2)) return false; off += B(pc, 1); pc += 2; }
            else if (op == 0x0a) { if (!need(3)) return false; off += (B(pc, 1) << 8) | B(pc, 2); pc += 3; }
            else if (op == 0x0b) { if (!need(5)) return false; off = (B(pc, 1) << 24) | (B(pc, 2) << 16) | (B(pc, 3) << 8) | B(pc, 4); pc += 5; }
            else if (op >= 0x10 && op <= 0x1c) { if (!need(4)) return false; stack.push_back({pc + 4 + B(pc, 3), off}); pc += 4; }
            else if (op >= 0x20 && op <= 0x22) { if (!need(3)) return false; stack.push_back({pc + 3 + B(pc, 2), off}); pc += 3; }
            else if (op >= 0x23 && op <= 0x25) {
                if (!need(7)) return false;
                stack.push_back({pc + 7 + ((B(pc, 5) << 8) | B(pc, 6)), off});
                pc = pc + 7 + ((B(pc, 3) << 8) | B(pc, 4));
            }
            else if (op >= 0x26 && op <= 0x28) { if (!need(3)) return false; pc += 3; }
            else if (op >= 0x29 && op <= 0x2b) { if (!need(7)) return false; pc += 7; }
            else if (op >= 0x30 && op <= 0x4f) { keys.push_back((op - 0x30) + off); break; }
            else if (op == 0x50) { if (!need(2)) return false; keys.push_back(B(pc, 1) + off); break; }
            else if (op == 0x51) { if (!need(3)) return false; keys.push_back(((B(pc, 1) << 8) | B(pc, 2)) + off); break; }
            else if (op == 0x52) { if (!need(4)) return false; keys.push_back(((B(pc, 1) << 16) | (B(pc, 2) << 8) | B(pc, 3)) + off); break; }
            else if (op == 0x53) { if (!need(5)) return false; keys.push_back(((B(pc, 1) << 24) | (B(pc, 2) << 16) | (B(pc, 3) << 8) | B(pc, 4)) + off); break; }
            else if (op >= 0x60 && op <= 0x63) { if (!need(2)) return false; pc += 2; }
            else if (op >= 0x64 && op <= 0x67) { if (!need(3)) return false; pc += 3; }
            else if (op >= 0x68 && op <= 0x6b) { if (!need(5)) return false; pc += 5; }
            else return false;
        }
    }
    return true;
}

std::vector<std::array<float, 3>> BuildKeyTriangles(const CollisionMesh& mesh) {
    std::vector<std::array<float, 3>> tris;
    tris.reserve(mesh.keys.size() * 3);
    for (uint32_t key : mesh.keys) {
        const ShapeKeyTriangle t = DecodeShapeKey(key);
        if (t.v0 >= mesh.v.size() || t.v1 >= mesh.v.size() || t.v2 >= mesh.v.size()) continue;
        const bool flip = (key & 0x80000000u) != 0;
        tris.push_back(mesh.v[t.v0]);
        tris.push_back(mesh.v[flip ? t.v2 : t.v1]);
        tris.push_back(mesh.v[flip ? t.v1 : t.v2]);
    }
    return tris;
}

bool LocateCollisionMeshes(vpp::ByteView czn, std::vector<CollisionMesh>& out) {
    out.clear();
    const size_t size = czn.size();
    // CHOSEN locator: the blob starts at a 16-aligned offset p with the MOPP magic, `u32 blobLen` at p - 16.
    for (size_t p = 16; p + 4 <= size; p += 16) {
        if (rd32(czn, p) != kMoppMagic) continue;
        const size_t q = p - 16;
        const uint32_t blobLen = rd32(czn, q);
        if (blobLen < 8 || blobLen > size) continue;
        const size_t aabbAt = alignUp(p + blobLen, 4);
        if (aabbAt + 24 > size) continue;
        float bmin[3], bmax[3];
        for (size_t k = 0; k < 3; ++k) {
            bmin[k] = rdf(czn, aabbAt + 4 * k);
            bmax[k] = rdf(czn, aabbAt + 12 + 4 * k);
        }
        // Solve n (= n1 = n2): s[] ends at c + 4 + 2n and align16 of that is q.
        for (size_t n = 3; n * 14 + 24 <= q; ++n) {
            if (q < 4 + 2 * n) break;
            const size_t top = (q - 4 - 2 * n) & ~size_t(3);
            bool done = false;
            for (size_t j = 0; j < 5 && top >= 4 * j; ++j) {
                CollisionMesh m;
                if (tryMesh(czn, n, top - 4 * j, q, bmin, bmax, blobLen, m)) {
                    out.push_back(std::move(m));
                    done = true;
                    break;
                }
            }
            if (done) break;
        }
    }
    return !out.empty();
}

std::vector<std::array<float, 3>> BuildStripStandInTriangles(const CollisionMesh& mesh) {
    std::vector<std::array<float, 3>> tris;
    const size_t n = mesh.v.size();
    if (n < 3) return tris;
    for (size_t v = 0; v + 2 < n && v < mesh.s.size(); ++v) {
        if (mesh.s[v] == kNoTriangleSurfaceId) continue;
        tris.push_back(mesh.v[v]);
        tris.push_back(mesh.v[v + 1]);
        tris.push_back(mesh.v[v + 2]);
    }
    return tris;
}

}  // namespace sr3zone

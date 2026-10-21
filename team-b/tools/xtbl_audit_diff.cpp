// Reads the VAL lines written by the impact-audit comparison (tab separated: VAL, file, before-line, after-line, where a
// line is "E path T='text' F=<float bits> I=<int>") and buckets the float-reader changes by magnitude.
// Usage: xtbl_audit_diff <val_changes.txt> <out_big.txt>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

static bool field(const std::string& line, uint32_t& bits) {
    const size_t p = line.rfind(" F=");
    if (p == std::string::npos) return false;
    bits = static_cast<uint32_t>(std::strtoul(line.c_str() + p + 3, nullptr, 16));
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream in(argv[1]);
    FILE* big = std::fopen(argv[2], "wb");
    std::string line;
    std::map<std::string, long long> bucket;
    std::map<std::string, long long> bigByLeaf;
    long long total = 0;
    while (std::getline(in, line)) {
        std::vector<std::string> c;
        size_t s = 0;
        for (int i = 0; i < 3; ++i) { size_t t = line.find('\t', s); c.push_back(line.substr(s, t - s)); s = t + 1; }
        c.push_back(line.substr(s));
        uint32_t ba, bb;
        if (!field(c[2], ba) || !field(c[3], bb)) { ++bucket["presence"]; continue; }
        float fa, fb;
        std::memcpy(&fa, &ba, 4);
        std::memcpy(&fb, &bb, 4);
        ++total;
        std::string k;
        const double d = std::fabs(static_cast<double>(fa) - static_cast<double>(fb));
        const double m = std::fmax(std::fabs(fa), std::fabs(fb));
        if (std::isnan(fa) || std::isnan(fb) || std::isinf(fa) || std::isinf(fb)) k = "inf/nan involved";
        else if ((fa < 0) != (fb < 0) && (fa != 0 && fb != 0)) k = "sign flip";
        else if (d == 0) k = "equal value, different bits (+0/-0)";
        else if (d / m < 1e-6) k = "rel < 1e-6 (1-2 ulp)";
        else if (d / m < 1e-3) k = "rel 1e-6 .. 1e-3";
        else if (d / m < 1e-1) k = "rel 1e-3 .. 0.1";
        else k = "rel >= 0.1";
        ++bucket[k];
        if (k == "rel >= 0.1" || k == "sign flip" || k == "inf/nan involved" || k == "rel 1e-3 .. 0.1") {
            std::fprintf(big, "%s\t%s\t%s\t%s\t%.9g\t%.9g\n", k.c_str(), c[1].c_str(), c[2].c_str(), c[3].c_str(), fa, fb);
            std::string name = c[2].substr(2, c[2].find(" T=") - 2);
            ++bigByLeaf[name.substr(name.rfind('/') + 1)];
        }
    }
    std::fclose(big);
    for (auto& kv : bucket) std::printf("%-40s %lld\n", kv.first.c_str(), kv.second);
    std::printf("float-comparable changes: %lld\n", total);
    return 0;
}

// Base time-of-day curves. See include/sr3sky/tod_curves.h.

#include "sr3sky/tod_curves.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace sr3sky {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Value4 Lerp(const Value4& a, const Value4& b, float w) {
    Value4 r;
    for (int i = 0; i < 4; ++i) r[static_cast<size_t>(i)] = (1.0f - w) * a[static_cast<size_t>(i)] + w * b[static_cast<size_t>(i)];
    return r;
}

}  // namespace

std::array<Value4, 48> ResampleTodKeys(const std::map<int, Value4>& keys, const Value4& defaultValue) {
    std::array<Value4, 48> out;
    std::vector<int> keyed;
    for (const auto& k : keys)
        if (k.first >= 0 && k.first < 48) keyed.push_back(k.first);
    for (int i = 0; i < 48; ++i) {
        const auto it = keys.find(i);
        if (it != keys.end()) {
            out[static_cast<size_t>(i)] = it->second;
            continue;
        }
        if (keyed.empty()) {
            out[static_cast<size_t>(i)] = defaultValue;
            continue;
        }
        if (keyed.size() == 1) {
            out[static_cast<size_t>(i)] = keys.at(keyed[0]);
            continue;
        }
        int p = keyed.back(), n = keyed.front();  // cyclic neighbours
        for (int k : keyed)
            if (k < i) p = k;
        for (auto r = keyed.rbegin(); r != keyed.rend(); ++r)
            if (*r > i) n = *r;
        float w;
        if (p < n) {
            w = static_cast<float>(i - p) / static_cast<float>(n - p);
        } else {  // the wrap: the shipped code spans with 47, not 48
            const int span = n - p + 47;
            const int num = i > p ? i - p : i - p + 47;
            w = static_cast<float>(num) / static_cast<float>(span);
        }
        if (w <= 0.0f) {
            out[static_cast<size_t>(i)] = keys.at(p);
            continue;
        }
        w = std::min(w, 1.0f);
        out[static_cast<size_t>(i)] = Lerp(keys.at(p), keys.at(n), w);
    }
    return out;
}

Value4 SampleTodCurve(const TodCurve& c, float hour) {
    const float x = 2.0f * hour;
    const float f = std::floor(x);
    int slot = static_cast<int>(f) % 48;
    if (slot < 0) slot += 48;
    const float frac = x - f;
    return Lerp(c.slots[static_cast<size_t>(slot)], c.slots[static_cast<size_t>((slot + 1) % 48)], frac);
}

TodBaseCurves TodBaseCurves::Parse(const sr3xtbl::Document& doc) {
    TodBaseCurves out;
    const sr3xtbl::Node* root = doc.root();
    if (!root) return out;
    for (const sr3xtbl::Node* el : root->children()) {
        std::map<int, Value4> keys;
        int comps = 0;
        for (const sr3xtbl::Node* k : sr3xtbl::Children(el, "key")) {
            const std::string* time = sr3xtbl::ChildText(k, "time");
            const std::string* value = sr3xtbl::ChildText(k, "value");
            if (!time || !value) continue;
            Value4 v{};
            std::istringstream ss(*value);
            int n = 0;
            float f;
            while (n < 4 && ss >> f) v[static_cast<size_t>(n++)] = f;
            comps = std::max(comps, n);
            keys[std::atoi(time->c_str())] = v;
        }
        if (keys.empty()) continue;
        TodCurve c;
        c.components = comps;
        c.slots = ResampleTodKeys(keys);
        out.curves_[Lower(el->name())] = c;
    }
    return out;
}

bool TodBaseCurves::has(const std::string& name) const { return curves_.count(Lower(name)) != 0; }

std::optional<Value4> TodBaseCurves::sample(const std::string& name, float hour) const {
    const auto it = curves_.find(Lower(name));
    if (it == curves_.end()) return std::nullopt;
    return SampleTodCurve(it->second, hour);
}

std::optional<std::array<float, 3>> TodBaseCurves::colour(const std::string& name, float hour) const {
    const auto v = sample(name, hour);
    if (!v) return std::nullopt;
    return std::array<float, 3>{(*v)[0] * (*v)[3], (*v)[1] * (*v)[3], (*v)[2] * (*v)[3]};
}

Value4 MergeWeatherField(const Value4& base, const Value4* from, const Value4* to, float t, bool mergeOn) {
    if (!mergeOn || (!from && !to)) return base;
    const Value4& F = from ? *from : base;
    const Value4& T = to ? *to : base;
    const float omt = static_cast<float>(1.0 - static_cast<double>(t));
    Value4 out{};
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(static_cast<double>(F[i]) * omt);
        const float b = static_cast<float>(static_cast<double>(T[i]) * t);
        out[i] = static_cast<float>(static_cast<double>(a) + b);
    }
    return out;
}

}  // namespace sr3sky

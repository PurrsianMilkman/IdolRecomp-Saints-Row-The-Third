// Engine state whose value this project does not know (cloud phase,
// 2026-09-30, manager queue item 3).
//
// A spec often CONFIRMS that an engine global exists and how a function tests
// it (e.g. `zscene_is_loaded` returns true when the global state code
// 0x0153b51c equals 2, spec-lua-api-behaviour.md Sec14.23) without saying what
// the global holds at start-up or who writes it. Giving such a global a
// default here would be inventing a value. Instead it is an OpenValue: it
// carries its address and the spec section that names it, starts OPEN, and a
// read while OPEN throws OpenStateError instead of answering. A Lua stub turns
// that into a Lua error naming the global, so a mission run stops at "blocked
// on OPEN state 0x0153b51c (Sec14.23)" - which is exactly the question to send
// Team A - rather than looping forever on a made-up default. When Team A
// specs an initial value or a writer, implementing it is one set() call.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace sr3luahost {

class OpenStateError : public std::runtime_error {
public:
    OpenStateError(const std::string& global, const std::string& spec)
        : std::runtime_error("engine state " + global + " is OPEN (" + spec +
                             "): no spec gives its value, so it is not modelled"),
          global_(global), spec_(spec) {}
    const std::string& global() const { return global_; }
    const std::string& spec() const { return spec_; }

private:
    std::string global_;
    std::string spec_;
};

// One engine global of type T: OPEN until set(), refuses reads while OPEN.
template <typename T>
class OpenValue {
public:
    OpenValue(const char* global, const char* spec) : global_(global), spec_(spec) {}
    bool known() const { return value_.has_value(); }
    const T& get() const {
        if (!value_) throw OpenStateError(global_, spec_);
        return *value_;
    }
    void set(const T& v) { value_ = v; }
    void forget() { value_.reset(); }
    const char* global() const { return global_; }
    const char* spec() const { return spec_; }

private:
    const char* global_;
    const char* spec_;
    std::optional<T> value_;
};

// A 32-bit flags word where only some bits have a known value: a writer
// confirmed for bits 0x4/0x10 (mission_end_silently, Sec15.23) says nothing
// about the other 30. Reading bits outside the known mask throws.
class OpenBits32 {
public:
    OpenBits32(const char* global, const char* spec) : global_(global), spec_(spec) {}
    uint32_t knownMask() const { return knownMask_; }
    bool known(uint32_t mask) const { return (knownMask_ & mask) == mask; }
    uint32_t get(uint32_t mask) const {
        if (!known(mask)) throw OpenStateError(global_, spec_);
        return value_ & mask;
    }
    // Sets the bits in `mask` to the corresponding bits of `bits`; they become known.
    void setBits(uint32_t mask, uint32_t bits) {
        value_ = (value_ & ~mask) | (bits & mask);
        knownMask_ |= mask;
    }
    void forget() { knownMask_ = 0; value_ = 0; }
    const char* global() const { return global_; }
    const char* spec() const { return spec_; }

private:
    const char* global_;
    const char* spec_;
    uint32_t knownMask_ = 0;
    uint32_t value_ = 0;
};

// Per-name engine state (e.g. a scene's per-name state read by
// zscene_is_loaded's tier 1): every name is OPEN until set.
template <typename T>
class OpenValueMap {
public:
    OpenValueMap(const char* what, const char* spec) : what_(what), spec_(spec) {}
    bool known(const std::string& key) const { return values_.count(key) != 0; }
    const T& get(const std::string& key) const {
        auto it = values_.find(key);
        if (it == values_.end()) throw OpenStateError(std::string(what_) + "['" + key + "']", spec_);
        return it->second;
    }
    void set(const std::string& key, const T& v) { values_[key] = v; }
    void forget(const std::string& key) { values_.erase(key); }
    size_t knownCount() const { return values_.size(); }
    const char* what() const { return what_; }
    const char* spec() const { return spec_; }

private:
    const char* what_;
    const char* spec_;
    std::unordered_map<std::string, T> values_;
};

} // namespace sr3luahost

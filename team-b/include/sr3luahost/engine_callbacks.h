// Engine -> Lua callback events: the value types used by the engine callback
// dispatch of spec-lua-api-behaviour.md, sections "Engine callback dispatch:
// death and destruction (2026-10-14)" and "Engine callback dispatch: vehicle
// entry/exit, phone calls, triggers, group vehicles (2026-10-14)" (spec syncs
// bfe0542 and 98375ea; e903f59 was a draft and is superseded).
//
// Mechanism (spec A / V.0, FINAL): firing never calls Lua synchronously. The
// engine creates a NEW script thread (function name looked up as a Lua global,
// arguments typed string / nil / number / boolean) in the script-thread table;
// the thread body runs at the next script resume, in creation order. A missing
// or non-function global, or a full 256-entry thread table, silently loses the
// event. Registration is persistent: firing never clears a hook slot.
//
// EngineState::fireEngineCallback() builds a PendingCallback and hands it to a
// spawner (Host installs one that creates the thread, see src/lua_host.cpp).
// With no spawner installed (unit tests of EngineState alone) the event stays
// in a pending list a test can inspect.
#pragma once

#include <string>
#include <vector>

namespace sr3luahost {

struct CallbackArg {
    enum class Kind { Nil, String, Number, Boolean };
    Kind kind = Kind::Nil;
    std::string text;
    double number = 0.0;
    bool boolean = false;

    static CallbackArg nil() { return {}; }
    static CallbackArg str(std::string v) {
        CallbackArg a;
        a.kind = Kind::String;
        a.text = std::move(v);
        return a;
    }
    // A name that may be absent: empty string -> nil (spec V.0 "pass nil when there is no name").
    static CallbackArg strOrNil(const std::string& v) { return v.empty() ? nil() : str(v); }
    static CallbackArg num(double v) {
        CallbackArg a;
        a.kind = Kind::Number;
        a.number = v;
        return a;
    }
    static CallbackArg boolean_(bool v) {
        CallbackArg a;
        a.kind = Kind::Boolean;
        a.boolean = v;
        return a;
    }
    std::string describe() const {
        switch (kind) {
            case Kind::Nil: return "nil";
            case Kind::String: return "\"" + text + "\"";
            case Kind::Number: return std::to_string(number);
            case Kind::Boolean: return boolean ? "true" : "false";
        }
        return "?";
    }
};

struct PendingCallback {
    std::string name;                // the Lua global function to run
    std::vector<CallbackArg> args;   // engine-supplied arguments, in order
    std::string source;              // event kind label, e.g. "on_death" (diagnostics only)

    std::string describe() const {
        std::string s = name + "(";
        for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : "") + args[i].describe();
        return s + ")";
    }
};

struct FiredCallbackRecord {
    PendingCallback callback;
    bool accepted = false;  // false: silently lost (no global function, or thread table full)
};

}  // namespace sr3luahost

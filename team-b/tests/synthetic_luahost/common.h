// Shared declarations for the synthetic sr3luahost test suite (split out of
// synthetic_luahost_test.cpp; same sr3luahost_tests executable, same run order).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "sr3luahost/engine_state.h"
#include "sr3luahost/host.h"
#include "sr3luahost/stub_registry.h"
#include <utility>
#include <vector>

extern int g_failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED: " #cond " at " __FILE__ ":"         \
                      << __LINE__ << "\n";                                  \
            ++g_failures;                                                   \
        }                                                                   \
    } while (0)

using sr3luahost::confirmedHooks;
using sr3luahost::EngineState;
using sr3luahost::HitLog;
using sr3luahost::HookGroup;
using sr3luahost::HookSpec;
using sr3luahost::Host;
using sr3luahost::RegisteredName;
using sr3luahost::loadTaggedRegistrationList;
using sr3luahost::registerStubs;
using sr3luahost::VintTaggedValue;

std::string globalString(lua_State* L, const char* name);
std::vector<RegisteredName> specConfirmedFixtureNames();
extern std::vector<std::pair<char, uint32_t>> g_fadeCalls;
void fadeCbA(EngineState&, uint32_t t);
void fadeCbB(EngineState&, uint32_t t);
void fadeCbC(EngineState&, uint32_t t);

void test_block_1();
void test_block_2();
void test_block_3();
void test_block_4();
void test_block_5();
void test_block_6();
void test_block_7();
void test_block_8();
void test_block_9();
void test_block_10();
void test_block_11();
void test_block_12();
void test_block_13();
void test_block_14();
void test_block_15();
void test_block_16();
void test_block_17();
void test_block_18();
void test_block_19();
void test_block_20();
void test_block_21();
void test_block_22();

#pragma once

#include <stdexcept>
#include <string>

namespace sr3lua {

// Thrown by Lexer/Parser on real Lua 5.1 syntax it cannot tokenize/parse -
// an unterminated string/long-bracket, an unexpected token where the
// grammar (Lua 5.1 reference manual Sec8, "Complete Syntax") requires
// something else, a malformed number, etc. `line`/`col` are 1-based,
// pointing at the offending token's own start.
class LuaSyntaxError : public std::runtime_error {
public:
    LuaSyntaxError(const std::string& what, int line, int col)
        : std::runtime_error(what), line(line), col(col) {}

    int line;
    int col;
};

} // namespace sr3lua

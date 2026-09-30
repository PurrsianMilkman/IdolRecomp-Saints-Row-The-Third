// A real Lua 5.1 tokenizer, built from the PUBLIC Lua 5.1 reference manual
// (www.lua.org/manual/5.1/manual.html Sec2.1 "Lexical Conventions") - Lua
// 5.1 itself is explicitly out of cleanroom scope for this project (it's a
// public, unmodified third-party library, spec-lua-bindings.md Sec1/Sec2),
// so implementing its own public grammar is not a disassembly-derived
// finding and carries none of this project's usual CONFIRMED/HYPOTHESIS
// evidence apparatus - it's just "does this match the published language."
//
// Covers everything Sec2.1 documents: identifiers/keywords, the 21
// reserved words, all operator/punctuation symbols (including the 5.1 set:
// no `//`, no bitwise operators, no `::label::` - those are 5.2+), decimal
// and hex numeric literals (5.1 does NOT have hex floats - those are 5.2+;
// a hex literal here is read as a plain integer-shaped token, matching
// lobject.c's own 5.1-era `luaO_str2d`), short strings with the full 5.1
// escape set (`\a \b \f \n \r \t \v \\ \" \' \<newline> \ddd`), and long
// brackets `[[ ]]` / `[=[ ]=]` / ... (both for long strings and, preceded
// by `--`, long comments) with the "a newline immediately after the
// opening bracket is skipped" rule Sec2.1 documents.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sr3lua/errors.h"

namespace sr3lua {

enum class TokType {
    Eof,
    Name,     // identifier, not a reserved word
    Number,   // decimal or hex numeric literal (value not evaluated - not needed for this reader's own purpose)
    String,   // short "..."/'...' or long [[...]]/[=[...]=] literal (decoded escapes not retained - not needed)
    Keyword,  // one of Lua 5.1's 21 reserved words - `text` holds which one
    Symbol,   // an operator/punctuation token - `text` holds the exact spelling, e.g. "==", "..", "...", "::" is NOT 5.1
};

struct Token {
    TokType type = TokType::Eof;
    std::string text; // Name spelling, Keyword spelling, or Symbol spelling; empty for Number/String (value discarded)
    int line = 1;      // 1-based, start of token
    int col = 1;       // 1-based, start of token
};

// Streaming tokenizer: owns nothing but a view of the caller's source
// string (kept alive by the caller) and a cursor. `next()` is the only
// entry point - repeated calls walk forward; the token AFTER Eof keeps
// returning Eof rather than throwing, so a caller can peek past the end
// safely.
class Lexer {
public:
    explicit Lexer(const std::string& source);

    // Returns the next token and advances past it. Throws LuaSyntaxError on
    // a lexical error (unterminated string, unterminated long bracket,
    // malformed escape, a byte that starts no valid token).
    Token next();

private:
    const std::string& src_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    char peekChar(size_t ahead = 0) const;
    char advanceChar();
    bool atEnd() const;

    void skipWhitespaceAndComments();
    // Attempts to read a long-bracket body `[=*[ ... ]=*]` starting exactly
    // at the current `[` position. `level` is the number of `=` signs
    // already confirmed between the brackets. Returns the raw inner text
    // (unused by callers today, kept for completeness/future reuse).
    // Throws if unterminated. Consumes through the closing bracket.
    std::string readLongBracketBody(int level, int startLine, int startCol);
    // Returns -1 if the text at pos_ is not a long-bracket opener at all;
    // otherwise consumes the opener (`[`, `=` * n, `[`) and returns n.
    int tryConsumeLongBracketOpener();

    Token readName();
    Token readNumber();
    Token readShortString(char quote);
    Token readLongString(int level, int startLine, int startCol);
};

} // namespace sr3lua

#include "sr3lua/lexer.h"

#include <cctype>
#include <unordered_set>

namespace sr3lua {

namespace {

// Lua 5.1 reference manual Sec2.1: the 21 reserved words.
const std::unordered_set<std::string>& keywordSet() {
    static const std::unordered_set<std::string> kw = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while",
    };
    return kw;
}

bool isNameStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool isNameCont(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

} // namespace

Lexer::Lexer(const std::string& source) : src_(source) {}

bool Lexer::atEnd() const { return pos_ >= src_.size(); }

char Lexer::peekChar(size_t ahead) const {
    size_t p = pos_ + ahead;
    return p < src_.size() ? src_[p] : '\0';
}

char Lexer::advanceChar() {
    char c = src_[pos_++];
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

int Lexer::tryConsumeLongBracketOpener() {
    // Caller has already checked peekChar() == '['. Look ahead for
    // '=' * n then '[' WITHOUT consuming anything unless it really is a
    // long-bracket opener (a lone '[' or '[=' followed by something else
    // is just ordinary '[' / '=' tokens elsewhere in the grammar - only
    // relevant for the string-literal case, where the caller already
    // knows it wants a long bracket if one is present).
    size_t p = pos_ + 1;
    int n = 0;
    while (p < src_.size() && src_[p] == '=') {
        n++;
        p++;
    }
    if (p < src_.size() && src_[p] == '[') {
        // Consume '[' '=' * n '['.
        advanceChar(); // '['
        for (int i = 0; i < n; i++) advanceChar(); // '='
        advanceChar(); // '['
        return n;
    }
    return -1;
}

std::string Lexer::readLongBracketBody(int level, int startLine, int startCol) {
    // Sec2.1: "if the opening long bracket is immediately followed by a
    // newline, the newline is not included in the string."
    if (!atEnd() && (peekChar() == '\n' || peekChar() == '\r')) {
        char c = advanceChar();
        if ((c == '\r' && peekChar() == '\n') || (c == '\n' && peekChar() == '\r')) {
            advanceChar();
        }
    }
    std::string out;
    for (;;) {
        if (atEnd()) {
            throw LuaSyntaxError("unterminated long bracket (level " + std::to_string(level) + ")",
                                  startLine, startCol);
        }
        if (peekChar() == ']') {
            size_t save = pos_;
            int saveLine = line_, saveCol = col_;
            advanceChar(); // ']'
            int n = 0;
            while (!atEnd() && peekChar() == '=') {
                n++;
                advanceChar();
            }
            if (n == level && !atEnd() && peekChar() == ']') {
                advanceChar(); // closing ']'
                return out;
            }
            // Not a real closer - it was just literal text; rewind and
            // consume one real character to make forward progress.
            pos_ = save;
            line_ = saveLine;
            col_ = saveCol;
            out.push_back(advanceChar());
        } else {
            out.push_back(advanceChar());
        }
    }
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        if (atEnd()) return;
        char c = peekChar();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            advanceChar();
            continue;
        }
        if (c == '-' && peekChar(1) == '-') {
            int startLine = line_, startCol = col_;
            advanceChar();
            advanceChar();
            // Possible long comment: '--' immediately followed by a long
            // bracket opener.
            if (!atEnd() && peekChar() == '[') {
                int level = tryConsumeLongBracketOpener();
                if (level >= 0) {
                    readLongBracketBody(level, startLine, startCol);
                    continue;
                }
            }
            // Short comment: runs to end of line (or EOF).
            while (!atEnd() && peekChar() != '\n' && peekChar() != '\r') advanceChar();
            continue;
        }
        return;
    }
}

Token Lexer::readName() {
    int startLine = line_, startCol = col_;
    std::string text;
    while (!atEnd() && isNameCont(peekChar())) text.push_back(advanceChar());
    Token t;
    t.line = startLine;
    t.col = startCol;
    if (keywordSet().count(text)) {
        t.type = TokType::Keyword;
        t.text = text;
    } else {
        t.type = TokType::Name;
        t.text = text;
    }
    return t;
}

Token Lexer::readNumber() {
    int startLine = line_, startCol = col_;
    std::string text;
    if (peekChar() == '0' && (peekChar(1) == 'x' || peekChar(1) == 'X')) {
        text.push_back(advanceChar()); // '0'
        text.push_back(advanceChar()); // 'x'
        while (!atEnd() && isHexDigit(peekChar())) text.push_back(advanceChar());
        // Lua 5.1 has no hex floats - a trailing '.'/exponent on a hex
        // literal is not part of the number token in this reader (matches
        // 5.1's own str2d, which stops at the hex digit run).
    } else {
        while (!atEnd() && isDigit(peekChar())) text.push_back(advanceChar());
        if (!atEnd() && peekChar() == '.') {
            text.push_back(advanceChar());
            while (!atEnd() && isDigit(peekChar())) text.push_back(advanceChar());
        }
        if (!atEnd() && (peekChar() == 'e' || peekChar() == 'E')) {
            size_t save = pos_;
            int saveLine = line_, saveCol = col_;
            std::string exp;
            exp.push_back(advanceChar());
            if (!atEnd() && (peekChar() == '+' || peekChar() == '-')) exp.push_back(advanceChar());
            if (!atEnd() && isDigit(peekChar())) {
                while (!atEnd() && isDigit(peekChar())) exp.push_back(advanceChar());
                text += exp;
            } else {
                // Not actually an exponent - rewind (e.g. "3 .. e" edge
                // case never occurs in Lua 5.1 numbers, but be safe).
                pos_ = save;
                line_ = saveLine;
                col_ = saveCol;
            }
        }
    }
    Token t;
    t.type = TokType::Number;
    t.line = startLine;
    t.col = startCol;
    return t;
}

Token Lexer::readShortString(char quote) {
    int startLine = line_, startCol = col_;
    advanceChar(); // opening quote
    while (true) {
        if (atEnd()) throw LuaSyntaxError("unterminated string", startLine, startCol);
        char c = peekChar();
        if (c == quote) {
            advanceChar();
            break;
        }
        if (c == '\n' || c == '\r') {
            throw LuaSyntaxError("unterminated string (hit end of line)", startLine, startCol);
        }
        if (c == '\\') {
            advanceChar(); // backslash
            if (atEnd()) throw LuaSyntaxError("unterminated string escape", startLine, startCol);
            char e = peekChar();
            switch (e) {
                case 'a': case 'b': case 'f': case 'n': case 'r':
                case 't': case 'v': case '\\': case '"': case '\'':
                    advanceChar();
                    break;
                case '\n': case '\r': {
                    // Sec2.1: \<newline> inserts a newline into the string.
                    char c2 = advanceChar();
                    if ((c2 == '\r' && peekChar() == '\n') || (c2 == '\n' && peekChar() == '\r')) {
                        advanceChar();
                    }
                    break;
                }
                default:
                    if (isDigit(e)) {
                        // \ddd, up to 3 decimal digits.
                        for (int i = 0; i < 3 && !atEnd() && isDigit(peekChar()); i++) advanceChar();
                    } else {
                        // Unknown escape - real Lua 5.1 rejects this too;
                        // still consume the one char to keep making
                        // progress with a clear diagnostic rather than
                        // looping.
                        throw LuaSyntaxError(std::string("invalid escape sequence '\\") + e + "'",
                                              startLine, startCol);
                    }
            }
            continue;
        }
        advanceChar();
    }
    Token t;
    t.type = TokType::String;
    t.line = startLine;
    t.col = startCol;
    return t;
}

Token Lexer::readLongString(int level, int startLine, int startCol) {
    readLongBracketBody(level, startLine, startCol);
    Token t;
    t.type = TokType::String;
    t.line = startLine;
    t.col = startCol;
    return t;
}

Token Lexer::next() {
    skipWhitespaceAndComments();
    int startLine = line_, startCol = col_;
    if (atEnd()) {
        Token t;
        t.type = TokType::Eof;
        t.line = startLine;
        t.col = startCol;
        return t;
    }
    char c = peekChar();

    if (isNameStart(c)) return readName();
    if (isDigit(c)) return readNumber();
    // ".5" is a valid Lua number (leading-dot decimal).
    if (c == '.' && isDigit(peekChar(1))) return readNumber();

    if (c == '"' || c == '\'') return readShortString(c);

    if (c == '[') {
        int level = tryConsumeLongBracketOpener();
        if (level >= 0) return readLongString(level, startLine, startCol);
        advanceChar();
        Token t;
        t.type = TokType::Symbol;
        t.text = "[";
        t.line = startLine;
        t.col = startCol;
        return t;
    }

    // Multi-character symbols first (longest match), matching Lua 5.1's
    // own token set exactly (Sec2.1's operator/punctuation list - no `//`,
    // no `&|~<<>>`, no `::`, those are 5.2/5.3/5.4 additions).
    auto makeSym = [&](const char* s, int n) {
        Token t;
        t.type = TokType::Symbol;
        t.text = s;
        t.line = startLine;
        t.col = startCol;
        for (int i = 0; i < n; i++) advanceChar();
        return t;
    };

    if (c == '.' ) {
        if (peekChar(1) == '.' && peekChar(2) == '.') return makeSym("...", 3);
        if (peekChar(1) == '.') return makeSym("..", 2);
        return makeSym(".", 1);
    }
    if (c == '=' ) { if (peekChar(1) == '=') return makeSym("==", 2); return makeSym("=", 1); }
    if (c == '~' ) { if (peekChar(1) == '=') return makeSym("~=", 2);
                      throw LuaSyntaxError("unexpected character '~'", startLine, startCol); }
    if (c == '<' ) { if (peekChar(1) == '=') return makeSym("<=", 2); return makeSym("<", 1); }
    if (c == '>' ) { if (peekChar(1) == '=') return makeSym(">=", 2); return makeSym(">", 1); }

    // Single-character symbols ('[' is handled above with the long-bracket check).
    switch (c) {
        case '+': return makeSym("+", 1);
        case '-': return makeSym("-", 1);
        case '*': return makeSym("*", 1);
        case '/': return makeSym("/", 1);
        case '%': return makeSym("%", 1);
        case '^': return makeSym("^", 1);
        case '#': return makeSym("#", 1);
        case '(': return makeSym("(", 1);
        case ')': return makeSym(")", 1);
        case '{': return makeSym("{", 1);
        case '}': return makeSym("}", 1);
        case ']': return makeSym("]", 1);
        case ';': return makeSym(";", 1);
        case ':': return makeSym(":", 1);
        case ',': return makeSym(",", 1);
        default:
            break;
    }

    throw LuaSyntaxError(std::string("unexpected character '") + c + "'", startLine, startCol);
}

} // namespace sr3lua

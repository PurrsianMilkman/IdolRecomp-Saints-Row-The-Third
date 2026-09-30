// A real Lua 5.1 recursive-descent parser, implementing the PUBLIC grammar
// in the Lua 5.1 reference manual Sec8 ("The Complete Syntax of Lua") and
// the operator precedence table in Sec2.5.6/Sec3.4.8 - see lexer.h's own
// top comment for why this is "implement the public spec", not a
// disassembly finding.
//
// UNLIKE a typical parser, this one does not materialize a separate AST.
// It performs semantic recording DURING the single recursive-descent
// pass, exactly the way Lua's OWN reference implementation works
// (lparser.c is a single-pass parser that emits bytecode directly, with
// no separate AST stage either) - so this is a faithful architectural
// match to the real thing, not a shortcut taken for this reader's own
// convenience.
//
// WHAT IT RECORDS, and why exactly this shape (matching a specific task's
// own definitions):
//   - A CALL SITE is recorded only for a "bare name" direct call:
//     `Name(args)` / `Name{table}` / `Name"str"` where `Name` is the
//     UNSUFFIXED primary expression (Lua grammar: suffixedexp with the
//     `args` production applied as the very first suffix). `t.f(...)`,
//     `t[i](...)`, `t:m(...)` and `f()(...)` (calling a call's result) are
//     real Lua call expressions too, but none of them are "a script calls
//     the global function named X" in the flat-namespace sense - they
//     call a FIELD, an INDEXED value, a METHOD, or an anonymous returned
//     value, respectively - so none of those are recorded as a call site
//     for a name. This matches how spec-lua-bindings.md's own disassembly
//     side describes the engine's C-function-exposed-to-Lua and
//     named-hook surfaces: always invoked as a plain global name looked
//     up in the Lua globals table, never as a dotted path.
//   - A GLOBAL DEFINE is recorded only for the two forms a task asking
//     for this census explicitly named: `function Name ( ... ) ... end`
//     with an UNDOTTED, COLONLESS `Name` (the `functionstat` production's
//     simplest case - per the manual, Sec3.4.10, this is literally sugar
//     for `Name = function ... end`), or `Name = function ( ... ) ... end`
//     as a plain assignment whose right-hand side is syntactically a
//     function literal. `function T.f()`, `function T:m()`,
//     `T.f = function() end`, and `local`-anything are none of these two
//     forms and are correctly NOT recorded - but ARE still fully parsed
//     (their bodies included) so the rest of the file's calls are found.
//   - Both classifications require real lexical scope tracking (Lua 5.1
//     Sec4.2's rules: a `local` is visible from the statement after its
//     declaration to the end of its own block; `local function` binds the
//     name before parsing the body, for self-recursion; loop/`for`
//     variables, function parameters, and `repeat`'s special
//     until-condition-sees-the-body-locals rule are all real scope
//     boundaries; a name not found in any enclosing scope is a global by
//     construction) - this parser tracks all of it with a real scope
//     stack rather than a flat/regex name search, which is exactly why a
//     one-pass regex over the text cannot answer this task's question
//     correctly (it cannot tell a global `foo` from a `local foo`, or a
//     bare call from a field/method call).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sr3lua/errors.h"
#include "sr3lua/lexer.h"

namespace sr3lua {

struct CallSite {
    std::string name;
    int line = 0;
    int col = 0;
};

enum class DefineKind {
    FunctionStatement, // `function Name(...) ... end`
    AssignFunction,     // `Name = function(...) ... end`
};

struct GlobalDefine {
    std::string name;
    int line = 0;
    int col = 0;
    DefineKind kind = DefineKind::FunctionStatement;
    // True iff this define was recorded with only the file/chunk-level
    // scope frame on the scope stack (Parser::scopes_.size() == 1) at the
    // moment of recording - i.e. not nested inside any block (function
    // body, if/do/while/for/repeat) at all. A stricter, more conservative
    // reading of "top-level" than "not nested in a function specifically"
    // (a function defined inside a top-level `if` block would read false
    // here), chosen because it needs no new scope-kind tracking beyond
    // what the parser already maintains. Added for the per-script
    // entry-point-naming census (HANDOFF.md Sec9's §C item 4) - existing
    // callers of GlobalDefine that don't read this field are unaffected.
    bool isTopLevel = false;
};

struct ScriptAnalysis {
    std::vector<CallSite> calls;      // every bare-name global call site found, in source order
    std::vector<GlobalDefine> defines; // every bare-name global function definition found, in source order
};

class Parser {
public:
    // Parses `source` as one Lua 5.1 chunk (a whole script file - the
    // grammar's own top production, `chunk`). Throws LuaSyntaxError on any
    // real syntax error. On success, `out` holds every call site/global
    // define found by a real, scope-aware parse of the entire file.
    static ScriptAnalysis analyze(const std::string& source);

private:
    explicit Parser(const std::string& source);

    Lexer lexer_;
    Token cur_;
    std::optional<Token> lookahead_; // one token of lookahead past cur_, filled on demand (peekAhead())
    ScriptAnalysis result_;

    // Scope stack: scopes_[0] is the file/chunk-level block; each nested
    // block (function body, do/while/for/if/repeat body, ...) pushes one
    // frame of locally-declared names and pops it on exit. A name not
    // found in ANY frame (searched innermost-first, which is also just
    // "found in any frame" since we only care about local-vs-not) is a
    // global by construction (Lua 5.1 Sec4.2).
    std::vector<std::vector<std::string>> scopes_;

    void pushScope();
    void popScope();
    void declareLocal(const std::string& name);
    bool isLocalInScope(const std::string& name) const;

    void advance();
    Token peekAhead(); // token after cur_, without consuming cur_ (needed for the `Name '=' exp` table-field lookahead - Sec8's own grammar is ambiguous there with 1 token; real lparser.c uses the identical lookahead)
    bool checkSym(const char* s) const;
    bool checkKw(const char* s) const;
    bool checkName() const;
    bool checkEof() const;
    bool matchSym(const char* s);
    bool matchKw(const char* s);
    void expectSym(const char* s);
    void expectKw(const char* s);
    std::string expectName();
    [[noreturn]] void error(const std::string& msg) const;

    // Grammar (Sec8), each parseX consuming exactly one X and leaving
    // cur_ on the following token.
    void parseChunk();
    bool isBlockFollowKeyword() const; // Eof/'end'/'else'/'elseif'/'until' - Sec8's block-terminator set
    void parseBlock();                 // {stat} [laststat] - does NOT push/pop scope itself; callers control that
    void parseStatement();             // one non-last `stat` (return/break are handled directly by parseBlock, since Sec8 requires them to be the block's own final statement)
    void parseIfStat();
    void parseWhileStat();
    void parseDoStat();
    void parseForStat();
    void parseRepeatStat();
    void parseFunctionStat();          // `function funcname funcbody`
    void parseLocalStat();             // `local function Name funcbody` | `local namelist ['=' explist]`
    void parseExprStatementOrAssignment(); // varlist '=' explist | functioncall
    void parseFuncBody(bool isMethod); // '(' [parlist] ')' block end - pushes/pops its own scope
    void parseTableConstructor();
    void parseField();                 // '[' exp ']' '=' exp | Name '=' exp | exp

    // Expression parsing. The result carries just enough shape info to
    // support (a) the "bare name, no suffix yet" call-site rule above, and
    // (b) the "RHS is exactly a function literal" assign-define rule; no
    // value/type is computed (this reader never evaluates Lua). Either flag
    // is true only when NO operator/suffix was applied on top of the raw
    // primary/simple expression - combining via any binop, unop, or
    // suffix (other than the recorded call itself) yields a fresh,
    // flag-less result, exactly matching "foo = function() end" needing to
    // be a literal, not e.g. "foo = (function() end)" or "foo = f or function() end".
    struct ExprResult {
        bool isBareName = false;      // true iff this expression IS, syntactically, exactly one unsuffixed Name reference
        std::string name;             // valid iff isBareName
        int line = 0, col = 0;        // location of that Name, iff isBareName
        bool isFunctionLiteral = false; // true iff this expression IS, syntactically, exactly `function(...) ... end`
    };
    ExprResult parseExpr(int limit = 0);
    ExprResult parseSimpleExpr();      // nil/false/true/Number/String/'...'/function/tableconstructor/suffixedexp
    ExprResult parseSuffixedExpr();    // primaryexp {suffix}, recording bare-name call sites per this header's own rule
    ExprResult parsePrimaryExpr();     // Name | '(' exp ')'
    void parseArgs();                  // '(' [explist] ')' | tableconstructor | String
    std::vector<ExprResult> parseExprList(); // exp {',' exp}

    void recordCallIfBareName(const ExprResult& base);
    void recordGlobalDefineIfEligible(const std::string& name, int line, int col, DefineKind kind);
};

} // namespace sr3lua

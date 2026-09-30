#include "sr3lua/parser.h"

namespace sr3lua {

namespace {

// Sec2.5.6 / Sec3.4.8's binary operator precedence table, and the unary
// operator priority - transcribed from the public Lua 5.1 manual (the
// same table Lua's own lparser.c hardcodes as `priority[]`/`UNARY_PRIORITY`).
// {left, right} - right < left on '..'/'^' implements their documented
// right-associativity via precedence-climbing.
constexpr int kUnaryPriority = 8;

bool binopPriority(const Token& t, int& left, int& right) {
    if (t.type == TokType::Symbol) {
        if (t.text == "+") { left = 6; right = 6; return true; }
        if (t.text == "-") { left = 6; right = 6; return true; }
        if (t.text == "*") { left = 7; right = 7; return true; }
        if (t.text == "/") { left = 7; right = 7; return true; }
        if (t.text == "%") { left = 7; right = 7; return true; }
        if (t.text == "^") { left = 10; right = 9; return true; }
        if (t.text == "..") { left = 5; right = 4; return true; }
        if (t.text == "==") { left = 3; right = 3; return true; }
        if (t.text == "~=") { left = 3; right = 3; return true; }
        if (t.text == "<") { left = 3; right = 3; return true; }
        if (t.text == "<=") { left = 3; right = 3; return true; }
        if (t.text == ">") { left = 3; right = 3; return true; }
        if (t.text == ">=") { left = 3; right = 3; return true; }
    } else if (t.type == TokType::Keyword) {
        if (t.text == "and") { left = 2; right = 2; return true; }
        if (t.text == "or") { left = 1; right = 1; return true; }
    }
    return false;
}

} // namespace

Parser::Parser(const std::string& source) : lexer_(source) {}

ScriptAnalysis Parser::analyze(const std::string& source) {
    Parser p(source);
    p.parseChunk();
    return std::move(p.result_);
}

// ---- token stream plumbing -------------------------------------------------

void Parser::advance() {
    if (lookahead_) {
        cur_ = *lookahead_;
        lookahead_.reset();
    } else {
        cur_ = lexer_.next();
    }
}

Token Parser::peekAhead() {
    if (!lookahead_) lookahead_ = lexer_.next();
    return *lookahead_;
}

bool Parser::checkSym(const char* s) const { return cur_.type == TokType::Symbol && cur_.text == s; }
bool Parser::checkKw(const char* s) const { return cur_.type == TokType::Keyword && cur_.text == s; }
bool Parser::checkName() const { return cur_.type == TokType::Name; }
bool Parser::checkEof() const { return cur_.type == TokType::Eof; }

bool Parser::matchSym(const char* s) {
    if (!checkSym(s)) return false;
    advance();
    return true;
}
bool Parser::matchKw(const char* s) {
    if (!checkKw(s)) return false;
    advance();
    return true;
}
void Parser::expectSym(const char* s) {
    if (!matchSym(s)) error(std::string("expected '") + s + "', got '" + cur_.text + "'");
}
void Parser::expectKw(const char* s) {
    if (!matchKw(s)) error(std::string("expected '") + s + "', got '" + cur_.text + "'");
}
std::string Parser::expectName() {
    if (!checkName()) error("expected a name, got '" + cur_.text + "'");
    std::string n = cur_.text;
    advance();
    return n;
}
void Parser::error(const std::string& msg) const { throw LuaSyntaxError(msg, cur_.line, cur_.col); }

// ---- scope tracking ---------------------------------------------------------

void Parser::pushScope() { scopes_.emplace_back(); }
void Parser::popScope() { scopes_.pop_back(); }
void Parser::declareLocal(const std::string& name) { scopes_.back().push_back(name); }

bool Parser::isLocalInScope(const std::string& name) const {
    for (auto frameIt = scopes_.rbegin(); frameIt != scopes_.rend(); ++frameIt) {
        for (const auto& n : *frameIt) {
            if (n == name) return true;
        }
    }
    return false;
}

void Parser::recordCallIfBareName(const ExprResult& base) {
    if (!base.isBareName) return;
    if (isLocalInScope(base.name)) return; // local/parameter/upvalue - not a global by Lua 5.1 Sec4.2's rule
    result_.calls.push_back(CallSite{base.name, base.line, base.col});
}

void Parser::recordGlobalDefineIfEligible(const std::string& name, int line, int col, DefineKind kind) {
    if (isLocalInScope(name)) return; // e.g. `local foo; function foo() end` - foo is local here, not global
    bool isTopLevel = (scopes_.size() == 1); // only the file-level chunk scope is on the stack
    result_.defines.push_back(GlobalDefine{name, line, col, kind, isTopLevel});
}

// ---- grammar: chunk/block/statements ---------------------------------------
// Grammar productions below follow the Lua 5.1 manual Sec8 ("The Complete
// Syntax of Lua") one for one; see this header's own top comment for why
// no separate AST is built.

void Parser::parseChunk() {
    advance(); // prime cur_
    pushScope(); // file-level chunk scope
    parseBlock();
    popScope();
    if (!checkEof()) error("unexpected trailing token '" + cur_.text + "' after chunk");
}

bool Parser::isBlockFollowKeyword() const {
    return checkEof() || checkKw("end") || checkKw("else") || checkKw("elseif") || checkKw("until");
}

void Parser::parseBlock() {
    while (!isBlockFollowKeyword()) {
        if (checkKw("return")) {
            advance();
            if (!isBlockFollowKeyword() && !checkSym(";")) {
                parseExprList();
            }
            matchSym(";");
            break; // laststat must be the block's final statement (Sec8: chunk ::= {stat[';']} [laststat[';']])
        }
        if (checkKw("break")) {
            advance();
            matchSym(";");
            break;
        }
        parseStatement();
        matchSym(";");
    }
}

void Parser::parseStatement() {
    if (matchSym(";")) return; // empty statement - real lparser.c also accepts a lone ';'
    if (checkKw("if")) { parseIfStat(); return; }
    if (checkKw("while")) { parseWhileStat(); return; }
    if (checkKw("do")) { parseDoStat(); return; }
    if (checkKw("for")) { parseForStat(); return; }
    if (checkKw("repeat")) { parseRepeatStat(); return; }
    if (checkKw("function")) { parseFunctionStat(); return; }
    if (checkKw("local")) { parseLocalStat(); return; }
    parseExprStatementOrAssignment();
}

void Parser::parseIfStat() {
    advance(); // 'if'
    parseExpr();
    expectKw("then");
    pushScope();
    parseBlock();
    popScope();
    while (checkKw("elseif")) {
        advance();
        parseExpr();
        expectKw("then");
        pushScope();
        parseBlock();
        popScope();
    }
    if (matchKw("else")) {
        pushScope();
        parseBlock();
        popScope();
    }
    expectKw("end");
}

void Parser::parseWhileStat() {
    advance(); // 'while'
    parseExpr();
    expectKw("do");
    pushScope();
    parseBlock();
    popScope();
    expectKw("end");
}

void Parser::parseDoStat() {
    advance(); // 'do'
    pushScope();
    parseBlock();
    popScope();
    expectKw("end");
}

void Parser::parseForStat() {
    advance(); // 'for'
    std::string firstName = expectName();
    if (checkSym("=")) {
        advance();
        parseExpr(); // start
        expectSym(",");
        parseExpr(); // stop
        if (matchSym(",")) parseExpr(); // step (optional)
        expectKw("do");
        pushScope();
        declareLocal(firstName);
        parseBlock();
        popScope();
        expectKw("end");
    } else {
        std::vector<std::string> names{firstName};
        while (matchSym(",")) names.push_back(expectName());
        expectKw("in");
        parseExprList(); // iterator exprs, evaluated in the ENCLOSING scope
        expectKw("do");
        pushScope();
        for (auto& n : names) declareLocal(n);
        parseBlock();
        popScope();
        expectKw("end");
    }
}

void Parser::parseRepeatStat() {
    advance(); // 'repeat'
    pushScope();
    parseBlock();
    expectKw("until");
    parseExpr(); // Sec8's own special rule: the until-condition still sees the body's locals - scope not yet popped
    popScope();
}

void Parser::parseFunctionStat() {
    advance(); // 'function'
    int nameLine = cur_.line, nameCol = cur_.col;
    std::string base = expectName();
    bool hasSuffix = false;
    bool isMethod = false;
    while (checkSym(".")) {
        hasSuffix = true;
        advance();
        expectName();
    }
    if (checkSym(":")) {
        hasSuffix = true;
        isMethod = true;
        advance();
        expectName();
    }
    if (!hasSuffix) {
        // `function Name(...) ... end` with an undotted, colonless Name -
        // Sec3.4.10: sugar for `Name = function ... end` at the CURRENT scope.
        recordGlobalDefineIfEligible(base, nameLine, nameCol, DefineKind::FunctionStatement);
    }
    parseFuncBody(isMethod);
}

void Parser::parseLocalStat() {
    advance(); // 'local'
    if (matchKw("function")) {
        std::string name = expectName();
        declareLocal(name); // visible inside its own body first, for self-recursion (Sec8)
        parseFuncBody(false);
        return;
    }
    std::vector<std::string> names;
    names.push_back(expectName());
    while (matchSym(",")) names.push_back(expectName());
    if (matchSym("=")) {
        parseExprList(); // RHS evaluated BEFORE these names become local (Sec4.2: sees the OLD bindings)
    }
    for (auto& n : names) declareLocal(n);
}

void Parser::parseExprStatementOrAssignment() {
    ExprResult first = parseSuffixedExpr();
    if (checkSym("=") || checkSym(",")) {
        std::vector<ExprResult> lhs;
        lhs.push_back(first);
        while (matchSym(",")) lhs.push_back(parseSuffixedExpr());
        expectSym("=");
        std::vector<ExprResult> rhs = parseExprList();
        for (size_t i = 0; i < lhs.size(); i++) {
            if (!lhs[i].isBareName) continue;
            if (i >= rhs.size()) continue;            // fewer RHS values than vars -> nil, not a function define
            if (!rhs[i].isFunctionLiteral) continue;   // only `Name = function(...) ... end` literally, per task scope
            recordGlobalDefineIfEligible(lhs[i].name, lhs[i].line, lhs[i].col, DefineKind::AssignFunction);
        }
        return;
    }
    // Otherwise this must be a bare `functioncall` statement; any bare-name
    // call site was already recorded as a side effect of parseSuffixedExpr
    // above. (Real Lua additionally requires `first` to end in a call for
    // this to be syntactically valid; not re-enforced here since the
    // census target is already-compiled-by-real-Lua shipped scripts.)
}

// ---- function bodies / table constructors ----------------------------------

void Parser::parseFuncBody(bool isMethod) {
    expectSym("(");
    pushScope();
    if (isMethod) declareLocal("self"); // Sec3.4.10: `function t:m(...)` implicitly adds `self` as first param
    if (!checkSym(")")) {
        for (;;) {
            if (matchSym("...")) break; // vararg ends parlist
            declareLocal(expectName());
            if (!matchSym(",")) break;
        }
    }
    expectSym(")");
    parseBlock();
    expectKw("end");
    popScope();
}

void Parser::parseField() {
    if (checkSym("[")) {
        advance();
        parseExpr(); // key
        expectSym("]");
        expectSym("=");
        parseExpr(); // value
        return;
    }
    if (checkName() && peekAhead().type == TokType::Symbol && peekAhead().text == "=") {
        advance(); // Name (key, discarded)
        advance(); // '='
        parseExpr(); // value
        return;
    }
    parseExpr(); // plain list-style field
}

void Parser::parseTableConstructor() {
    expectSym("{");
    while (!checkSym("}")) {
        parseField();
        if (checkSym(",") || checkSym(";")) {
            advance();
        } else {
            break;
        }
    }
    expectSym("}");
}

// ---- expressions ------------------------------------------------------------

Parser::ExprResult Parser::parseExpr(int limit) {
    ExprResult left;
    if (checkSym("-") || checkKw("not") || checkSym("#")) {
        advance();
        parseExpr(kUnaryPriority); // operand, result discarded (unary application is never bare/literal)
        left = ExprResult{};
    } else {
        left = parseSimpleExpr();
    }
    for (;;) {
        int lp = 0, rp = 0;
        if (!binopPriority(cur_, lp, rp)) break;
        if (lp <= limit) break;
        advance(); // consume operator
        parseExpr(rp); // right operand, result discarded
        left = ExprResult{}; // combination via a binop is never bare/literal
    }
    return left;
}

Parser::ExprResult Parser::parseSimpleExpr() {
    if (checkKw("nil") || checkKw("false") || checkKw("true")) {
        advance();
        return ExprResult{};
    }
    if (cur_.type == TokType::Number) {
        advance();
        return ExprResult{};
    }
    if (cur_.type == TokType::String) {
        advance();
        return ExprResult{};
    }
    if (checkSym("...")) {
        advance();
        return ExprResult{};
    }
    if (checkKw("function")) {
        advance();
        parseFuncBody(false);
        ExprResult r;
        r.isFunctionLiteral = true;
        return r;
    }
    if (checkSym("{")) {
        parseTableConstructor();
        return ExprResult{};
    }
    return parseSuffixedExpr();
}

Parser::ExprResult Parser::parseSuffixedExpr() {
    ExprResult base = parsePrimaryExpr();
    for (;;) {
        if (matchSym(".")) {
            expectName();
            base = ExprResult{};
            continue;
        }
        if (matchSym("[")) {
            parseExpr();
            expectSym("]");
            base = ExprResult{};
            continue;
        }
        if (matchSym(":")) {
            expectName(); // method name - not tracked as a global (it's a method on `base`'s value, not a flat global)
            parseArgs();
            base = ExprResult{};
            continue;
        }
        if (checkSym("(") || checkSym("{") || cur_.type == TokType::String) {
            recordCallIfBareName(base); // only fires when `base` is still an unsuffixed bare Name
            parseArgs();
            base = ExprResult{};
            continue;
        }
        break;
    }
    return base;
}

Parser::ExprResult Parser::parsePrimaryExpr() {
    if (cur_.type == TokType::Name) {
        ExprResult r;
        r.isBareName = true;
        r.name = cur_.text;
        r.line = cur_.line;
        r.col = cur_.col;
        advance();
        return r;
    }
    if (matchSym("(")) {
        parseExpr();
        expectSym(")");
        return ExprResult{}; // parenthesizing removes it from the `var`/bare-name production (Sec8)
    }
    error("unexpected token '" + cur_.text + "', expected a name or '('");
}

void Parser::parseArgs() {
    if (matchSym("(")) {
        if (!checkSym(")")) parseExprList();
        expectSym(")");
        return;
    }
    if (checkSym("{")) {
        parseTableConstructor();
        return;
    }
    if (cur_.type == TokType::String) {
        advance();
        return;
    }
    error("function arguments expected, got '" + cur_.text + "'");
}

std::vector<Parser::ExprResult> Parser::parseExprList() {
    std::vector<ExprResult> out;
    out.push_back(parseExpr());
    while (matchSym(",")) out.push_back(parseExpr());
    return out;
}

} // namespace sr3lua

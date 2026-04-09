#include "Lexer.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>   // ★ 新增

namespace jc {

    static const std::unordered_map<std::string, TokenType> keywords = {
        {"if",       TokenType::IF},
        {"else",     TokenType::ELSE},
        {"while",    TokenType::WHILE},
        {"for",      TokenType::FOR},
        {"break",    TokenType::BREAK},
        {"continue", TokenType::CONTINUE},
        {"return",   TokenType::RETURN},
        {"local",    TokenType::LOCAL},
        {"ref",      TokenType::REF},
        {"const",    TokenType::CONST},
        {"delete",   TokenType::DELETE},
        {"global",   TokenType::GLOBAL},
        {"in",       TokenType::IN},
        {"throw",    TokenType::THROW},        // ★
        {"try",      TokenType::TRY},          // ★
        {"catch",    TokenType::CATCH},         // ★
        {"import",   TokenType::IMPORT},
        {"switch",   TokenType::SWITCH},       // ★
        {"case",     TokenType::CASE},         // ★
        {"default",  TokenType::DEFAULT},      // ★
        {"class",    TokenType::CLASS},
        {"super",    TokenType::SUPER},
    };

    static bool isContinuationToken(TokenType t) {
        switch (t) {
            // 二元运算符
        case TokenType::PLUS: case TokenType::MINUS:
        case TokenType::STAR: case TokenType::SLASH:
        case TokenType::CARET: case TokenType::BACKSLASH:
        case TokenType::PERCENT:
            // 赋值
        case TokenType::ASSIGN:
        case TokenType::PLUS_ASSIGN: case TokenType::MINUS_ASSIGN:
        case TokenType::STAR_ASSIGN: case TokenType::SLASH_ASSIGN:
        case TokenType::PERCENT_ASSIGN: case TokenType::CARET_ASSIGN:
            // 比较
        case TokenType::EQUAL: case TokenType::BANG_EQUAL:
        case TokenType::LESS: case TokenType::LESS_EQUAL:
        case TokenType::GREATER: case TokenType::GREATER_EQUAL:
            // 逻辑
        case TokenType::AND_AND: case TokenType::OR_OR:
            // 管道与箭头
        case TokenType::PIPE: case TokenType::ARROW:
            // 标点
        case TokenType::COMMA: case TokenType::DOT:
        case TokenType::COLON: case TokenType::QUESTION:
        case TokenType::SEMICOLON:
            // 开括号
        case TokenType::LPAREN: case TokenType::LBRACKET:
        case TokenType::LBRACE:
            // 已有 NEWLINE 不重复发射
        case TokenType::NEWLINE:
            return true;
        default:
            return false;
        }
    }

    Lexer::Lexer(std::string source) : source(std::move(source)) {}

    std::vector<Token> Lexer::tokenize() {
        while (!isAtEnd()) {
            start = current;
            scanToken();
        }
        tokens.emplace_back(TokenType::END_OF_FILE, "", current, line);
        return tokens;
    }

    void Lexer::scanToken() {
        char c = advance();
        switch (c) {
        case '(': addToken(TokenType::LPAREN); break;
        case ')': addToken(TokenType::RPAREN); break;
        case '[': addToken(TokenType::LBRACKET); break;
        case ']': addToken(TokenType::RBRACKET); break;
        case '{': addToken(TokenType::LBRACE); break;    // ★ 新增
        case '}': addToken(TokenType::RBRACE); break;    // ★ 新增
        case ',': addToken(TokenType::COMMA); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case '?': addToken(TokenType::QUESTION); break;    // ★
        case ':': addToken(TokenType::COLON); break;       // ★
        case '.':
            if (match('.') && match('.')) {
                addToken(TokenType::ELLIPSIS);
            }
            else {
                addToken(TokenType::DOT);
            }
            break;
        case '+':
            addToken(match('=') ? TokenType::PLUS_ASSIGN : TokenType::PLUS);
            break;
        case '-':
            addToken(match('=') ? TokenType::MINUS_ASSIGN : TokenType::MINUS);
            break;
        case '*':
            addToken(match('=') ? TokenType::STAR_ASSIGN : TokenType::STAR);
            break;
        case '/':
            if (match('/')) {
                // 单行注释
                while (!isAtEnd() && peek() != '\n') advance();
            }
            else if (match('=')) {
                addToken(TokenType::SLASH_ASSIGN);
            }
            else {
                addToken(TokenType::SLASH);
            }
            break;
        case '%':
            addToken(match('=') ? TokenType::PERCENT_ASSIGN : TokenType::PERCENT);
            break;
        case '^':
            addToken(match('=') ? TokenType::CARET_ASSIGN : TokenType::CARET);
            break;
        case '\\': addToken(TokenType::BACKSLASH); break;
        case '"': stringLiteral(); break;
        case '=':
            if (match('=')) addToken(TokenType::EQUAL);
            else if (match('>')) addToken(TokenType::ARROW);      // ★
            else addToken(TokenType::ASSIGN);
            break;
        case '!':
            if (match('=')) addToken(TokenType::BANG_EQUAL);
            else addToken(TokenType::BANG);           // ★ 不再报错，作为逻辑 NOT
            break;
        case '&':                                      // ★ 新增
            if (match('&')) addToken(TokenType::AND_AND);
            else throw std::runtime_error("Lexer Error: Expected '&&' at pos " + std::to_string(current));
            break;
        case '|':
            if (match('|')) addToken(TokenType::OR_OR);
            else if (match('>')) addToken(TokenType::PIPE);  // ★
            else throw std::runtime_error("Lexer Error: Expected '||' or '|>' at pos " + std::to_string(current));
            break;
        case '<':
            addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
            break;
        case '>':
            addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
            break;
        case ' ': case '\r': case '\t': 
            break;
        case '\n':
            line++;
            // ★ 智能换行符：当不在 () 或 [] 内部、且上一个 token 不是续行符时发射
            if (parenBracketDepth == 0 && !tokens.empty()) {
                TokenType lastType = tokens.back().type;
                if (!isContinuationToken(lastType)) {
                    tokens.emplace_back(TokenType::NEWLINE, "\\n", current - 1);
                }
            }
            break;
        default:
            if (std::isdigit(c)) { number(); }
            else if (c == 'f' && peek() == '"') {
                advance(); // consume opening "
                fstringLiteral();
            }
            else if (c == 'r' && peek() == '"') {  // ★ raw string
                advance(); // consume opening "
                rstringLiteral();
            }
            else if (std::isalpha(c) || c == '_') { identifier(); }
            else {
                throw std::runtime_error("Lexer Error: Unexpected character '" + std::string(1, c) + "' at pos " + std::to_string(current));
            }
            break;
        }
    }

    // ★ 修改：扫描完标识符后查关键字表
    void Lexer::identifier() {
        while (std::isalnum(peek()) || peek() == '_') {
            advance();
        }
        std::string text = source.substr(start, current - start);
        auto it = keywords.find(text);
        if (it != keywords.end()) {
            addToken(it->second);   // ★ 命中关键字
        }
        else {
            addToken(TokenType::IDENTIFIER);
        }
    }

    void Lexer::number() {
        while (std::isdigit(peek())) advance();
        if (peek() == '.' && std::isdigit(peekNext())) {
            advance();
            while (std::isdigit(peek())) advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            char next = peekNext();
            bool hasSign = (next == '+' || next == '-');
            bool isValidScientific = false;
            if (hasSign) { if (std::isdigit(peekNextNext())) isValidScientific = true; }
            else if (std::isdigit(next)) isValidScientific = true;
            if (isValidScientific) {
                advance();
                if (hasSign) advance();
                while (std::isdigit(peek())) advance();
            }
        }
        // ★ 虚数后缀: 3i, 3.14i, 1e3i
        if (peek() == 'i') {
            char next = peekNext();
            // i 后面必须是"非标识符延续字符"
            bool validEnd = (next == '\0' || next == ' ' || next == '\t' ||
                next == '+' || next == '-' || next == '*' || next == '/' ||
                next == '^' || next == '%' || next == '\\' ||
                next == ')' || next == ']' || next == '}' ||
                next == ',' || next == ';' || next == '\n' || next == '\r' ||
                next == '|' || next == '&' || next == '!' ||
                next == '?' || next == ':' || next == '=' ||
                next == '<' || next == '>' || next == '"');
            if (validEnd) {
                advance(); // consume 'i'
                addToken(TokenType::IMAGINARY);
            }
            else {
                addToken(TokenType::NUMBER);
            }
        }
        else {
            addToken(TokenType::NUMBER);
        }
    }

    bool Lexer::isAtEnd() const { return current >= (int)source.length(); }
    char Lexer::advance() { return source[current++]; }
    char Lexer::peek() const { if (isAtEnd()) return '\0'; return source[current]; }
    char Lexer::peekNext() const { if (current + 1 >= (int)source.length()) return '\0'; return source[current + 1]; }
    char Lexer::peekNextNext() const { if (current + 2 >= (int)source.length()) return '\0'; return source[current + 2]; }
    bool Lexer::match(char expected) { if (isAtEnd() || source[current] != expected) return false; current++; return true; }
    void Lexer::addToken(TokenType type) { tokens.emplace_back(type, source.substr(start, current - start), start, line); }

    void Lexer::stringLiteral() {
        std::string value;
        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') line++;
            char c = advance();
            if (c == '\\' && !isAtEnd()) {
                // ★ 转义序列处理
                char esc = advance();
                switch (esc) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '0':  value += '\0'; break;
                default:
                    // 不认识的转义原样保留
                    value += '\\';
                    value += esc;
                    break;
                }
            }
            else {
                value += c;
            }
        }
        if (isAtEnd()) {
            throw std::runtime_error("Lexer Error: Unterminated string at pos " + std::to_string(start));
        }
        advance(); // 吃掉右引号
        tokens.emplace_back(TokenType::STRING, value, start);
    }

    void Lexer::fstringLiteral() {
        std::string value;
        while (!isAtEnd() && peek() != '"') {
            char c = peek();
            if (c == '{') {
                // ★ 表达式段：追踪花括号深度
                value += advance(); // consume and include '{'
                int depth = 1;
                bool inStr = false;
                while (!isAtEnd() && depth > 0) {
                    c = peek();
                    if (inStr) {
                        // ★ 在表达式内的字符串中
                        if (c == '\\' && peekNext() == '"') {
                            // \" 在 inStr 中 → 结束字符串（剥离反斜杠）
                            advance(); // skip backslash
                            value += advance(); // add " (closes string for sub-lexer)
                            inStr = false;
                        }
                        else if (c == '\\' && !isAtEnd()) {
                            // 其他转义序列原样保留给子词法分析器处理
                            value += advance(); // add backslash
                            if (!isAtEnd()) value += advance(); // add escaped char
                        }
                        else if (c == '"') {
                            // 裸 " 也关闭字符串（兼容不带反斜杠的情况）
                            inStr = false;
                            value += advance();
                        }
                        else {
                            value += advance();
                        }
                    }
                    else {
                        // ★ 在表达式中，但不在字符串内
                        if (c == '\\' && peekNext() == '"') {
                            // \" 在表达式中 → 开始字符串（剥离反斜杠，传裸 " 给子词法分析器）
                            advance(); // skip backslash
                            value += advance(); // add " (starts string for sub-lexer)
                            inStr = true;
                        }
                        else if (c == '"') {
                            inStr = true;
                            value += advance();
                        }
                        else if (c == '{') { depth++; value += advance(); }
                        else if (c == '}') {
                            depth--;
                            value += advance(); // include '}' in value
                        }
                        else { value += advance(); }
                    }
                }
                if (depth != 0)
                    throw std::runtime_error("Lexer Error: Unmatched '{' in f-string at pos " + std::to_string(start));
            }
            else if (c == '\\') {
                // ★ 文本段的转义序列
                advance();
                if (isAtEnd()) break;
                char esc = advance();
                switch (esc) {
                case 'n':  value += '\n'; break;
                case 't':  value += '\t'; break;
                case 'r':  value += '\r'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '0':  value += '\0'; break;
                default:   value += '\\'; value += esc; break;
                }
            }
            else {
                if (c == '\n') line++;
                value += advance();
            }
        }
        if (isAtEnd())
            throw std::runtime_error("Lexer Error: Unterminated f-string at pos " + std::to_string(start));
        advance(); // consume closing "
        tokens.emplace_back(TokenType::FSTRING, value, start);
    }

    void Lexer::rstringLiteral() {
        // r" 已经被消费，current 指向 " 后面的第一个字符
        //
        // 两种模式：
        //   1. 简单模式:     r"content"         → 到下一个 " 为止
        //   2. 定界符模式:   r"TAG(content)TAG"  → 到 )TAG" 为止
        //
        // 判定：从当前位置向前探测，如果遇到的全是 [a-zA-Z0-9_] 然后紧跟 '('，
        //        就是定界符模式；否则就是简单模式。

        // ★ 探测阶段（不消费字符）
        int probePos = current;
        std::string delimiter;
        bool hasDelimiter = false;

        while (probePos < static_cast<int>(source.size())) {
            char c = source[probePos];
            if (c == '(') {
                // 找到开括号 → 确认为定界符模式
                hasDelimiter = true;
                break;
            }
            else if (std::isalnum(c) || c == '_') {
                delimiter += c;
                probePos++;
            }
            else {
                // 遇到其他字符（普通文本内容） → 不是定界符模式
                break;
            }
        }

        if (hasDelimiter) {
            // ═══ 定界符模式: r"TAG(content)TAG" ═══
            current = probePos + 1;  // 跳过 '('

            // 构造终止模式: )TAG"
            std::string endMarker = ")" + delimiter + "\"";
            size_t endLen = endMarker.size();

            std::string value;
            while (current < static_cast<int>(source.size())) {
                if (source[current] == '\n') line++;
                // 检查当前位置是否匹配终止模式
                if (source[current] == ')' &&
                    current + static_cast<int>(endLen) <= static_cast<int>(source.size())) {
                    bool match = true;
                    for (size_t k = 0; k < endLen; ++k) {
                        if (source[current + k] != endMarker[k]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        current += static_cast<int>(endLen);  // 消费 )TAG"
                        tokens.emplace_back(TokenType::RSTRING, value, start);
                        return;
                    }
                }
                value += source[current++];
            }
            throw std::runtime_error(
                "Lexer Error: Unterminated raw string (expected )" + delimiter + "\") at pos " +
                std::to_string(start));
        }
        else {
            // ═══ 简单模式: r"content" ═══
            std::string value;
            while (!isAtEnd() && peek() != '"') {
                if (peek() == '\n') line++;
                value += advance();
            }
            if (isAtEnd())
                throw std::runtime_error("Lexer Error: Unterminated raw string at pos " + std::to_string(start));
            advance();  // consume closing "
            tokens.emplace_back(TokenType::RSTRING, value, start);
        }
    }
} // namespace jc

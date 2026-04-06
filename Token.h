#ifndef JC2_TOKEN_H
#define JC2_TOKEN_H

#include <string>
#include <utility>

namespace jc {

    enum class TokenType {
        // --- 字面量与标识符 ---
        NUMBER, STRING, IDENTIFIER, FSTRING, RSTRING, IMAGINARY,           // ★ 3i, 2.5i, 1e3i

        // --- 基础数学运算符 ---
        PLUS, MINUS, STAR, SLASH, CARET, BACKSLASH, PERCENT,

        // --- 比较与赋值运算符 ---
        ASSIGN, EQUAL, BANG_EQUAL, GREATER, GREATER_EQUAL, LESS, LESS_EQUAL, ARROW, PIPE,

        // --- 复合赋值运算符 ---     // ★ 新增
        PLUS_ASSIGN,         // ★ +=
        MINUS_ASSIGN,        // ★ -=
        STAR_ASSIGN,         // ★ *=
        SLASH_ASSIGN,        // ★ /=
        PERCENT_ASSIGN,      // ★ %=
        CARET_ASSIGN,        // ★ ^=

        // --- 括号与标点符号 ---
        LPAREN, RPAREN, LBRACKET, RBRACKET,
        LBRACE, RBRACE,           // ★ 新增：{ }
        COMMA, SEMICOLON,
        QUESTION,            // ★ ?
        COLON,               // ★ :
        DOT,

        // --- 控制流关键字 ---     // ★ 新增
        IF, ELSE, WHILE, FOR, IN,
        BREAK, CONTINUE,
        RETURN,
        LOCAL, REF, GLOBAL,
        CONST,
        DELETE,
        THROW,               // ★ 新增
        TRY,                 // ★ 新增
        CATCH,               // ★ 新增
        IMPORT,
        SWITCH,              // ★
        CASE,                // ★
        DEFAULT,             // ★
        CLASS,
        SUPER,

        // --- 逻辑运算符 ---
        AND_AND,        // &&
        OR_OR,          // ||
        BANG,           // !  (单独的，不是 !=)


        // --- 特殊标记 ---
        END_OF_FILE, ERROR
    };

    struct Token {
        TokenType type;
        std::string lexeme;
        int position;
        Token(TokenType type, std::string lexeme, int position = 0)
            : type(type), lexeme(std::move(lexeme)), position(position) {
        }
    };

    inline std::string tokenTypeToString(TokenType type) {
        switch (type) {
        case TokenType::NUMBER:        return "NUMBER";
        case TokenType::IMAGINARY:     return "IMAGINARY";
        case TokenType::IDENTIFIER:    return "IDENTIFIER";
        case TokenType::STRING:        return "STRING";
        case TokenType::FSTRING:       return "FSTRING";
        case TokenType::RSTRING:       return "RSTRING";
        case TokenType::PLUS:          return "PLUS(+)";
        case TokenType::MINUS:         return "MINUS(-)";
        case TokenType::STAR:          return "STAR(*)";
        case TokenType::SLASH:         return "SLASH(/)";
        case TokenType::CARET:         return "CARET(^)";
        case TokenType::BACKSLASH:     return "BACKSLASH(\\)";
        case TokenType::PERCENT:       return "PERCENT(%)";
        case TokenType::ASSIGN:        return "ASSIGN(=)";
        case TokenType::EQUAL:         return "EQUAL(==)";
        case TokenType::ARROW:         return "ARROW(=>)";
        case TokenType::PIPE:          return "PIPE(|>)";
        case TokenType::BANG_EQUAL:    return "BANG_EQUAL(!=)";
        case TokenType::PLUS_ASSIGN:    return "PLUS_ASSIGN(+=)";
        case TokenType::MINUS_ASSIGN:   return "MINUS_ASSIGN(-=)";
        case TokenType::STAR_ASSIGN:    return "STAR_ASSIGN(*=)";
        case TokenType::SLASH_ASSIGN:   return "SLASH_ASSIGN(/=)";
        case TokenType::PERCENT_ASSIGN: return "PERCENT_ASSIGN(%=)";
        case TokenType::CARET_ASSIGN:   return "CARET_ASSIGN(^=)";
        case TokenType::AND_AND:       return "AND_AND(&&)";    // ★
        case TokenType::OR_OR:         return "OR_OR(||)";      // ★
        case TokenType::BANG:          return "BANG(!)";         // ★
        case TokenType::GREATER:       return "GREATER(>)";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL(>=)";
        case TokenType::LESS:          return "LESS(<)";
        case TokenType::LESS_EQUAL:    return "LESS_EQUAL(<=)";
        case TokenType::LPAREN:        return "LPAREN( ( )";
        case TokenType::RPAREN:        return "RPAREN( ) )";
        case TokenType::LBRACKET:      return "LBRACKET( [ )";
        case TokenType::RBRACKET:      return "RBRACKET( ] )";
        case TokenType::LBRACE:        return "LBRACE( { )";     // ★
        case TokenType::RBRACE:        return "RBRACE( } )";     // ★
        case TokenType::COMMA:         return "COMMA(,)";
        case TokenType::SEMICOLON:     return "SEMICOLON(;)";
        case TokenType::QUESTION:      return "QUESTION(?)";
        case TokenType::COLON:         return "COLON(:)";
        case TokenType::DOT:           return "DOT(.)";
        case TokenType::CLASS:         return "CLASS";
        case TokenType::SUPER:         return "SUPER";
        case TokenType::IF:            return "IF";               // ★
        case TokenType::ELSE:          return "ELSE";             // ★
        case TokenType::WHILE:         return "WHILE";            // ★
        case TokenType::FOR:           return "FOR";              // ★
        case TokenType::IN:            return "IN";
        case TokenType::BREAK:         return "BREAK";            // ★
        case TokenType::CONTINUE:      return "CONTINUE";         // ★
        case TokenType::RETURN:        return "RETURN";           // ★
        case TokenType::SWITCH:        return "SWITCH";
        case TokenType::CASE:          return "CASE";
        case TokenType::DEFAULT:       return "DEFAULT";
        case TokenType::LOCAL:         return "LOCAL";
        case TokenType::GLOBAL:        return "GLOBAL";
        case TokenType::IMPORT:        return "IMPORT";
        case TokenType::REF:           return "REF";
        case TokenType::CONST:         return "CONST";
        case TokenType::DELETE:        return "DELETE";
        case TokenType::END_OF_FILE:   return "EOF";
        case TokenType::ERROR:         return "ERROR";
        case TokenType::THROW:         return "THROW";
        case TokenType::TRY:           return "TRY";
        case TokenType::CATCH:         return "CATCH";
        default:                       return "UNKNOWN";
        }
    }

} // namespace jc
#endif // JC2_TOKEN_H

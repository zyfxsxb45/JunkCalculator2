#include "Lexer.h"  // ★ f-string 子解析需要
#include "Parser.h"

namespace jc {

    std::unique_ptr<Expr> Parser::expression() {
        return assignment();
    }

    std::unique_ptr<Expr> Parser::pipe() {
        auto expr = logicalOr();
        while (match({ TokenType::PIPE })) {
            Token op = previous();
            auto right = logicalOr();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::logicalOr() {
        auto expr = logicalAnd();
        while (match({ TokenType::OR_OR })) {
            Token op = previous();
            auto right = logicalAnd();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::logicalAnd() {
        auto expr = comparison();
        while (match({ TokenType::AND_AND })) {
            Token op = previous();
            auto right = comparison();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::parse() {
        try {
            std::vector<std::unique_ptr<Expr>> stmts;

            // ★ 循环解析，直到吃完所有的 Token (遇到 EOF)
            while (!isAtEnd()) {
                // 跳过可能开头的多余分号 (如 ;;; a=1)
                while (match({ TokenType::SEMICOLON })) {}

                if (isAtEnd()) break;

                stmts.push_back(expression());

                // 每条语句结束后，允许并吃掉任意多个分号分隔符
                while (match({ TokenType::SEMICOLON })) {}
            }

            // 如果整个输入解析出来是个空，塞个 0 进去防崩
            if (stmts.empty()) {
                return std::make_unique<Literal>("0");
            }

            // ★ 优化：如果只有一条语句，直接拔出来返回
            // 这样如果是单行普通计算，AST 就不会徒增不必要的 Block 节点
            if (stmts.size() == 1) {
                return std::move(stmts[0]);
            }

            // ★ 否则，把它打包成一个隐式的系统级大代码块！
            // Block 默认行为就是挨个执行，最后返回最末尾那条语句的值
            return std::make_unique<Block>(std::move(stmts));
        }
        catch (const std::exception&) {
            throw;
        }
    }

    std::unique_ptr<Expr> Parser::ternary() {
        auto expr = pipe();  // ★

        if (match({ TokenType::QUESTION })) {
            // cond ? thenExpr : elseExpr
            // 右结合：a ? b : c ? d : e  →  a ? b : (c ? d : e)
            auto thenBranch = ternary();    // 允许嵌套 ternary
            consume(TokenType::COLON, "Parser Error: Expect ':' in ternary expression.");
            auto elseBranch = ternary();    // 右结合递归

            // ★ 直接复用 IfExpr，零 Evaluator 改动
            expr = std::make_unique<IfExpr>(
                std::move(expr),
                std::move(thenBranch),
                std::move(elseBranch));
        }

        return expr;
    }

    std::unique_ptr<Expr> Parser::assignment() {
        auto expr = ternary();

        if (match({ TokenType::PLUS_ASSIGN, TokenType::MINUS_ASSIGN,
                    TokenType::STAR_ASSIGN, TokenType::SLASH_ASSIGN,
                    TokenType::PERCENT_ASSIGN, TokenType::CARET_ASSIGN })) {
            Token compOp = previous();
            // 映射到基础运算符
            TokenType baseOp;
            switch (compOp.type) {
            case TokenType::PLUS_ASSIGN:    baseOp = TokenType::PLUS; break;
            case TokenType::MINUS_ASSIGN:   baseOp = TokenType::MINUS; break;
            case TokenType::STAR_ASSIGN:    baseOp = TokenType::STAR; break;
            case TokenType::SLASH_ASSIGN:   baseOp = TokenType::SLASH; break;
            case TokenType::PERCENT_ASSIGN: baseOp = TokenType::PERCENT; break;
            case TokenType::CARET_ASSIGN:   baseOp = TokenType::CARET; break;
            default: baseOp = TokenType::PLUS; break;
            }
            auto value = assignment();
            return std::make_unique<CompoundAssign>(std::move(expr), baseOp, std::move(value));
        }

        if (match({ TokenType::ASSIGN })) {
            Token equals = previous();

            int valueStartTokenIndex = current;
            auto value = assignment();
            int valueEndTokenIndex = current;

            if (auto* dotExpr = dynamic_cast<DotAccess*>(expr.get())) {
                return std::make_unique<DotAssign>(
                    std::move(dotExpr->object),
                    std::move(dotExpr->field),
                    std::move(value));
            }

            // ★ 索引赋值：A[i] = v  或  A[i][j] = v  (任意深度)
            if (auto* indexExpr = dynamic_cast<IndexAccess*>(expr.get())) {
                // 递归展开 IndexAccess 链
                std::vector<std::vector<std::unique_ptr<Expr>>> chain;
                IndexAccess* currentIA = indexExpr;
                chain.push_back(std::move(currentIA->indices));
                while (auto* inner = dynamic_cast<IndexAccess*>(currentIA->object.get())) {
                    chain.push_back(std::move(inner->indices));
                    currentIA = inner;
                }
                // currentIA->object 现在是链的根（Variable 或 DotAccess 等）
                std::reverse(chain.begin(), chain.end());
                auto* varExpr = dynamic_cast<Variable*>(currentIA->object.get());
                if (varExpr) {
                    // ★ 原路径：根是变量
                    return std::make_unique<IndexAssign>(
                        varExpr->name, std::move(chain), std::move(value));
                }
                else {
                    // ★ 新路径：根是表达式（如 self.data, d.list 等）
                    return std::make_unique<IndexAssign>(
                        std::move(currentIA->object), std::move(chain), std::move(value));
                }
            }

            // ★ 解构赋值: [a, b, c] = expr
            if (auto* matNode = dynamic_cast<MatrixNode*>(expr.get())) {
                if (matNode->elements.size() == 1) {
                    std::vector<Token> names;
                    bool validDestruct = true;
                    for (auto& elem : matNode->elements[0]) {
                        if (auto* v = dynamic_cast<Variable*>(elem.get())) {
                            names.push_back(v->name);
                        }
                        else {
                            validDestruct = false;
                            break;
                        }
                    }
                    if (validDestruct && !names.empty()) {
                        return std::make_unique<DestructAssign>(
                            std::move(names), std::move(value));
                    }
                }
            }

            // 情况 1: 普通的变量赋值
            if (auto* varExpr = dynamic_cast<Variable*>(expr.get())) {
                return std::make_unique<Assign>(varExpr->name, std::move(value));
            }
            // 情况 2: 函数定义
            if (auto* callExpr = dynamic_cast<Call*>(expr.get())) {
                std::vector<Token> params;
                std::vector<bool> paramIsRef;
                std::vector<std::shared_ptr<Expr>> defaultExprs;  // ★

                for (auto& argExpr : callExpr->arguments) {
                    if (auto* refParam = dynamic_cast<RefParam*>(argExpr.get())) {
                        params.push_back(refParam->name);
                        paramIsRef.push_back(true);
                        defaultExprs.push_back(nullptr);
                    }
                    // ★ param = defaultValue → Assign 节点
                    else if (auto* assignExpr = dynamic_cast<Assign*>(argExpr.get())) {
                        params.push_back(assignExpr->name);
                        paramIsRef.push_back(false);
                        defaultExprs.push_back(std::shared_ptr<Expr>(assignExpr->value.release()));
                    }
                    else if (auto* varExpr = dynamic_cast<Variable*>(argExpr.get())) {
                        params.push_back(varExpr->name);
                        paramIsRef.push_back(false);
                        defaultExprs.push_back(nullptr);
                    }
                    else {
                        throw std::runtime_error("Parser Error: Function parameters must be simple variable names (optionally prefixed with 'ref'), or name = default.");
                    }
                }

                std::string rawBodyStr = "";
                for (int i = valueStartTokenIndex; i < valueEndTokenIndex; ++i) {
                    if (tokens[i].type == TokenType::STRING) rawBodyStr += "\"" + tokens[i].lexeme + "\"";
                    else rawBodyStr += tokens[i].lexeme;
                    if (i < valueEndTokenIndex - 1) rawBodyStr += " ";
                }

                return std::make_unique<FunctionDef>(
                    callExpr->callee,
                    params,
                    paramIsRef,
                    defaultExprs,     // ★
                    rawBodyStr,
                    std::shared_ptr<Expr>(value.release())
                );
            }
            throw std::runtime_error("Parser Error: Invalid assignment target at '" + equals.lexeme + "'.");
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::comparison() {
        auto expr = addition();
        while (match({ TokenType::EQUAL, TokenType::BANG_EQUAL,
                       TokenType::LESS, TokenType::LESS_EQUAL,
                       TokenType::GREATER, TokenType::GREATER_EQUAL,
                       TokenType::IN })) {                           // ★ 新增
            Token op = previous();
            auto right = addition();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::addition() {
        auto expr = multiplication();
        while (match({ TokenType::PLUS, TokenType::MINUS })) {
            Token op = previous();
            auto right = multiplication();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::multiplication() {
        auto expr = unary();
        while (match({ TokenType::STAR, TokenType::SLASH, TokenType::PERCENT, TokenType::BACKSLASH })) {
            Token op = previous();
            auto right = unary();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::unary() {
        if (match({ TokenType::PLUS, TokenType::MINUS, TokenType::BANG })) {  // ★ 加 BANG
            Token op = previous();
            auto right = unary();
            return std::make_unique<Unary>(op, std::move(right));
        }
        return power();
    }

    std::unique_ptr<Expr> Parser::power() {
        auto expr = call();
        if (match({ TokenType::CARET })) {
            Token op = previous();
            auto right = unary();
            expr = std::make_unique<Binary>(std::move(expr), op, std::move(right));
        }
        return expr;
    }

    std::unique_ptr<Expr> Parser::call() {
        auto expr = primary();
        while (true) {
            if (match({ TokenType::LPAREN })) {
                std::vector<std::unique_ptr<Expr>> args;
                if (!check(TokenType::RPAREN)) {
                    do { args.push_back(assignment()); } while (match({ TokenType::COMMA }));
                }
                consume(TokenType::RPAREN, "Parser Error: Expect ')' after arguments.");

                if (auto* varExpr = dynamic_cast<Variable*>(expr.get())) {
                    // ★ 标识符调用：sin(x), f(1, 2) 等
                    expr = std::make_unique<Call>(varExpr->name, std::move(args));
                }
                else {
                    // ★ 表达式调用：(() => 42)(), getLambda()(x) 等
                    expr = std::make_unique<InvokeExpr>(std::move(expr), std::move(args));
                }
            }
            else if (match({ TokenType::LBRACKET })) {
                std::vector<std::unique_ptr<Expr>> indices;

                auto parseSliceArg = [this]() -> std::unique_ptr<Expr> {
                    std::unique_ptr<Expr> st, en, sp;
                    bool isSl = false;
                    // 读取 start (如果有)
                    if (!check(TokenType::COLON) && !check(TokenType::COMMA) && !check(TokenType::RBRACKET)) {
                        st = expression();
                    }
                    // 检测是否被切开
                    if (match({ TokenType::COLON })) {
                        isSl = true;
                        // 读取 end (如果有)
                        if (!check(TokenType::COLON) && !check(TokenType::COMMA) && !check(TokenType::RBRACKET)) {
                            en = expression();
                        }
                        // 读取 step (如果有)
                        if (match({ TokenType::COLON })) {
                            if (!check(TokenType::COMMA) && !check(TokenType::RBRACKET)) {
                                sp = expression();
                            }
                        }
                    }
                    if (isSl) return std::make_unique<SliceExpr>(std::move(st), std::move(en), std::move(sp));
                    return st;
                    };

                indices.push_back(parseSliceArg());
                if (match({ TokenType::COMMA })) {
                    indices.push_back(parseSliceArg());
                }
                consume(TokenType::RBRACKET, "Parser Error: Expect ']' after index.");
                expr = std::make_unique<IndexAccess>(std::move(expr), std::move(indices));
            }
            else if (match({ TokenType::DOT })) {
                Token field = consume(TokenType::IDENTIFIER,
                    "Parser Error: Expect field/method name after '.'.");
                if (match({ TokenType::LPAREN })) {
                    // obj.method(args)
                    std::vector<std::unique_ptr<Expr>> args;
                    if (!check(TokenType::RPAREN)) {
                        do { args.push_back(assignment()); } while (match({ TokenType::COMMA }));
                    }
                    consume(TokenType::RPAREN, "Parser Error: Expect ')' after method arguments.");
                    expr = std::make_unique<MethodCallExpr>(std::move(expr), field, std::move(args));
                }
                else {
                    // obj.field
                    expr = std::make_unique<DotAccess>(std::move(expr), field);
                }
            }
            else break;
        }
        return expr;
    }

    // =================================================================
    // ★ 新增：块 { stmt1; stmt2; ... }
    // =================================================================
    std::unique_ptr<Expr> Parser::parseBlock() {
        consume(TokenType::LBRACE, "Parser Error: Expect '{'.");
        std::vector<std::unique_ptr<Expr>> stmts;
        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            stmts.push_back(expression());
            // ★ 修复：分号作为可选分隔符，吃掉所有连续的分号
            //   这样 for(){} result 和 for(){}; result 都合法
            while (match({ TokenType::SEMICOLON })) {}
        }
        consume(TokenType::RBRACE, "Parser Error: Expect '}' after block.");
        return std::make_unique<Block>(std::move(stmts));
    }

    // =================================================================
    // ★ 新增：if (cond) { ... } else { ... }
    // =================================================================
    std::unique_ptr<Expr> Parser::ifExpr() {
        consume(TokenType::LPAREN, "Parser Error: Expect '(' after 'if'.");
        auto condition = expression();
        consume(TokenType::RPAREN, "Parser Error: Expect ')' after if condition.");
        auto thenBranch = parseBlock();

        std::unique_ptr<Expr> elseBranch = nullptr;
        if (match({ TokenType::ELSE })) {
            if (check(TokenType::IF)) {
                // else if 链：递归解析下一个 if
                advance(); // 消费 'if'
                elseBranch = ifExpr();
            }
            else {
                elseBranch = parseBlock();
            }
        }
        return std::make_unique<IfExpr>(std::move(condition), std::move(thenBranch), std::move(elseBranch));
    }

    // =================================================================
    // ★ 新增：while (cond) { ... }
    // =================================================================
    std::unique_ptr<Expr> Parser::whileExpr() {
        consume(TokenType::LPAREN, "Parser Error: Expect '(' after 'while'.");
        auto condition = expression();
        consume(TokenType::RPAREN, "Parser Error: Expect ')' after while condition.");
        auto body = parseBlock();
        return std::make_unique<WhileExpr>(std::move(condition), std::move(body));
    }

    // =================================================================
    // ★ 新增：for (init; cond; update) { ... }
    // =================================================================
    std::unique_ptr<Expr> Parser::forExpr() {
        consume(TokenType::LPAREN, "Parser Error: Expect '(' after 'for'.");

        // ★ 解构 for-in: for ([a, b, ...] in iterable)
        if (check(TokenType::LBRACKET)) {
            int savedPos = current;
            advance(); // consume [
            std::vector<Token> names;
            bool valid = true;

            if (check(TokenType::IDENTIFIER)) {
                names.push_back(advance());
                while (match({ TokenType::COMMA })) {
                    if (!check(TokenType::IDENTIFIER)) { valid = false; break; }
                    names.push_back(advance());
                }
            }
            else {
                valid = false;
            }

            if (valid && !names.empty() && match({ TokenType::RBRACKET })) {
                if (check(TokenType::IN)) {
                    advance(); // consume 'in'
                    auto iterable = expression();
                    consume(TokenType::RPAREN,
                        "Parser Error: Expect ')' after for-in iterable.");
                    auto body = parseBlock();
                    return std::make_unique<ForInExpr>(
                        std::move(names), std::move(iterable), std::move(body));
                }
            }
            // 不是解构 for-in，回退
            current = savedPos;
        }

        // ★ 推测性检查：for (IDENTIFIER in ...) 还是 for (init; cond; update)
        if (check(TokenType::IDENTIFIER) &&
            current + 1 < static_cast<int>(tokens.size()) &&
            tokens[current + 1].type == TokenType::IN) {
            Token varName = advance(); // 消费标识符
            advance();                  // 消费 'in'
            return forInExpr(varName);
        }

        // 传统三段式 for
        auto init = expression();
        consume(TokenType::SEMICOLON, "Parser Error: Expect ';' after for-initializer.");
        auto cond = expression();
        consume(TokenType::SEMICOLON, "Parser Error: Expect ';' after for-condition.");
        auto update = expression();
        consume(TokenType::RPAREN, "Parser Error: Expect ')' after for-clauses.");
        auto body = parseBlock();
        return std::make_unique<ForExpr>(std::move(init), std::move(cond), std::move(update), std::move(body));
    }

    std::unique_ptr<Expr> Parser::forInExpr(Token varName) {
        auto iterable = expression();
        consume(TokenType::RPAREN, "Parser Error: Expect ')' after for-in iterable.");
        auto body = parseBlock();
        return std::make_unique<ForInExpr>(std::move(varName), std::move(iterable), std::move(body));
    }

    // =================================================================
    // primary — ★ 新增 if / while / for / block / break / continue
    // =================================================================
    std::unique_ptr<Expr> Parser::primary() {
        // ★ 统一拦截：任何关键字后面紧跟 = 都是误用
        auto isKeyword = [](TokenType t) {
            return t == TokenType::IF || t == TokenType::ELSE ||
                t == TokenType::WHILE || t == TokenType::FOR ||
                t == TokenType::BREAK || t == TokenType::CONTINUE ||
                t == TokenType::RETURN || t == TokenType::LOCAL ||
                t == TokenType::CONST || t == TokenType::DELETE ||
                t == TokenType::GLOBAL || t == TokenType::IN ||
                t == TokenType::THROW || t == TokenType::TRY ||  // ★
                t == TokenType::CATCH || t == TokenType::REF ||   // ★
                t == TokenType::IMPORT || t == TokenType::SWITCH ||  // ★
                t == TokenType::CASE || t == TokenType::DEFAULT ||
                t == TokenType::SUPER || t == TokenType::CLASS;
            };
        if (isKeyword(peek().type) && current + 1 < static_cast<int>(tokens.size())
            && tokens[current + 1].type == TokenType::ASSIGN) {
            throw std::runtime_error("Syntax Error: '" + peek().lexeme +
                "' is a reserved keyword and cannot be used as a variable name.");
        }


        if (match({ TokenType::NUMBER }))     return std::make_unique<Literal>(previous().lexeme);
        if (match({ TokenType::IMAGINARY })) {  // ★
            std::string numStr = previous().lexeme;
            numStr.pop_back();  // 去掉尾部 'i'
            return std::make_unique<Literal>(numStr, false, true);
        }
        if (match({ TokenType::FSTRING }))    return parseFString(previous().lexeme);  // ★
        if (match({ TokenType::RSTRING })) return std::make_unique<Literal>(previous().lexeme, true);  // ★
        if (match({ TokenType::STRING }))     return std::make_unique<Literal>(previous().lexeme, true);
        if (match({ TokenType::IDENTIFIER })) return std::make_unique<Variable>(previous());
        if (match({ TokenType::GLOBAL })) {
            std::vector<Token> names;
            names.push_back(consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after 'global'."));
            while (match({ TokenType::COMMA })) {
                names.push_back(consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after ','."));
            }
            return std::make_unique<GlobalDecl>(std::move(names));
        }

        // ★ 控制流关键字
        if (match({ TokenType::SUPER }))    return std::make_unique<SuperExpr>();
        if (match({ TokenType::CLASS }))    return classDefExpr();
        if (match({ TokenType::IF }))       return ifExpr();
        if (match({ TokenType::WHILE }))    return whileExpr();
        if (match({ TokenType::FOR }))      return forExpr();
        if (match({ TokenType::BREAK }))    return std::make_unique<BreakExpr>();
        if (match({ TokenType::CONTINUE })) return std::make_unique<ContinueExpr>();
        if (match({ TokenType::RETURN })) {
            // return 后面可以跟表达式，也可以什么都不跟（裸 return）
            // 裸 return 的判定：下一个 token 是 } 或 ; 或 EOF
            std::unique_ptr<Expr> value = nullptr;
            if (!check(TokenType::RBRACE) &&
                !check(TokenType::SEMICOLON) &&
                !check(TokenType::END_OF_FILE)) {
                value = expression();
            }
            return std::make_unique<ReturnExpr>(std::move(value));
        }
        if (match({ TokenType::THROW })) {
            auto value = expression();
            return std::make_unique<ThrowExpr>(std::move(value));
        }
        if (match({ TokenType::TRY })) {
            auto tryBody = parseBlock();
            consume(TokenType::CATCH, "Parser Error: Expect 'catch' after try block.");
            consume(TokenType::LPAREN, "Parser Error: Expect '(' after 'catch'.");
            Token catchName = consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name in catch.");
            consume(TokenType::RPAREN, "Parser Error: Expect ')' after catch variable.");
            auto catchBody = parseBlock();
            return std::make_unique<TryCatchExpr>(std::move(tryBody), catchName, std::move(catchBody));
        }
        if (match({ TokenType::IMPORT })) {
            auto path = expression();
            return std::make_unique<ImportExpr>(std::move(path));
        }
        if (match({ TokenType::SWITCH })) return switchExpr();
        if (match({ TokenType::LOCAL })) {
            throw std::runtime_error(
                "Syntax Error: 'local' has been removed. "
                "Variables inside functions are now auto-local. "
                "Use 'global x' to modify outer variables.");
        }
        if (match({ TokenType::CONST })) {
            Token name = consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after 'const'.");
            consume(TokenType::ASSIGN, "Parser Error: 'const' declaration requires '= value'.");
            auto value = expression();
            return std::make_unique<ConstDecl>(name, std::move(value));
        }
        if (match({ TokenType::REF })) {
            Token name = consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after 'ref'.");
            return std::make_unique<RefParam>(name);
        }
        if (match({ TokenType::DELETE })) {
            std::vector<Token> names;
            names.push_back(consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after 'delete'."));
            while (match({ TokenType::COMMA })) {
                names.push_back(consume(TokenType::IDENTIFIER, "Parser Error: Expect variable name after ','."));
            }
            return std::make_unique<DeleteExpr>(std::move(names));
        }

        // ★ 裸块 { ... } 或字典字面量 { key: value, ... }
        if (check(TokenType::LBRACE)) {
            // 快速前瞻：{ 后面是 (IDENTIFIER|STRING|NUMBER) 紧跟 COLON → 字典
            if (current + 2 < static_cast<int>(tokens.size())) {
                TokenType first = tokens[current + 1].type;
                TokenType second = tokens[current + 2].type;
                if (second == TokenType::COLON &&
                    (first == TokenType::IDENTIFIER || first == TokenType::STRING ||
                        first == TokenType::NUMBER || first == TokenType::FSTRING ||
                        first == TokenType::RSTRING || first == TokenType::IMAGINARY)) {
                    return parseDictLiteral();
                }
            }
            return parseBlock();
        }

        if (match({ TokenType::LPAREN })) {
            // ★ 推测性 lambda 解析：(params) => body
            int savedPos = current;
            std::vector<Token> lambdaParams;
            bool validParams = true;

            // --- 推测阶段：跳过标识符和可选的 = defaultExpr ---
            auto skipDefault = [&]() {
                if (current < static_cast<int>(tokens.size()) &&
                    tokens[current].type == TokenType::ASSIGN) {
                    current++; // skip =
                    int depth = 0;
                    while (current < static_cast<int>(tokens.size())) {
                        auto tt = tokens[current].type;
                        if (tt == TokenType::LPAREN || tt == TokenType::LBRACKET ||
                            tt == TokenType::LBRACE) {
                            depth++; current++;
                        }
                        else if (tt == TokenType::RPAREN || tt == TokenType::RBRACKET ||
                            tt == TokenType::RBRACE) {
                            if (depth == 0) break;
                            depth--; current++;
                        }
                        else if (depth == 0 && tt == TokenType::COMMA) break;
                        else current++;
                    }
                }
                };

            if (check(TokenType::RPAREN)) {
                // () => expr — zero params
            }
            else if (check(TokenType::IDENTIFIER)) {
                lambdaParams.push_back(tokens[current]); current++;
                skipDefault();  // ★
                while (current < static_cast<int>(tokens.size()) &&
                    tokens[current].type == TokenType::COMMA) {
                    current++; // skip comma
                    if (current >= static_cast<int>(tokens.size()) ||
                        tokens[current].type != TokenType::IDENTIFIER) {
                        validParams = false;
                        break;
                    }
                    lambdaParams.push_back(tokens[current]); current++;
                    skipDefault();  // ★
                }
            }
            else {
                validParams = false;
            }

            // 检查 ) =>
            bool isLambda = false;
            if (validParams &&
                current < static_cast<int>(tokens.size()) &&
                tokens[current].type == TokenType::RPAREN) {
                current++; // skip )
                if (current < static_cast<int>(tokens.size()) &&
                    tokens[current].type == TokenType::ARROW) {
                    current++; // skip =>
                    isLambda = true;
                }
            }

            if (isLambda) {
                // ★ 回溯并用完整解析器重新解析参数（带默认值）
                current = savedPos;
                lambdaParams.clear();
                std::vector<std::shared_ptr<Expr>> lambdaDefaults;

                if (!check(TokenType::RPAREN)) {
                    do {
                        Token param = consume(TokenType::IDENTIFIER,
                            "Parser Error: Expect parameter name in lambda.");
                        lambdaParams.push_back(param);
                        if (match({ TokenType::ASSIGN })) {
                            auto defExpr = ternary();
                            lambdaDefaults.push_back(
                                std::shared_ptr<Expr>(defExpr.release()));
                        }
                        else {
                            lambdaDefaults.push_back(nullptr);
                        }
                    } while (match({ TokenType::COMMA }));
                }

                consume(TokenType::RPAREN, "Parser Error: Expect ')' after lambda parameters.");
                consume(TokenType::ARROW, "Parser Error: Expect '=>' for lambda.");

                int bodyStart = current;
                auto body = check(TokenType::LBRACE) ? parseBlock() : expression();
                int bodyEnd = current;

                std::string rawBody;
                for (int ii = bodyStart; ii < bodyEnd; ++ii) {
                    if (tokens[ii].type == TokenType::STRING)
                        rawBody += "\"" + tokens[ii].lexeme + "\"";
                    else
                        rawBody += tokens[ii].lexeme;
                    if (ii < bodyEnd - 1) rawBody += " ";
                }

                return std::make_unique<LambdaExpr>(
                    std::move(lambdaParams),
                    std::move(lambdaDefaults),  // ★
                    rawBody,
                    std::shared_ptr<Expr>(body.release()));
            }
            else {
                // 不是 lambda，回退
                current = savedPos;
                auto expr = expression();
                consume(TokenType::RPAREN, "Parser Error: Expect ')' after expression.");
                return expr;
            }
        }

        // ★ 矩阵 [...] 或列表推导式 [expr for x in ...]
        if (match({ TokenType::LBRACKET })) {
            std::vector<std::vector<std::unique_ptr<Expr>>> matrixElements;
            std::vector<std::unique_ptr<Expr>> currentRow;

            if (!check(TokenType::RBRACKET)) {
                // ★ 先解析第一个元素
                currentRow.push_back(expression());

                // ★ 检测列表推导式：[expr for x in ...]
                if (check(TokenType::FOR)) {
                    auto valueExpr = std::move(currentRow[0]);
                    return parseListComp(std::move(valueExpr));
                }

                // ★ 非推导式 → 继续解析矩阵
                if (match({ TokenType::COMMA })) {
                    do {
                        if (check(TokenType::SEMICOLON) || check(TokenType::RBRACKET)) break;
                        currentRow.push_back(expression());
                    } while (match({ TokenType::COMMA }));
                }

                if (match({ TokenType::SEMICOLON })) {
                    matrixElements.push_back(std::move(currentRow));
                    currentRow.clear();
                    while (!check(TokenType::RBRACKET) && !isAtEnd()) {
                        if (check(TokenType::SEMICOLON)) { advance(); continue; }
                        currentRow.push_back(expression());
                        if (match({ TokenType::COMMA })) continue;
                        else if (match({ TokenType::SEMICOLON })) {
                            matrixElements.push_back(std::move(currentRow));
                            currentRow.clear();
                        }
                        else if (!check(TokenType::RBRACKET)) {
                            throw std::runtime_error("Parser Error: Expect ',' or ';' or ']' inside matrix.");
                        }
                    }
                }

                if (!currentRow.empty()) matrixElements.push_back(std::move(currentRow));
            }

            consume(TokenType::RBRACKET, "Parser Error: Expect ']' after matrix structure.");
            if (!matrixElements.empty()) {
                size_t cols = matrixElements[0].size();
                for (const auto& row : matrixElements)
                    if (row.size() != cols)
                        throw std::runtime_error("Parser Error: Matrix rows must have the same number of columns.");
            }
            return std::make_unique<MatrixNode>(std::move(matrixElements));
        }
        throw std::runtime_error("Parser Error: Expect expression at '" + peek().lexeme + "'.");
    }

    std::unique_ptr<Expr> Parser::switchExpr() {
        consume(TokenType::LPAREN, "Parser Error: Expect '(' after 'switch'.");
        auto subject = expression();
        consume(TokenType::RPAREN, "Parser Error: Expect ')' after switch expression.");
        consume(TokenType::LBRACE, "Parser Error: Expect '{' to open switch body.");

        std::vector<std::pair<std::vector<std::unique_ptr<Expr>>, std::unique_ptr<Expr>>> cases;
        std::unique_ptr<Expr> defaultBody = nullptr;

        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            if (match({ TokenType::CASE })) {
                // 解析一到多个匹配值：case v1, v2, v3:
                std::vector<std::unique_ptr<Expr>> values;
                values.push_back(expression());
                while (match({ TokenType::COMMA })) {
                    values.push_back(expression());
                }
                consume(TokenType::COLON, "Parser Error: Expect ':' after case value(s).");
                auto body = parseBlock();
                cases.push_back({ std::move(values), std::move(body) });

                // 吃掉可选的分号
                while (match({ TokenType::SEMICOLON })) {}
            }
            else if (match({ TokenType::DEFAULT })) {
                consume(TokenType::COLON, "Parser Error: Expect ':' after 'default'.");
                defaultBody = parseBlock();
                while (match({ TokenType::SEMICOLON })) {}
            }
            else {
                throw std::runtime_error("Parser Error: Expect 'case' or 'default' inside switch.");
            }
        }

        consume(TokenType::RBRACE, "Parser Error: Expect '}' to close switch body.");
        return std::make_unique<SwitchExpr>(std::move(subject), std::move(cases), std::move(defaultBody));
    }

    // ★ 新增：整个方法
    std::unique_ptr<Expr> Parser::classDefExpr() {
        Token name = consume(TokenType::IDENTIFIER, "Parser Error: Expect class name after 'class'.");

        // ★ 可选继承：class Child extends Parent { ... }
        std::string superClassName;
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "extends") {
            advance(); // consume 'extends'
            Token superToken = consume(TokenType::IDENTIFIER,
                "Parser Error: Expect parent class name after 'extends'.");
            superClassName = superToken.lexeme;
        }

        consume(TokenType::LBRACE, "Parser Error: Expect '{' after class name.");

        std::vector<ClassDefExpr::MethodDef> methods;

        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            while (match({ TokenType::SEMICOLON })) {}
            if (check(TokenType::RBRACE)) break;

            Token methodName = consume(TokenType::IDENTIFIER,
                "Parser Error: Expect method name inside class.");
            consume(TokenType::LPAREN,
                "Parser Error: Expect '(' after method name '" + methodName.lexeme + "'.");

            std::vector<Token> params;
            std::vector<bool> paramIsRef;
            std::vector<std::shared_ptr<Expr>> defaultExprs;
            if (!check(TokenType::RPAREN)) {
                do {
                    bool isRef = false;
                    if (match({ TokenType::REF })) isRef = true;
                    Token param = consume(TokenType::IDENTIFIER,
                        "Parser Error: Expect parameter name.");
                    params.push_back(param);
                    paramIsRef.push_back(isRef);
                    if (match({ TokenType::ASSIGN })) {
                        auto defExpr = ternary();
                        defaultExprs.push_back(std::shared_ptr<Expr>(defExpr.release()));
                    }
                    else {
                        defaultExprs.push_back(nullptr);
                    }
                } while (match({ TokenType::COMMA }));
            }
            consume(TokenType::RPAREN,
                "Parser Error: Expect ')' after method parameters.");
            consume(TokenType::ASSIGN,
                "Parser Error: Expect '=' after method '" + methodName.lexeme + "' parameters.");

            int bodyStart = current;
            auto body = check(TokenType::LBRACE) ? parseBlock() : expression();
            int bodyEnd = current;

            std::string rawBody;
            for (int i = bodyStart; i < bodyEnd; ++i) {
                if (tokens[i].type == TokenType::STRING)
                    rawBody += "\"" + tokens[i].lexeme + "\"";
                else
                    rawBody += tokens[i].lexeme;
                if (i < bodyEnd - 1) rawBody += " ";
            }

            methods.push_back(ClassDefExpr::MethodDef{
                methodName,
                std::move(params),
                std::move(paramIsRef),
                std::move(defaultExprs),
                std::move(rawBody),
                std::shared_ptr<Expr>(body.release())
                });

            while (match({ TokenType::SEMICOLON })) {}
        }

        consume(TokenType::RBRACE, "Parser Error: Expect '}' after class body.");
        return std::make_unique<ClassDefExpr>(name, std::move(superClassName), std::move(methods));
    }

    std::unique_ptr<Expr> Parser::parseFString(const std::string& raw) {
        std::vector<std::string> literals;
        std::vector<std::unique_ptr<Expr>> exprs;
        std::vector<std::string> specs;

        std::string currentLit;
        size_t i = 0;

        while (i < raw.size()) {
            if (raw[i] == '{') {
                i++; // skip opening {

                // ★ 提取到匹配的 } 之间的原始内容
                std::string exprStr;
                int depth = 1;
                bool inStr = false;
                while (i < raw.size() && depth > 0) {
                    char c = raw[i];
                    if (inStr) {
                        if (c == '\\' && i + 1 < raw.size()) { exprStr += c; i++; c = raw[i]; }
                        else if (c == '"') inStr = false;
                        exprStr += c; i++;
                    }
                    else {
                        if (c == '"') { inStr = true; exprStr += c; i++; }
                        else if (c == '{') { depth++; exprStr += c; i++; }
                        else if (c == '}') { depth--; if (depth > 0) exprStr += c; i++; }
                        else { exprStr += c; i++; }
                    }
                }

                // ★ 分离格式说明符：查找顶层 '::'（双冒号，彻底无歧义）
                std::string spec;
                {
                    int pd = 0, bd = 0, brd = 0;
                    bool s = false;
                    int sepPos = -1;
                    for (int j = 0; j < static_cast<int>(exprStr.size()) - 1; j++) {
                        char c = exprStr[j];
                        if (s) {
                            if (c == '\\' && j + 1 < static_cast<int>(exprStr.size())) j++;
                            else if (c == '"') s = false;
                        }
                        else {
                            if (c == '"')      s = true;
                            else if (c == '(') pd++;
                            else if (c == ')') pd--;
                            else if (c == '[') bd++;
                            else if (c == ']') bd--;
                            else if (c == '{') brd++;
                            else if (c == '}') brd--;
                            else if (c == ':' && exprStr[j + 1] == ':' &&
                                pd == 0 && bd == 0 && brd == 0) {
                                sepPos = j;
                                break;  // 找到第一个顶层 :: 即停止
                            }
                        }
                    }
                    if (sepPos >= 0) {
                        spec = exprStr.substr(sepPos + 2);  // 跳过 ::
                        exprStr = exprStr.substr(0, sepPos);
                    }
                }

                // ★ 保存前置文本段
                literals.push_back(currentLit);
                currentLit.clear();

                // ★ 子词法分析 + 子语法分析
                Lexer subLexer(exprStr);
                auto subTokens = subLexer.tokenize();
                Parser subParser(subTokens);
                auto exprAst = subParser.parse();

                exprs.push_back(std::move(exprAst));
                specs.push_back(spec);
            }
            else {
                currentLit += raw[i++];
            }
        }
        // ★ 尾部文本段
        literals.push_back(currentLit);

        return std::make_unique<FStringExpr>(
            std::move(literals), std::move(exprs), std::move(specs));
    }

    std::unique_ptr<Expr> Parser::parseListComp(std::unique_ptr<Expr> valueExpr) {
        // 进入时：当前 token 指向 FOR，valueExpr 已被提取
        std::vector<ListCompExpr::CompClause> clauses;
        std::unique_ptr<Expr> condition;

        while (match({ TokenType::FOR })) {
            // ★ 解构模式：for [a, b] in ...
            if (check(TokenType::LBRACKET)) {
                advance(); // consume [
                std::vector<Token> names;
                names.push_back(consume(TokenType::IDENTIFIER,
                    "Parser Error: Expect variable name in comprehension destructuring."));
                while (match({ TokenType::COMMA })) {
                    names.push_back(consume(TokenType::IDENTIFIER,
                        "Parser Error: Expect variable name after ',' in destructuring."));
                }
                consume(TokenType::RBRACKET,
                    "Parser Error: Expect ']' after destructuring variables.");
                consume(TokenType::IN,
                    "Parser Error: Expect 'in' after variable in list comprehension.");
                auto iterable = expression();
                clauses.emplace_back(std::move(names),
                    std::shared_ptr<Expr>(iterable.release()));
            }
            // ★ 单变量模式：for x in ...
            else {
                Token varName = consume(TokenType::IDENTIFIER,
                    "Parser Error: Expect variable name after 'for' in list comprehension.");
                consume(TokenType::IN,
                    "Parser Error: Expect 'in' after variable in list comprehension.");
                auto iterable = expression();
                clauses.emplace_back(varName,
                    std::shared_ptr<Expr>(iterable.release()));
            }
        }

        // ★ 可选的 if 过滤条件
        if (match({ TokenType::IF })) {
            condition = expression();
        }

        consume(TokenType::RBRACKET,
            "Parser Error: Expect ']' after list comprehension.");

        return std::make_unique<ListCompExpr>(
            std::move(valueExpr), std::move(clauses), std::move(condition));
    }

    std::unique_ptr<Expr> Parser::parseDictLiteral() {
        consume(TokenType::LBRACE, "Parser Error: Expect '{'.");

        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries;

        while (!check(TokenType::RBRACE) && !isAtEnd()) {
            // ★ 解析 key
            std::unique_ptr<Expr> key;
            if (check(TokenType::IDENTIFIER) &&
                current + 1 < static_cast<int>(tokens.size()) &&
                tokens[current + 1].type == TokenType::COLON) {
                // 裸标识符 → 自动当作字符串字面量（JavaScript 风格）
                Token idTok = advance();
                key = std::make_unique<Literal>(idTok.lexeme, true);
            }
            else {
                // 其他情况：字符串、数字等表达式（求值后转字符串）
                key = ternary();
            }

            consume(TokenType::COLON, "Parser Error: Expect ':' after dict key.");

            // ★ 解析 value（用 ternary 避免 = 被当成赋值）
            auto value = ternary();

            entries.push_back({ std::move(key), std::move(value) });

            if (!match({ TokenType::COMMA })) break;
            // 允许尾部逗号
            if (check(TokenType::RBRACE)) break;
        }

        consume(TokenType::RBRACE, "Parser Error: Expect '}' after dict literal.");
        return std::make_unique<DictLiteral>(std::move(entries));
    }

    // ---- 辅助函数 (不变) ----
    bool Parser::match(std::initializer_list<TokenType> types) { for (auto t : types) if (check(t)) { advance(); return true; } return false; }
    bool Parser::check(TokenType type) const { if (isAtEnd()) return false; return peek().type == type; }
    bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
    Token Parser::advance() { if (!isAtEnd()) current++; return previous(); }
    Token Parser::peek() const { return tokens[current]; }
    Token Parser::previous() const { return tokens[current - 1]; }
    Token Parser::consume(TokenType type, const std::string& message) { if (check(type)) return advance(); throw std::runtime_error(message); }

} // namespace jc

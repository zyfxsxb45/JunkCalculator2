#include "Compiler.h"
#include <functional>

namespace jc {

    // ── 辅助方法 ──

    void Compiler::emit(OpCode op, int line) { chunk()->write(op, line); }
    void Compiler::emit(uint8_t byte, int line) { chunk()->write(byte, line); }
    void Compiler::emit16(uint16_t val, int line) { chunk()->write16(val, line); }
    uint16_t Compiler::makeConstant(const Value& val) { return chunk()->addConstant(val); }
    uint16_t Compiler::identifierConstant(const std::string& name) { return makeConstant(Value(name)); }
    void Compiler::compileNode(Expr* expr) { expr->accept(*this); }

    void Compiler::initCompiler(CompiledFunction* fn) {
        CompilerState state;
        state.function = fn;
        state.scopeDepth = 0;
        stateStack.push_back(state);
    }

    void Compiler::beginScope() { current().scopeDepth++; }

    void Compiler::endScope() {
        current().scopeDepth--;
        while (!current().locals.empty() &&
            current().locals.back().depth > current().scopeDepth) {
            emit(OpCode::OP_POP, 0);
            current().locals.pop_back();
        }
    }

    // Compiler.cpp — 实现
    void Compiler::emitDefaultPreamble(
        const std::vector<std::shared_ptr<Expr>>& defaultExprs,
        int paramCount)
    {
        for (int i = 0; i < paramCount; ++i) {
            if (i < static_cast<int>(defaultExprs.size()) && defaultExprs[i]) {
                // if (local[i] == none) { local[i] = defaultExpr }
                emit(OpCode::OP_GET_LOCAL, 0);
                emit16(static_cast<uint16_t>(i), 0);
                emit(OpCode::OP_NONE, 0);
                emit(OpCode::OP_EQUAL, 0);

                int skipJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
                emit(OpCode::OP_POP, 0);  // pop true

                // 编译默认值表达式并写入 local
                compileNode(defaultExprs[i].get());
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(i), 0);
                emit(OpCode::OP_POP, 0);  // pop set 残留

                int endJump = chunk()->emitJump(OpCode::OP_JUMP, 0);
                chunk()->patchJump(skipJump);
                emit(OpCode::OP_POP, 0);  // pop false
                chunk()->patchJump(endJump);
            }
        }
    }

    void Compiler::emitStoreTarget(Expr* target) {
        uint16_t tmpIdx = identifierConstant("__compound_tmp__");

        // ═══ 终止条件：变量 ═══
        if (auto* var = dynamic_cast<Variable*>(target)) {
            int slot = resolveLocal(var->name.lexeme);
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                int upvalue = resolveUpvalue(var->name.lexeme);
                if (upvalue != -1) {
                    emit(OpCode::OP_SET_UPVALUE, 0);
                    emit16(static_cast<uint16_t>(upvalue), 0);
                }
                else {
                    uint16_t nameIdx = identifierConstant(var->name.lexeme);
                    emit(OpCode::OP_SET_GLOBAL, 0);
                    emit16(nameIdx, 0);
                }
            }
            return;
        }

        // ═══ DotAccess — 设置字段后递归回写父级 ═══
        if (auto* dot = dynamic_cast<DotAccess*>(target)) {
            // 栈: [newValue]
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(tmpIdx, 0);
            emit(OpCode::OP_POP, 0);
            // 栈: []

            compileNode(dot->object.get());
            // 栈: [parent]

            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(tmpIdx, 0);
            // 栈: [parent, newValue]

            uint16_t fieldIdx = identifierConstant(dot->field.lexeme);
            emit(OpCode::OP_SET_PROPERTY, 0);
            emit16(fieldIdx, 0);
            // 栈: [modified_parent]
            // SET_PROPERTY now pushes modified container for ALL types

            // ★★★ 递归回写（不再直接 return）★★★
            emitStoreTarget(dot->object.get());
            return;
        }

        // ═══ IndexAccess — 索引写入后递归回写（不变）═══
        if (auto* idx = dynamic_cast<IndexAccess*>(target)) {
            for (auto& i : idx->indices) {
                if (dynamic_cast<SliceExpr*>(i.get()))
                    throw std::runtime_error(
                        "Compiler Error: Slice compound assignment not supported in VM.");
            }
            uint8_t dimCount = static_cast<uint8_t>(idx->indices.size());

            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(tmpIdx, 0);
            emit(OpCode::OP_POP, 0);

            compileNode(idx->object.get());
            for (auto& i : idx->indices)
                compileNode(i.get());

            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(tmpIdx, 0);

            emit(OpCode::OP_INDEX_SET, 0);
            emit(dimCount, 0);

            emitStoreTarget(idx->object.get());
            return;
        }

        throw std::runtime_error(
            "Compiler Error: Cannot store to this expression type in compound assignment.");
    }

    void Compiler::compileCompClause(ListCompExpr* expr, size_t clauseIdx) {
        if (clauseIdx >= expr->clauses.size()) {
            // ★ 所有 for 子句完毕 → 检查条件并追加
            // 此时栈：[..., ResultList, (ElemsList, Index) × clauseIdx, ...]
            // 追加时栈顶有 exprValue，ResultList 在 2*totalClauses 层之下

            uint16_t depth = static_cast<uint16_t>(2 * expr->clauses.size());

            if (expr->condition) {
                compileNode(expr->condition.get());
                int skipJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
                emit(OpCode::OP_POP, 0);

                compileNode(expr->valueExpr.get());
                emit(OpCode::OP_LIST_APPEND, 0);
                emit16(depth, 0);

                int endJump = chunk()->emitJump(OpCode::OP_JUMP, 0);
                chunk()->patchJump(skipJump);
                emit(OpCode::OP_POP, 0);
                chunk()->patchJump(endJump);
            }
            else {
                compileNode(expr->valueExpr.get());
                emit(OpCode::OP_LIST_APPEND, 0);
                emit16(depth, 0);
            }
            return;
        }

        auto& clause = expr->clauses[clauseIdx];
        compileNode(clause.iterable.get());
        emit(OpCode::OP_ITER_INIT, 0);
        emit(static_cast<uint8_t>(clause.isDestruct() ? 1 : 0), 0);  // ★ Dict 迭代模式标志

        int loopStart = static_cast<int>(chunk()->code.size());
        int exitJump = chunk()->emitJump(OpCode::OP_ITER_NEXT, 0);

        if (clause.isDestruct()) {
            int n = static_cast<int>(clause.destructNames.size());
            emit(OpCode::OP_DESTRUCT, 0);
            emit(static_cast<uint8_t>(n), 0);
            for (int j = n - 1; j >= 0; --j) {
                const std::string& name = clause.destructNames[j].lexeme;
                if (name == "_") { emit(OpCode::OP_POP, 0); continue; }
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(idx, 0);
                emit(OpCode::OP_POP, 0);
            }
        }
        else {
            uint16_t idx = identifierConstant(clause.varName.lexeme);
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(idx, 0);
            emit(OpCode::OP_POP, 0);
        }

        compileCompClause(expr, clauseIdx + 1);

        chunk()->emitLoop(loopStart, 0);
        chunk()->patchJump(exitJump);

        emit(OpCode::OP_POP, 0); // index
        emit(OpCode::OP_POP, 0); // elements
    }

    void Compiler::addLocal(const std::string& name) {
        current().locals.push_back({ name, current().scopeDepth });
    }

    void Compiler::declareVariable(const std::string& name) {
        if (current().scopeDepth == 0) return;
        addLocal(name);
    }

    int Compiler::resolveLocal(const std::string& name) {
        auto& locals = current().locals;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
            if (locals[i].name == name) return i;
        }
        return -1;
    }

    void Compiler::beginLoop(int loopStart) {
        loopStack.push_back({ loopStart, {}, current().scopeDepth });
    }

    void Compiler::endLoop() {
        loopStack.pop_back();
    }

    void Compiler::emitBreakJumps() {
        for (int offset : loopStack.back().breakJumps) {
            chunk()->patchJump(offset);
        }
    }

    // ── 入口 ──

    Chunk Compiler::compile(Expr* ast) {
        auto mainFn = std::make_shared<CompiledFunction>();
        mainFn->name = "<script>";
        compiledFunctions.push_back(mainFn);
        initCompiler(mainFn.get());
        compileNode(ast);
        emit(OpCode::OP_RETURN, 0);
        stateStack.pop_back();
        return mainFn->chunk;
    }

    // ══════════════════════════════════════════════
    // Visitor 实现
    // ══════════════════════════════════════════════

    std::any Compiler::visitLiteral(Literal* expr) {
        if (expr->isString) {
            chunk()->emitConstant(Value(expr->value), 0);
        }
        else if (expr->isImaginary) {
            chunk()->emitConstant(Value(Complex(0.0, std::stod(expr->value))), 0);
        }
        else {
            const std::string& s = expr->value;
            if (s.find('.') == std::string::npos &&
                s.find('e') == std::string::npos &&
                s.find('E') == std::string::npos) {
                try { chunk()->emitConstant(Value(BigInt(s)), 0); }
                catch (...) { chunk()->emitConstant(Value(std::stod(s)), 0); }
            }
            else {
                chunk()->emitConstant(Value(std::stod(s)), 0);
            }
        }
        return {};
    }

    std::any Compiler::visitVariable(Variable* expr) {
        const std::string& name = expr->name.lexeme;
        int slot = resolveLocal(name);
        if (slot != -1) {
            emit(OpCode::OP_GET_LOCAL, expr->name.position);
            emit16(static_cast<uint16_t>(slot), expr->name.position);
        }
        else {
            // ★ 新增：尝试 upvalue
            int upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_GET_UPVALUE, expr->name.position);
                emit16(static_cast<uint16_t>(upvalue), expr->name.position);
            }
            else {
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_GET_GLOBAL, expr->name.position);
                emit16(idx, expr->name.position);
            }
        }
        return {};
    }

    std::any Compiler::visitAssign(Assign* expr) {
        compileNode(expr->value.get());
        const std::string& name = expr->name.lexeme;
        int slot = resolveLocal(name);
        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, expr->name.position);
            emit16(static_cast<uint16_t>(slot), expr->name.position);
        }
        else {
            // ★ 新增：尝试 upvalue
            int upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_SET_UPVALUE, expr->name.position);
                emit16(static_cast<uint16_t>(upvalue), expr->name.position);
            }
            else {
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_SET_GLOBAL, expr->name.position);
                emit16(idx, expr->name.position);
            }
        }
        return {};
    }

    std::any Compiler::visitUnary(Unary* expr) {
        compileNode(expr->right.get());
        switch (expr->op.type) {
        case TokenType::MINUS: emit(OpCode::OP_NEGATE, expr->op.position); break;
        case TokenType::BANG:  emit(OpCode::OP_NOT, expr->op.position); break;
        case TokenType::PLUS:  break;
        default: throw std::runtime_error("Compiler Error: Unknown unary operator.");
        }
        return {};
    }

    std::any Compiler::visitBinary(Binary* expr) {
        if (expr->op.type == TokenType::AND_AND) {
            compileNode(expr->left.get());
            int jump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, expr->op.position);
            emit(OpCode::OP_POP, expr->op.position);
            compileNode(expr->right.get());
            chunk()->patchJump(jump);
            return {};
        }
        if (expr->op.type == TokenType::OR_OR) {
            compileNode(expr->left.get());
            int elseJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, expr->op.position);
            int endJump = chunk()->emitJump(OpCode::OP_JUMP, expr->op.position);
            chunk()->patchJump(elseJump);
            emit(OpCode::OP_POP, expr->op.position);
            compileNode(expr->right.get());
            chunk()->patchJump(endJump);
            return {};
        }

        // ★ 管道运算符：value |> func → func(value)
        if (expr->op.type == TokenType::PIPE) {
            compileNode(expr->right.get());   // 先压入 func
            compileNode(expr->left.get());    // 再压入 value (作为参数)
            emit(OpCode::OP_CALL, expr->op.position);
            emit(1, expr->op.position);
            return {};
        }

        compileNode(expr->left.get());
        compileNode(expr->right.get());

        int line = expr->op.position;
        switch (expr->op.type) {
        case TokenType::PLUS:          emit(OpCode::OP_ADD, line); break;
        case TokenType::MINUS:         emit(OpCode::OP_SUBTRACT, line); break;
        case TokenType::STAR:          emit(OpCode::OP_MULTIPLY, line); break;
        case TokenType::SLASH:         emit(OpCode::OP_DIVIDE, line); break;
        case TokenType::PERCENT:       emit(OpCode::OP_MODULO, line); break;
        case TokenType::CARET:         emit(OpCode::OP_POWER, line); break;
        case TokenType::EQUAL:         emit(OpCode::OP_EQUAL, line); break;
        case TokenType::BANG_EQUAL:    emit(OpCode::OP_NOT_EQUAL, line); break;
        case TokenType::LESS:          emit(OpCode::OP_LESS, line); break;
        case TokenType::LESS_EQUAL:    emit(OpCode::OP_LESS_EQUAL, line); break;
        case TokenType::GREATER:       emit(OpCode::OP_GREATER, line); break;
        case TokenType::GREATER_EQUAL: emit(OpCode::OP_GREATER_EQUAL, line); break;
        case TokenType::IN:            emit(OpCode::OP_IN, line); break;
        default:
            throw std::runtime_error("Compiler Error: Unsupported binary operator '" +
                expr->op.lexeme + "'.");
        }
        return {};
    }

    std::any Compiler::visitCall(Call* expr) {
        const std::string& name = expr->callee.lexeme;
        int slot = resolveLocal(name);
        if (slot != -1) {
            emit(OpCode::OP_GET_LOCAL, expr->callee.position);
            emit16(static_cast<uint16_t>(slot), expr->callee.position);
        }
        else {
            uint16_t idx = identifierConstant(name);
            emit(OpCode::OP_GET_GLOBAL, expr->callee.position);
            emit16(idx, expr->callee.position);
        }
        for (auto& argExpr : expr->arguments) {
            compileNode(argExpr.get());
        }
        emit(OpCode::OP_CALL, expr->callee.position);
        emit(static_cast<uint8_t>(expr->arguments.size()), expr->callee.position);

        // ★★★ 仅当有 Variable 参数且函数可能有 ref 时才发射 ★★★
        // 优化：如果能在编译期确定函数无 ref 参数，跳过发射
        bool hasVariableArgs = false;
        for (auto& argExpr : expr->arguments) {
            if (dynamic_cast<Variable*>(argExpr.get())) {
                hasVariableArgs = true;
                break;
            }
        }

        // ★ 查找已编译的函数定义，检查是否有 ref 参数
        bool mayHaveRef = false;
        if (hasVariableArgs) {
            // 在已编译函数列表中查找同名函数
            bool foundDef = false;
            for (auto& fn : compiledFunctions) {
                if (fn->name == name) {
                    foundDef = true;
                    for (bool r : fn->paramIsRef) {
                        if (r) { mayHaveRef = true; break; }
                    }
                    break;
                }
            }
            // 如果找不到定义（动态函数、未定义等），保守假设可能有 ref
            if (!foundDef) mayHaveRef = true;
        }

        if (mayHaveRef) {
            struct ArgSource {
                uint8_t argIndex;
                uint8_t sourceType;
                uint16_t sourceRef;
            };
            std::vector<ArgSource> sources;

            for (int i = 0; i < static_cast<int>(expr->arguments.size()); ++i) {
                if (auto* varExpr = dynamic_cast<Variable*>(expr->arguments[i].get())) {
                    int localSlot = resolveLocal(varExpr->name.lexeme);
                    if (localSlot != -1) {
                        sources.push_back({ static_cast<uint8_t>(i), 2,
                            static_cast<uint16_t>(localSlot) });
                    }
                    else {
                        int uv = resolveUpvalue(varExpr->name.lexeme);
                        if (uv != -1) {
                            sources.push_back({ static_cast<uint8_t>(i), 3,
                                static_cast<uint16_t>(uv) });
                        }
                        else {
                            uint16_t nameIdx = identifierConstant(varExpr->name.lexeme);
                            sources.push_back({ static_cast<uint8_t>(i), 1, nameIdx });
                        }
                    }
                }
            }

            if (!sources.empty()) {
                emit(OpCode::OP_REF_WRITEBACK, expr->callee.position);
                emit(static_cast<uint8_t>(sources.size()), expr->callee.position);
                for (auto& s : sources) {
                    emit(s.argIndex, expr->callee.position);
                    emit(s.sourceType, expr->callee.position);
                    emit16(s.sourceRef, expr->callee.position);
                }
            }
        }

        return {};
    }

    std::any Compiler::visitBlock(Block* expr) {
        beginScope();

        // ★ 修复：空块也必须在栈上留下恰好一个值
        if (expr->statements.empty()) {
            emit(OpCode::OP_NONE, 0);
        }
        else {
            for (size_t i = 0; i < expr->statements.size(); ++i) {
                compileNode(expr->statements[i].get());
                if (i < expr->statements.size() - 1) {
                    emit(OpCode::OP_POP, 0);
                }
            }
        }

        current().scopeDepth--;
        while (!current().locals.empty() &&
            current().locals.back().depth > current().scopeDepth) {
            emit(OpCode::OP_POP, 0);
            current().locals.pop_back();
        }
        return {};
    }

    std::any Compiler::visitIfExpr(IfExpr* expr) {
        compileNode(expr->condition.get());
        int thenJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
        emit(OpCode::OP_POP, 0);
        compileNode(expr->thenBranch.get());
        int elseJump = chunk()->emitJump(OpCode::OP_JUMP, 0);
        chunk()->patchJump(thenJump);
        emit(OpCode::OP_POP, 0);
        if (expr->elseBranch) {
            compileNode(expr->elseBranch.get());
        }
        else {
            emit(OpCode::OP_NONE, 0);
        }
        chunk()->patchJump(elseJump);
        return {};
    }

    std::any Compiler::visitWhileExpr(WhileExpr* expr) {
        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        compileNode(expr->condition.get());
        int exitJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
        emit(OpCode::OP_POP, 0);
        compileNode(expr->body.get());
        emit(OpCode::OP_POP, 0);
        chunk()->emitLoop(loopStart, 0);

        chunk()->patchJump(exitJump);
        emit(OpCode::OP_POP, 0);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitForExpr(ForExpr* expr) {
        beginScope();
        compileNode(expr->initializer.get());
        emit(OpCode::OP_POP, 0);

        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        compileNode(expr->condition.get());
        int exitJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
        emit(OpCode::OP_POP, 0);
        compileNode(expr->body.get());
        emit(OpCode::OP_POP, 0);
        compileNode(expr->update.get());
        emit(OpCode::OP_POP, 0);
        chunk()->emitLoop(loopStart, 0);

        chunk()->patchJump(exitJump);
        emit(OpCode::OP_POP, 0);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_NONE, 0);
        endScope();
        return {};
    }

    // ★ 函数定义
    std::any Compiler::visitFunctionDef(FunctionDef* expr) {
        const std::string& funcName = expr->name.lexeme;

        auto fn = std::make_shared<CompiledFunction>();
        fn->name = funcName;
        fn->maxArity = static_cast<int>(expr->params.size());

        int requiredParams = 0;
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i >= expr->defaultExprs.size() || !expr->defaultExprs[i]) {
                requiredParams++;
            }
            else break;
        }
        fn->arity = requiredParams;

        compiledFunctions.push_back(fn);

        // ★★★ 立即锁定索引，body 编译前！★★★
        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        fn->localCount = static_cast<int>(expr->params.size());
        fn->paramIsRef = expr->paramIsRef;

        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);

        compileNode(expr->body.get());
        emit(OpCode::OP_RETURN, 0);

        endScope();
        stateStack.pop_back();

        // ★★★ 使用锁定的索引 ★★★
        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, expr->name.position);
        emit16(fnIdx, expr->name.position);

        uint16_t nameIdx = identifierConstant(funcName);
        emit(OpCode::OP_SET_GLOBAL, expr->name.position);
        emit16(nameIdx, expr->name.position);

        return {};
    }

    std::any Compiler::visitLambdaExpr(LambdaExpr* expr) {
        auto fn = std::make_shared<CompiledFunction>();
        fn->name = "<lambda>";
        fn->maxArity = static_cast<int>(expr->params.size());

        int requiredParams = 0;
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i >= expr->defaultExprs.size() || !expr->defaultExprs[i]) {
                requiredParams++;
            }
            else break;
        }
        fn->arity = requiredParams;
        fn->paramIsRef.assign(fn->maxArity, false);

        compiledFunctions.push_back(fn);

        // ★★★ 立即锁定索引 ★★★
        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        fn->localCount = static_cast<int>(expr->params.size());

        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);

        compileNode(expr->body.get());
        emit(OpCode::OP_RETURN, 0);

        endScope();
        stateStack.pop_back();

        // ★★★ 使用锁定的索引 ★★★
        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, 0);
        emit16(fnIdx, 0);

        return {};
    }

    int Compiler::addUpvalue(int level, const std::string& name,
        bool isLocal, int index) {
        auto* fn = stateStack[level].function;
        // 检查是否已注册（避免重复）
        for (int j = 0; j < static_cast<int>(fn->upvalues.size()); ++j) {
            if (fn->upvalues[j].name == name)
                return j;
        }
        fn->upvalues.push_back({ name, isLocal, index });
        return static_cast<int>(fn->upvalues.size()) - 1;
    }

    int Compiler::resolveUpvalueAt(int level, const std::string& name) {
        // 没有外层函数了 → 失败
        if (level <= 0) return -1;

        int enclosingLevel = level - 1;
        auto& enclosing = stateStack[enclosingLevel];

        // ══ 步骤 1：变量是外层函数的局部变量？ ══
        for (int i = static_cast<int>(enclosing.locals.size()) - 1; i >= 0; --i) {
            if (enclosing.locals[i].name == name) {
                // 在直接父级的 locals 中找到
                // → 在 level 层函数注册为 upvalue，标记 isLocal=true
                return addUpvalue(level, name, true, i);
            }
        }

        // ══ 步骤 2：递归——让外层函数先捕获它 ══
        int upvalueInEnclosing = resolveUpvalueAt(enclosingLevel, name);
        if (upvalueInEnclosing != -1) {
            // 外层函数现在有了这个 upvalue（可能刚注册的）
            // → 在 level 层函数注册为 upvalue，标记 isLocal=false（从父级的 upvalue 转发）
            return addUpvalue(level, name, false, upvalueInEnclosing);
        }

        return -1;
    }

    int Compiler::resolveUpvalue(const std::string& name) {
        int currentLevel = static_cast<int>(stateStack.size()) - 1;
        return resolveUpvalueAt(currentLevel, name);
    }

    // ★ Return
    std::any Compiler::visitReturnExpr(ReturnExpr* expr) {
        if (expr->value) {
            compileNode(expr->value.get());
        }
        else {
            emit(OpCode::OP_NONE, 0);
        }
        emit(OpCode::OP_RETURN, 0);
        return {};
    }

    // ★ Break
    std::any Compiler::visitBreakExpr(BreakExpr*) {
        if (loopStack.empty())
            throw std::runtime_error("Compiler Error: 'break' outside loop.");
        // 清理当前循环内的局部变量
        int loopDepth = loopStack.back().scopeDepth;
        auto& locals = current().locals;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
            if (locals[i].depth <= loopDepth) break;
            emit(OpCode::OP_POP, 0);
        }
        int jump = chunk()->emitJump(OpCode::OP_JUMP, 0);
        loopStack.back().breakJumps.push_back(jump);
        return {};
    }

    // ★ Continue
    std::any Compiler::visitContinueExpr(ContinueExpr*) {
        if (loopStack.empty())
            throw std::runtime_error("Compiler Error: 'continue' outside loop.");
        int loopDepth = loopStack.back().scopeDepth;
        auto& locals = current().locals;
        for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
            if (locals[i].depth <= loopDepth) break;
            emit(OpCode::OP_POP, 0);
        }
        chunk()->emitLoop(loopStack.back().loopStart, 0);
        return {};
    }

    std::any Compiler::visitCompoundAssign(CompoundAssign* expr) {
        auto emitOp = [this](TokenType op) {
            switch (op) {
            case TokenType::PLUS:    emit(OpCode::OP_ADD, 0); break;
            case TokenType::MINUS:   emit(OpCode::OP_SUBTRACT, 0); break;
            case TokenType::STAR:    emit(OpCode::OP_MULTIPLY, 0); break;
            case TokenType::SLASH:   emit(OpCode::OP_DIVIDE, 0); break;
            case TokenType::PERCENT: emit(OpCode::OP_MODULO, 0); break;
            case TokenType::CARET:   emit(OpCode::OP_POWER, 0); break;
            default: throw std::runtime_error("Compiler Error: Unknown compound operator.");
            }
            };

        // ═══ 情况 1：x += e（直接读写，最高效）═══
        if (auto* var = dynamic_cast<Variable*>(expr->target.get())) {
            const std::string& name = var->name.lexeme;
            int slot = resolveLocal(name);

            if (slot != -1) {
                emit(OpCode::OP_GET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                int upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    emit(OpCode::OP_GET_UPVALUE, 0);
                    emit16(static_cast<uint16_t>(upvalue), 0);
                }
                else {
                    uint16_t idx = identifierConstant(name);
                    emit(OpCode::OP_GET_GLOBAL, 0);
                    emit16(idx, 0);
                }
            }
            compileNode(expr->value.get());
            emitOp(expr->op);
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                int upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    emit(OpCode::OP_SET_UPVALUE, 0);
                    emit16(static_cast<uint16_t>(upvalue), 0);
                }
                else {
                    uint16_t idx = identifierConstant(name);
                    emit(OpCode::OP_SET_GLOBAL, 0);
                    emit16(idx, 0);
                }
            }
            return {};
        }

        // ═══ 情况 2 + 3：统一处理任意目标表达式 ═══
        //   obj.field += e
        //   A[i] += e / A[i,j] += e
        //   obj.arr[i] += e
        //   a[i][j][k] += e
        //   任意嵌套组合
        //
        // 通用策略：
        //   1. compileNode(target) → 读取当前值
        //   2. compile(value) + op → 计算新值
        //   3. emitStoreTarget(target) → 递归回写
        // ═══════════════════════════════════════════

        if (dynamic_cast<DotAccess*>(expr->target.get()) ||
            dynamic_cast<IndexAccess*>(expr->target.get())) {

            // 切片检查
            std::function<void(Expr*)> checkSlice = [&](Expr* e) {
                if (auto* idx = dynamic_cast<IndexAccess*>(e)) {
                    for (auto& i : idx->indices)
                        if (dynamic_cast<SliceExpr*>(i.get()))
                            throw std::runtime_error(
                                "Compiler Error: Slice compound assignment "
                                "not supported in VM.");
                    checkSlice(idx->object.get());
                }
                };
            checkSlice(expr->target.get());

            // 1. 读取当前值
            compileNode(expr->target.get());

            // 2. 计算新值
            compileNode(expr->value.get());
            emitOp(expr->op);

            // 3. 递归回写
            emitStoreTarget(expr->target.get());

            return {};
        }

        throw std::runtime_error("Compiler Error: Compound assignment target not supported in VM.");
    }

    // ★ For-In Loop
    std::any Compiler::visitForInExpr(ForInExpr* expr) {
        compileNode(expr->iterable.get());
        emit(OpCode::OP_ITER_INIT, 0);
        emit(static_cast<uint8_t>(expr->isDestruct() ? 1 : 0), 0);

        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        int exitJump = chunk()->emitJump(OpCode::OP_ITER_NEXT, 0);

        if (expr->isDestruct()) {
            int n = static_cast<int>(expr->destructNames.size());
            emit(OpCode::OP_DESTRUCT, 0);
            emit(static_cast<uint8_t>(n), 0);
            for (int j = n - 1; j >= 0; --j) {
                const std::string& name = expr->destructNames[j].lexeme;
                if (name == "_") { emit(OpCode::OP_POP, 0); continue; }
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(idx, 0);
                emit(OpCode::OP_POP, 0);
            }
        }
        else {
            const std::string& varName = expr->varName.lexeme;
            uint16_t idx = identifierConstant(varName);
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(idx, 0);
            emit(OpCode::OP_POP, 0);
        }

        compileNode(expr->body.get());
        emit(OpCode::OP_POP, 0);

        chunk()->emitLoop(loopStart, 0);
        chunk()->patchJump(exitJump);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_POP, 0); // index
        emit(OpCode::OP_POP, 0); // elements
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    // ★ Matrix 字面量
    std::any Compiler::visitMatrixNode(MatrixNode* expr) {
        int rows = static_cast<int>(expr->elements.size());
        if (rows == 0) {
            chunk()->emitConstant(Value(RealMatrix(0, 0)), 0);
            return {};
        }
        int cols = static_cast<int>(expr->elements[0].size());

        // 编译所有元素（行优先压栈）
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                compileNode(expr->elements[i][j].get());
            }
        }
        emit(OpCode::OP_BUILD_MATRIX, 0);
        emit16(static_cast<uint16_t>(rows), 0);
        emit16(static_cast<uint16_t>(cols), 0);
        return {};
    }

    // ★ Index Access: A[i] / A[i,j]
    std::any Compiler::visitIndexAccess(IndexAccess* expr) {
        // ★ 检测是否含有切片
        bool hasSlice = false;
        for (auto& idx : expr->indices) {
            if (dynamic_cast<SliceExpr*>(idx.get())) {
                hasSlice = true;
                break;
            }
        }

        if (hasSlice) {
            // ★★★ 切片路径：使用 OP_SLICE_GET ★★★
            compileNode(expr->object.get());

            // 对每个维度：编译 start/end/step（缺省压 none）
            for (auto& idx : expr->indices) {
                if (auto* slice = dynamic_cast<SliceExpr*>(idx.get())) {
                    if (slice->start) compileNode(slice->start.get());
                    else emit(OpCode::OP_NONE, 0);

                    if (slice->end) compileNode(slice->end.get());
                    else emit(OpCode::OP_NONE, 0);

                    if (slice->step) compileNode(slice->step.get());
                    else emit(OpCode::OP_NONE, 0);
                }
                else {
                    // 非切片维度：值本身 + none + none（标记为标量索引）
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, 0);
                    emit(OpCode::OP_NONE, 0);
                }
            }

            emit(OpCode::OP_SLICE_GET, 0);
            emit(static_cast<uint8_t>(expr->indices.size()), 0);
            return {};
        }

        // ── 非切片：原有逻辑 ──
        compileNode(expr->object.get());
        for (auto& idx : expr->indices) {
            compileNode(idx.get());
        }
        emit(OpCode::OP_INDEX_GET, 0);
        emit(static_cast<uint8_t>(expr->indices.size()), 0);
        return {};
    }

    // ★ Index Assign: A[i] = v
    std::any Compiler::visitIndexAssign(IndexAssign* expr) {
        // ★ 检测是否含有切片
        bool hasSlice = false;
        if (expr->indexChain.size() == 1) {
            for (auto& idx : expr->indexChain[0]) {
                if (dynamic_cast<SliceExpr*>(idx.get())) {
                    hasSlice = true;
                    break;
                }
            }
        }

        if (hasSlice) {
            // ═══ 切片赋值路径 ═══
            // 编译对象
            if (expr->hasObjectExpr()) {
                compileNode(expr->objectExpr.get());
            }
            else {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) {
                    emit(OpCode::OP_GET_LOCAL, 0);
                    emit16(static_cast<uint16_t>(slot), 0);
                }
                else {
                    uint16_t idx = identifierConstant(expr->name.lexeme);
                    emit(OpCode::OP_GET_GLOBAL, 0);
                    emit16(idx, 0);
                }
            }

            // 编译切片参数（每维度 3 个：start, end, step）
            for (auto& idx : expr->indexChain[0]) {
                if (auto* slice = dynamic_cast<SliceExpr*>(idx.get())) {
                    if (slice->start) compileNode(slice->start.get());
                    else emit(OpCode::OP_NONE, 0);
                    if (slice->end) compileNode(slice->end.get());
                    else emit(OpCode::OP_NONE, 0);
                    if (slice->step) compileNode(slice->step.get());
                    else emit(OpCode::OP_NONE, 0);
                }
                else {
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, 0);
                    emit(OpCode::OP_NONE, 0);
                }
            }

            // 编译赋值值
            compileNode(expr->value.get());

            emit(OpCode::OP_SLICE_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), 0);

            // 写回变量
            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) {
                    emit(OpCode::OP_SET_LOCAL, 0);
                    emit16(static_cast<uint16_t>(slot), 0);
                }
                else {
                    uint16_t idx = identifierConstant(expr->name.lexeme);
                    emit(OpCode::OP_SET_GLOBAL, 0);
                    emit16(idx, 0);
                }
            }
            else {
                emitStoreTarget(expr->objectExpr.get());
            }

            return {};
        }

        // ═══ 以下为非切片逻辑（支持任意深度）═══
        int depth = static_cast<int>(expr->indexChain.size());

        // ── 辅助 lambda：发射加载根对象的代码 ──
        auto emitLoadRoot = [&]() {
            if (expr->hasObjectExpr()) {
                compileNode(expr->objectExpr.get());
            }
            else {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) {
                    emit(OpCode::OP_GET_LOCAL, 0);
                    emit16(static_cast<uint16_t>(slot), 0);
                }
                else {
                    uint16_t nameIdx = identifierConstant(expr->name.lexeme);
                    emit(OpCode::OP_GET_GLOBAL, 0);
                    emit16(nameIdx, 0);
                }
            }
            };

        // ── 辅助 lambda：发射存储回根对象的代码 ──
        auto emitStoreRoot = [&]() {
            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) {
                    emit(OpCode::OP_SET_LOCAL, 0);
                    emit16(static_cast<uint16_t>(slot), 0);
                }
                else {
                    uint16_t nameIdx = identifierConstant(expr->name.lexeme);
                    emit(OpCode::OP_SET_GLOBAL, 0);
                    emit16(nameIdx, 0);
                }
            }
            else {
                emitStoreTarget(expr->objectExpr.get());
            }
            };

        if (depth == 1) {
            // ═══ 单层索引赋值（原有快速路径）═══
            emitLoadRoot();
            for (auto& idx : expr->indexChain[0])
                compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), 0);
            emitStoreRoot();
        }
        else {
            // ═══ 多层索引赋值：a[i][j]...[k] = value ═══
            uint16_t tmpIdx = identifierConstant("__idx_chain_tmp__");

            // 步骤 1：加载根对象，沿链读取到倒数第二层容器
            emitLoadRoot();
            for (int level = 0; level < depth - 1; ++level) {
                for (auto& idx : expr->indexChain[level])
                    compileNode(idx.get());
                emit(OpCode::OP_INDEX_GET, 0);
                emit(static_cast<uint8_t>(expr->indexChain[level].size()), 0);
            }
            // 栈：[container_at_depth-1]

            // 步骤 2：在最深层执行 INDEX_SET
            for (auto& idx : expr->indexChain[depth - 1])
                compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[depth - 1].size()), 0);
            // 栈：[modified_container_at_depth-1]

            // 步骤 3：逐层回写
            for (int level = depth - 2; level >= 0; --level) {
                // 保存修改后的子容器到临时变量
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(tmpIdx, 0);
                emit(OpCode::OP_POP, 0);

                // 重新加载根并导航到当前层的容器
                emitLoadRoot();
                for (int l = 0; l < level; ++l) {
                    for (auto& idx : expr->indexChain[l])
                        compileNode(idx.get());
                    emit(OpCode::OP_INDEX_GET, 0);
                    emit(static_cast<uint8_t>(expr->indexChain[l].size()), 0);
                }

                // 编译当前层的索引
                for (auto& idx : expr->indexChain[level])
                    compileNode(idx.get());

                // 取回修改后的子容器
                emit(OpCode::OP_GET_GLOBAL, 0);
                emit16(tmpIdx, 0);

                // INDEX_SET 写入当前层
                emit(OpCode::OP_INDEX_SET, 0);
                emit(static_cast<uint8_t>(expr->indexChain[level].size()), 0);
            }
            // 栈：[modified_root]

            // 步骤 4：存储回根变量
            emitStoreRoot();
        }

        return {};
    }

    // ★ Invoke: lambda() 或 getValue()()
    std::any Compiler::visitInvokeExpr(InvokeExpr* expr) {
        compileNode(expr->callee.get());
        for (auto& argExpr : expr->arguments) {
            compileNode(argExpr.get());
        }
        emit(OpCode::OP_CALL, 0);
        emit(static_cast<uint8_t>(expr->arguments.size()), 0);

        // ★ 表达式调用（lambda等），编译期无法知道函数名
        // 保守策略：检查是否有 Variable 参数
        bool hasVariableArgs = false;
        for (auto& argExpr : expr->arguments) {
            if (dynamic_cast<Variable*>(argExpr.get())) {
                hasVariableArgs = true;
                break;
            }
        }

        // ★ Lambda 通常不使用 ref，但无法确定
        // 只在有 Variable 参数时发射（利用运行时的 pendingRefWritebacks.clear() 防护）
        if (hasVariableArgs) {
            struct ArgSource {
                uint8_t argIndex;
                uint8_t sourceType;
                uint16_t sourceRef;
            };
            std::vector<ArgSource> sources;

            for (int i = 0; i < static_cast<int>(expr->arguments.size()); ++i) {
                if (auto* varExpr = dynamic_cast<Variable*>(expr->arguments[i].get())) {
                    int localSlot = resolveLocal(varExpr->name.lexeme);
                    if (localSlot != -1) {
                        sources.push_back({ static_cast<uint8_t>(i), 2,
                            static_cast<uint16_t>(localSlot) });
                    }
                    else {
                        int uv = resolveUpvalue(varExpr->name.lexeme);
                        if (uv != -1) {
                            sources.push_back({ static_cast<uint8_t>(i), 3,
                                static_cast<uint16_t>(uv) });
                        }
                        else {
                            uint16_t nameIdx = identifierConstant(varExpr->name.lexeme);
                            sources.push_back({ static_cast<uint8_t>(i), 1, nameIdx });
                        }
                    }
                }
            }

            if (!sources.empty()) {
                emit(OpCode::OP_REF_WRITEBACK, 0);
                emit(static_cast<uint8_t>(sources.size()), 0);
                for (auto& s : sources) {
                    emit(s.argIndex, 0);
                    emit(s.sourceType, 0);
                    emit16(s.sourceRef, 0);
                }
            }
        }

        return {};
    }

    // ★ 解构赋值
    std::any Compiler::visitDestructAssign(DestructAssign* expr) {
        // 编译右侧值
        compileNode(expr->value.get());

        // OP_DESTRUCT N：将栈顶的数组/列表/向量展开为 N 个值
        int n = static_cast<int>(expr->names.size());
        emit(OpCode::OP_DESTRUCT, 0);
        emit(static_cast<uint8_t>(n), 0);

        // 现在栈上有 N 个值（最后一个在栈顶）
        // 需要逆序赋值（栈顶 = 最后一个变量）
        for (int i = n - 1; i >= 0; --i) {
            const std::string& name = expr->names[i].lexeme;
            if (name == "_") {
                emit(OpCode::OP_POP, 0); // 丢弃
                continue;
            }
            int slot = resolveLocal(name);
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(idx, 0);
            }
            emit(OpCode::OP_POP, 0);
        }

        // 解构赋值的返回值：压入 none
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitSwitchExpr(SwitchExpr* expr) {
        compileNode(expr->subject.get());

        std::vector<int> endJumps;

        for (auto& [values, body] : expr->cases) {
            // 对每个 case value：DUP subject，比较，匹配则跳到 body
            std::vector<int> bodyJumps;
            int noMatchJump = -1;

            for (size_t vi = 0; vi < values.size(); ++vi) {
                emit(OpCode::OP_DUP, 0);
                compileNode(values[vi].get());
                emit(OpCode::OP_EQUAL, 0);

                // 如果匹配，跳到 body
                int matchJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
                emit(OpCode::OP_POP, 0); // pop true
                int toBody = chunk()->emitJump(OpCode::OP_JUMP, 0);
                bodyJumps.push_back(toBody);

                chunk()->patchJump(matchJump);
                emit(OpCode::OP_POP, 0); // pop false
            }

            // 所有 values 都不匹配 → 跳过 body
            noMatchJump = chunk()->emitJump(OpCode::OP_JUMP, 0);

            // body 开始：回填所有 bodyJumps
            for (int bj : bodyJumps) {
                chunk()->patchJump(bj);
            }

            // 弹出 subject 副本，执行 body
            emit(OpCode::OP_POP, 0);
            compileNode(body.get());
            endJumps.push_back(chunk()->emitJump(OpCode::OP_JUMP, 0));

            // 不匹配的跳到这里
            chunk()->patchJump(noMatchJump);
        }

        // default
        emit(OpCode::OP_POP, 0); // pop subject
        if (expr->defaultBody) {
            compileNode(expr->defaultBody.get());
        }
        else {
            emit(OpCode::OP_NONE, 0);
        }

        for (int ej : endJumps) {
            chunk()->patchJump(ej);
        }

        return {};
    }

    // ★ Throw
    std::any Compiler::visitThrowExpr(ThrowExpr* expr) {
        compileNode(expr->value.get());
        emit(OpCode::OP_THROW, 0);
        return {};
    }

    std::any Compiler::visitTryCatchExpr(TryCatchExpr* expr) {
        uint16_t catchNameIdx = identifierConstant(expr->catchName.lexeme);

        // OP_TRY_BEGIN [catch_relative_offset:16] [catchNameIdx:16]
        emit(OpCode::OP_TRY_BEGIN, 0);
        int offsetSlot = static_cast<int>(chunk()->code.size());
        emit16(0, 0);             // catch offset 占位
        emit16(catchNameIdx, 0);  // catch 变量名索引

        // try 体
        compileNode(expr->tryBody.get());

        // 成功：移除 handler，跳过 catch
        emit(OpCode::OP_TRY_END, 0);
        int skipCatch = chunk()->emitJump(OpCode::OP_JUMP, 0);

        // catch 块开始
        int catchAddr = static_cast<int>(chunk()->code.size());

        int relOffset = catchAddr - (offsetSlot + 4);  // ★ +4 = 2(offset) + 2(nameIdx)
        chunk()->code[offsetSlot] = static_cast<uint8_t>((relOffset >> 8) & 0xFF);
        chunk()->code[offsetSlot + 1] = static_cast<uint8_t>(relOffset & 0xFF);

        // 此时 VM 已将错误消息 push 到栈上
        // 赋值给 catch 变量
        uint16_t nameIdx = identifierConstant(expr->catchName.lexeme);
        int slot = resolveLocal(expr->catchName.lexeme);
        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, 0);
            emit16(static_cast<uint16_t>(slot), 0);
        }
        else {
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(nameIdx, 0);
        }
        emit(OpCode::OP_POP, 0); // pop 赋值残留

        // catch 体
        compileNode(expr->catchBody.get());

        chunk()->patchJump(skipCatch);
        return {};
    }
    // ★ Dict Literal
    std::any Compiler::visitDictLiteral(DictLiteral* expr) {
        for (auto& [keyExpr, valExpr] : expr->entries) {
            compileNode(keyExpr.get());
            compileNode(valExpr.get());
        }
        emit(OpCode::OP_BUILD_DICT, 0);
        emit16(static_cast<uint16_t>(expr->entries.size()), 0);
        return {};
    }



    // ★ Const
    std::any Compiler::visitConstDecl(ConstDecl* expr) {
        compileNode(expr->value.get());
        const std::string& name = expr->name.lexeme;
        uint16_t idx = identifierConstant(name);
        emit(OpCode::OP_DEFINE_GLOBAL, 0);   // ★ 改为 DEFINE_GLOBAL（VM 中标记 const）
        emit16(idx, 0);
        return {};
    }

    // ★ Delete
    std::any Compiler::visitDeleteExpr(DeleteExpr* expr) {
        for (auto& tok : expr->names) {
            // ★ 压入变量名字符串常量
            chunk()->emitConstant(Value(tok.lexeme), 0);
            // ★ 调用 VM 内部的 __vm_delete__ 内建函数
            uint16_t fnIdx = identifierConstant("__vm_delete__");
            emit(OpCode::OP_CALL_BUILTIN, 0);
            emit16(fnIdx, 0);
            emit(static_cast<uint8_t>(1), 0);
            emit(OpCode::OP_POP, 0);           // 丢弃返回值
        }
        emit(OpCode::OP_NONE, 0);             // delete 表达式的结果
        return {};
    }

    // ★ Global
    std::any Compiler::visitGlobalDecl(GlobalDecl*) {
        // VM 中所有未解析为 local 的变量默认就是 global
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    // ★ F-String（增强：支持格式说明符）
    std::any Compiler::visitFStringExpr(FStringExpr* expr) {
        int partCount = 0;
        for (size_t i = 0; i < expr->exprs.size(); ++i) {
            if (!expr->literals[i].empty()) {
                chunk()->emitConstant(Value(expr->literals[i]), 0);
                partCount++;
            }
            compileNode(expr->exprs[i].get());
            if (!expr->formatSpecs[i].empty()) {
                // 存储格式说明符为常量，用 OP_FORMAT_STRING 处理
                uint16_t specIdx = makeConstant(Value(expr->formatSpecs[i]));
                emit(OpCode::OP_FORMAT_STRING, 0);
                emit16(specIdx, 0);
            }
            else {
                emit(OpCode::OP_STRINGIFY, 0);
            }
            partCount++;
        }
        if (!expr->literals.back().empty()) {
            chunk()->emitConstant(Value(expr->literals.back()), 0);
            partCount++;
        }
        emit(OpCode::OP_CONCAT_STRINGS, 0);
        emit16(static_cast<uint16_t>(partCount), 0);
        return {};
    }

    // ★ List Comprehension
    std::any Compiler::visitListCompExpr(ListCompExpr* expr) {
        // 压入空 List 作为累加器
        emit(OpCode::OP_LIST_INIT, 0);

        // 递归编译嵌套 for 子句
        compileCompClause(expr, 0);

        return {};
    }

    // ★ Import
    std::any Compiler::visitImportExpr(ImportExpr* expr) {
        compileNode(expr->path.get());
        emit(OpCode::OP_IMPORT, 0);
        return {};
    }

    // ★ Destructured For-In
    // 已在 visitForInExpr 中处理，需要扩展

    // ★ Class 定义
    std::any Compiler::visitClassDefExpr(ClassDefExpr* expr) {
        const std::string& className = expr->name.lexeme;
        uint16_t nameIdx = identifierConstant(className);

        // OP_CLASS name
        emit(OpCode::OP_CLASS, 0);
        emit16(nameIdx, 0);

        // 赋值到全局
        emit(OpCode::OP_SET_GLOBAL, 0);
        emit16(nameIdx, 0);

        // 继承
        if (!expr->superClassName.empty()) {
            uint16_t superIdx = identifierConstant(expr->superClassName);
            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(nameIdx, 0);
            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(superIdx, 0);
            emit(OpCode::OP_INHERIT, 0);
        }

        // 在 visitClassDefExpr 中，每个方法的编译循环内：
        for (auto& md : expr->methods) {
            auto fn = std::make_shared<CompiledFunction>();
            fn->name = md.name.lexeme;
            fn->maxArity = static_cast<int>(md.params.size());

            int required = 0;
            for (size_t i = 0; i < md.params.size(); ++i) {
                if (i >= md.defaultExprs.size() || !md.defaultExprs[i]) required++;
                else break;
            }
            fn->arity = required;
            compiledFunctions.push_back(fn);

            // ★★★ 立即锁定索引 ★★★
            int thisFnIndex = functionIndexOffset +
                static_cast<int>(compiledFunctions.size()) - 1;

            initCompiler(fn.get());
            beginScope();
            for (size_t i = 0; i < md.params.size(); ++i)
                addLocal(md.params[i].lexeme);
            fn->localCount = static_cast<int>(md.params.size());
            fn->paramIsRef = md.paramIsRef;

            emitDefaultPreamble(md.defaultExprs, fn->maxArity);

            compileNode(md.body.get());
            emit(OpCode::OP_RETURN, 0);
            endScope();
            stateStack.pop_back();

            // 在外层：获取类 → 创建闭包 → 添加方法
            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(nameIdx, 0);

            // ★★★ 使用锁定的索引 ★★★
            uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
            emit(OpCode::OP_CLOSURE, 0);
            emit16(fnIdx, 0);

            uint16_t methodNameIdx = identifierConstant(md.name.lexeme);
            emit(OpCode::OP_METHOD, 0);
            emit16(methodNameIdx, 0);

            emit(OpCode::OP_POP, 0);
        }

        return {};
    }

    // ★ Dot Access: obj.field
    std::any Compiler::visitDotAccess(DotAccess* expr) {
        // ★★★ Super 属性访问：super.field ★★★
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(selfIdx, 0);
            uint16_t nameIdx = identifierConstant(expr->field.lexeme);
            emit(OpCode::OP_GET_SUPER, 0);
            emit16(nameIdx, 0);
            return {};
        }

        // ── 普通属性访问（不变）──
        compileNode(expr->object.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_GET_PROPERTY, 0);
        emit16(nameIdx, 0);
        return {};
    }

    // ★ Dot Assign: obj.field = val
    std::any Compiler::visitDotAssign(DotAssign* expr) {
        compileNode(expr->object.get());
        compileNode(expr->value.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_SET_PROPERTY, 0);
        emit16(nameIdx, 0);
        // ★ 递归回写（Dict 值语义需要，Instance 引用语义无害）
        emitStoreTarget(expr->object.get());
        return {};
    }

    // ★ Method Call: obj.method(args)
    std::any Compiler::visitMethodCallExpr(MethodCallExpr* expr) {
        // ★★★ Super 方法调用：super.method(args) ★★★
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            // 压入 self（方法的接收者仍是当前实例）
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(selfIdx, 0);

            // 压入参数
            for (auto& arg : expr->arguments)
                compileNode(arg.get());

            // OP_SUPER_INVOKE [name_idx:16] [argc:8]
            uint16_t nameIdx = identifierConstant(expr->method.lexeme);
            emit(OpCode::OP_SUPER_INVOKE, 0);
            emit16(nameIdx, 0);
            emit(static_cast<uint8_t>(expr->arguments.size()), 0);
            return {};
        }

        // ── 普通方法调用（不变）──
        compileNode(expr->object.get());
        for (auto& arg : expr->arguments)
            compileNode(arg.get());
        uint16_t nameIdx = identifierConstant(expr->method.lexeme);
        emit(OpCode::OP_INVOKE, 0);
        emit16(nameIdx, 0);
        emit(static_cast<uint8_t>(expr->arguments.size()), 0);
        return {};
    }

    std::any Compiler::visitSuperExpr(SuperExpr*) {
        // ★ 裸 super 在 VM 中不应该单独出现
        // 所有有意义的 super 用法（super.method / super.field）
        // 已在 visitMethodCallExpr / visitDotAccess 中拦截
        throw std::runtime_error("Compiler Error: 'super' must be followed by '.method()'.");
    }

    // ══════════════════════════════════════════════
    // 未实现的节点
    // ══════════════════════════════════════════════

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4702)
#endif

    std::any Compiler::visitSliceExpr(SliceExpr*) {
        throw std::runtime_error("Compiler Error: Slice expression should be handled by visitIndexAccess.");
    }

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace jc

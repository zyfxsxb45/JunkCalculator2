#include "Compiler.h"
#include <functional>

namespace jc {

    void Compiler::emit(OpCode op, int line) {
        chunk()->write(op, line);
        if (line > 0) lastLine = line; // ★ 新增同步点
    }
    void Compiler::emit(uint8_t byte, int line) {
        chunk()->write(byte, line);
        if (line > 0) lastLine = line; // ★ 新增同步点
    }
    void Compiler::emit16(uint16_t val, int line) {
        chunk()->write16(val, line);
        if (line > 0) lastLine = line; // ★ 新增同步点
    }
    uint16_t Compiler::makeConstant(const Value& val) { return chunk()->addConstant(val); }
    uint16_t Compiler::identifierConstant(const std::string& name) { return makeConstant(Value(name)); }
    void Compiler::compileNode(Expr* expr) { expr->accept(*this); }

    void Compiler::initCompiler(CompiledFunction* fn) {
        CompilerState state;
        state.function = fn;
        state.scopeDepth = 0;
        state.maxLocals = 0;  // ★ 初始化峰值
        stateStack.push_back(state);
    }

    void Compiler::beginScope() { current().scopeDepth++; }

    void Compiler::endScope() {
        current().scopeDepth--;
        while (!current().locals.empty() && current().locals.back().depth > current().scopeDepth) {
            // ★ 删除 emit(OpCode::OP_POP, lastLine); 我们实施了分离式局部变量锚定，变量生生不息地待在栈底
            current().locals.pop_back();
        }
    }

    void Compiler::emitDefaultPreamble(
        const std::vector<std::shared_ptr<Expr>>& defaultExprs,
        int paramCount)
    {
        for (int i = 0; i < paramCount; ++i) {
            if (i < static_cast<int>(defaultExprs.size()) && defaultExprs[i]) {
                emit(OpCode::OP_GET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(i), lastLine);
                emit(OpCode::OP_NONE, lastLine);
                emit(OpCode::OP_EQUAL, lastLine);

                int skipJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
                emit(OpCode::OP_POP, lastLine);

                compileNode(defaultExprs[i].get());
                emit(OpCode::OP_SET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(i), lastLine);
                emit(OpCode::OP_POP, lastLine);

                int endJump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
                chunk()->patchJump(skipJump);
                emit(OpCode::OP_POP, lastLine);
                chunk()->patchJump(endJump);
            }
        }
    }

    void Compiler::emitStoreTarget(Expr* target) {
        uint16_t tmpIdx = identifierConstant("__compound_tmp__");

        if (auto* var = dynamic_cast<Variable*>(target)) {
            const std::string& name = var->name.lexeme;
            int slot = resolveLocal(name);

            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                if (resolveUpvalue(name) == -1) {
                    addLocal(name);
                    slot = resolveLocal(name);
                }
            }

            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);
            }
            else {
                int upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    emit(OpCode::OP_SET_UPVALUE, lastLine);
                    emit16(static_cast<uint16_t>(upvalue), lastLine);
                }
                else {
                    uint16_t nameIdx = identifierConstant(name);
                    emit(OpCode::OP_SET_GLOBAL, lastLine);
                    emit16(nameIdx, lastLine);
                }
            }
            return;
        }

        if (auto* dot = dynamic_cast<DotAccess*>(target)) {
            emit(OpCode::OP_SET_GLOBAL, lastLine);
            emit16(tmpIdx, lastLine);
            emit(OpCode::OP_POP, lastLine);

            compileNode(dot->object.get());

            emit(OpCode::OP_GET_GLOBAL, lastLine);
            emit16(tmpIdx, lastLine);

            uint16_t fieldIdx = identifierConstant(dot->field.lexeme);
            emit(OpCode::OP_SET_PROPERTY, lastLine);
            emit16(fieldIdx, lastLine);

            emitStoreTarget(dot->object.get());
            return;
        }

        if (auto* idx = dynamic_cast<IndexAccess*>(target)) {
            for (auto& i : idx->indices) {
                if (dynamic_cast<SliceExpr*>(i.get()))
                    throw std::runtime_error(
                        "Compiler Error: Slice compound assignment not supported in VM.");
            }
            uint8_t dimCount = static_cast<uint8_t>(idx->indices.size());

            emit(OpCode::OP_SET_GLOBAL, lastLine);
            emit16(tmpIdx, lastLine);
            emit(OpCode::OP_POP, lastLine);

            compileNode(idx->object.get());
            for (auto& i : idx->indices)
                compileNode(i.get());

            emit(OpCode::OP_GET_GLOBAL, lastLine);
            emit16(tmpIdx, lastLine);

            emit(OpCode::OP_INDEX_SET, lastLine);
            emit(dimCount, lastLine);

            emitStoreTarget(idx->object.get());
            return;
        }

        throw std::runtime_error(
            "Compiler Error: Cannot store to this expression type in compound assignment.");
    }

    void Compiler::compileCompClause(ListCompExpr* expr, size_t clauseIdx) {
        if (clauseIdx >= expr->clauses.size()) {
            uint16_t depth = static_cast<uint16_t>(2 * expr->clauses.size());

            if (expr->condition) {
                compileNode(expr->condition.get());
                int skipJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
                emit(OpCode::OP_POP, lastLine);

                compileNode(expr->valueExpr.get());
                emit(OpCode::OP_LIST_APPEND, lastLine);
                emit16(depth, lastLine);

                int endJump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
                chunk()->patchJump(skipJump);
                emit(OpCode::OP_POP, lastLine);
                chunk()->patchJump(endJump);
            }
            else {
                compileNode(expr->valueExpr.get());
                emit(OpCode::OP_LIST_APPEND, lastLine);
                emit16(depth, lastLine);
            }
            return;
        }

        auto& clause = expr->clauses[clauseIdx];
        compileNode(clause.iterable.get());
        emit(OpCode::OP_ITER_INIT, lastLine);
        emit(static_cast<uint8_t>(clause.isDestruct() ? 1 : 0), lastLine);

        int loopStart = static_cast<int>(chunk()->code.size());
        int exitJump = chunk()->emitJump(OpCode::OP_ITER_NEXT, lastLine);

        if (clause.isDestruct()) {
            int n = static_cast<int>(clause.destructNames.size());
            emit(OpCode::OP_DESTRUCT, lastLine);
            emit(static_cast<uint8_t>(n), lastLine);
            for (int j = n - 1; j >= 0; --j) {
                const std::string& name = clause.destructNames[j].lexeme;
                if (name == "_") { emit(OpCode::OP_POP, lastLine); continue; }

                int slot = resolveLocal(name);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                    if (resolveUpvalue(name) == -1) {
                        addLocal(name);
                        slot = resolveLocal(name);
                    }
                }
                if (slot != -1) {
                    emit(OpCode::OP_SET_LOCAL, lastLine);
                    emit16(static_cast<uint16_t>(slot), lastLine);
                }
                else {
                    uint16_t idx = identifierConstant(name);
                    emit(OpCode::OP_SET_GLOBAL, lastLine);
                    emit16(idx, lastLine);
                }
                emit(OpCode::OP_POP, lastLine);
            }
            emit(OpCode::OP_POP, lastLine);
        }
        else {
            const std::string& varName = clause.varName.lexeme;
            int slot = resolveLocal(varName);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(varName) == 0 && current().refNames.count(varName) == 0) {
                if (resolveUpvalue(varName) == -1) {
                    addLocal(varName);
                    slot = resolveLocal(varName);
                }
            }
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);
            }
            else {
                uint16_t idx = identifierConstant(varName);
                emit(OpCode::OP_SET_GLOBAL, lastLine);
                emit16(idx, lastLine);
            }
            emit(OpCode::OP_POP, lastLine);
        }

        compileCompClause(expr, clauseIdx + 1);

        chunk()->emitLoop(loopStart, lastLine);
        chunk()->patchJump(exitJump);

        emit(OpCode::OP_POP, lastLine);
        emit(OpCode::OP_POP, lastLine);
    }

    void Compiler::addLocal(const std::string& name) {
        current().locals.push_back({ name, current().scopeDepth });
        // ★ 跟踪峰值容量
        if (static_cast<int>(current().locals.size()) > current().maxLocals) {
            current().maxLocals = static_cast<int>(current().locals.size());
        }
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
        loopStack.push_back({ loopStart, {}, {}, current().scopeDepth, current().tryDepth });
    }

    void Compiler::endLoop() {
        loopStack.pop_back();
    }

    void Compiler::emitBreakJumps() {
        for (int offset : loopStack.back().breakJumps) {
            chunk()->patchJump(offset);
        }
    }

    Chunk Compiler::compile(Expr* ast, const std::string& sourceFile) {
        currentSourceFile = sourceFile; // 记住
        auto mainFn = std::make_shared<CompiledFunction>();
        mainFn->name = "<script>";
        mainFn->sourceFile = sourceFile; // ★ 打上文件烙印

        compiledFunctions.push_back(mainFn);
        initCompiler(mainFn.get());
        compileNode(ast);
        emit(OpCode::OP_RETURN, lastLine);
        mainFn->localCount = current().maxLocals;
        stateStack.pop_back();
        return mainFn->chunk;
    }


    std::any Compiler::visitLiteral(Literal* expr) {
        if (expr->isString) {
            chunk()->emitConstant(Value(expr->value), lastLine);
        }
        else if (expr->isImaginary) {
            chunk()->emitConstant(Value(Complex(0.0, std::stod(expr->value))), lastLine);
        }
        else {
            const std::string& s = expr->value;
            if (s.find('.') == std::string::npos &&
                s.find('e') == std::string::npos &&
                s.find('E') == std::string::npos) {
                try { chunk()->emitConstant(Value(BigInt(s)), lastLine); }
                catch (...) { chunk()->emitConstant(Value(std::stod(s)), lastLine); }
            }
            else {
                chunk()->emitConstant(Value(std::stod(s)), lastLine);
            }
        }
        return {};
    }

    std::any Compiler::visitVariable(Variable* expr) {
        lastLine = expr->name.line;
        const std::string& name = expr->name.lexeme;
        int slot = resolveLocal(name);
        if (slot != -1) {
            emit(OpCode::OP_GET_LOCAL, expr->name.line);
            emit16(static_cast<uint16_t>(slot), expr->name.line);
        }
        else {
            int upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_GET_UPVALUE, expr->name.line);
                emit16(static_cast<uint16_t>(upvalue), expr->name.line);
            }
            else {
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_GET_GLOBAL, expr->name.line);
                emit16(idx, expr->name.line);
            }
        }
        return {};
    }

    std::any Compiler::visitAssign(Assign* expr) {
        lastLine = expr->name.line;
        compileNode(expr->value.get());
        const std::string& name = expr->name.lexeme;

        if (stateStack.size() == 1) {
            knownGlobals.insert(name);
        }

        int slot = resolveLocal(name);
        int upvalue = -1;

        if (expr->isRef) {
            if (stateStack.size() <= 1) {
                throw std::runtime_error("Compiler Error: Cannot use 'ref' in global scope.");
            }
            current().refNames.insert(name); // ★ 提前注册，确保 resolveUpvalue 能感知到 isRef
            upvalue = resolveUpvalue(name);
            if (upvalue == -1 && current().globalNames.count(name) == 0 && knownGlobals.count(name) == 0) {
                throw std::runtime_error("Compiler Error: Cannot ref undefined outer variable '" + name + "'.");
            }
        } else {
            // ★ Auto-local Write (Shadowing)
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                addLocal(name);
                slot = resolveLocal(name);
            }
        }

        if (slot != -1 && !expr->isRef) {
            emit(OpCode::OP_SET_LOCAL, expr->name.line);
            emit16(static_cast<uint16_t>(slot), expr->name.line);
        }
        else {
            if (upvalue == -1) upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_SET_UPVALUE, expr->name.line);
                emit16(static_cast<uint16_t>(upvalue), expr->name.line);
            }
            else {
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_SET_GLOBAL, expr->name.line);
                emit16(idx, expr->name.line);
            }
        }
        return {};
    }

    std::any Compiler::visitUnary(Unary* expr) {
        lastLine = expr->op.line;
        compileNode(expr->right.get());
        switch (expr->op.type) {
        case TokenType::MINUS: emit(OpCode::OP_NEGATE, expr->op.line); break;
        case TokenType::BANG:  emit(OpCode::OP_NOT, expr->op.line); break;
        case TokenType::PLUS:  break;
        default: throw std::runtime_error("Compiler Error: Unknown unary operator.");
        }
        return {};
    }

    std::any Compiler::visitBinary(Binary* expr) {
        lastLine = expr->op.line;
        if (expr->op.type == TokenType::AND_AND) {
            compileNode(expr->left.get());
            int jump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, expr->op.line);
            emit(OpCode::OP_POP, expr->op.line);
            compileNode(expr->right.get());
            chunk()->patchJump(jump);
            return {};
        }
        if (expr->op.type == TokenType::OR_OR) {
            compileNode(expr->left.get());
            int elseJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, expr->op.line);
            int endJump = chunk()->emitJump(OpCode::OP_JUMP, expr->op.line);
            chunk()->patchJump(elseJump);
            emit(OpCode::OP_POP, expr->op.line);
            compileNode(expr->right.get());
            chunk()->patchJump(endJump);
            return {};
        }

        if (expr->op.type == TokenType::PIPE) {
            compileNode(expr->right.get());
            compileNode(expr->left.get());
            emit(OpCode::OP_CALL, expr->op.line);
            emit(1, expr->op.line);
            return {};
        }

        compileNode(expr->left.get());
        compileNode(expr->right.get());

        int line = expr->op.line;
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
        case TokenType::BIT_AND:       emit(OpCode::OP_BIT_AND, line); break;  // ★
        case TokenType::BIT_OR:        emit(OpCode::OP_BIT_OR, line); break;   // ★
        default:
            throw std::runtime_error("Compiler Error: Unsupported binary operator '" +
                expr->op.lexeme + "'.");
        }
        return {};
    }

    std::any Compiler::visitCall(Call* expr) {
        lastLine = expr->callee.line;
        const std::string& name = expr->callee.lexeme;
        int slot = resolveLocal(name);
        if (slot != -1) {
            emit(OpCode::OP_GET_LOCAL, expr->callee.line);
            emit16(static_cast<uint16_t>(slot), expr->callee.line);
        }
        else {
            int upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_GET_UPVALUE, expr->callee.line);
                emit16(static_cast<uint16_t>(upvalue), expr->callee.line);
            }
            else {
                // ★ 关键重构：将全局级别调用的目标变成字符串文字，把解析交接给 VM 的 OP_CALL 晚绑定操作
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_CONSTANT, expr->callee.line);
                emit16(idx, expr->callee.line);
            }
        }
        for (auto& argExpr : expr->arguments) {
            compileNode(argExpr.get());
        }
        emit(OpCode::OP_CALL, expr->callee.line);
        emit(static_cast<uint8_t>(expr->arguments.size()), expr->callee.line);

        bool hasVariableArgs = false;
        for (auto& argExpr : expr->arguments) {
            if (dynamic_cast<Variable*>(argExpr.get())) {
                hasVariableArgs = true;
                break;
            }
        }

        bool mayHaveRef = false;
        if (hasVariableArgs) {
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
            if (!foundDef) mayHaveRef = true;
        }

        if (mayHaveRef) {
            struct ArgSource { uint8_t argIndex; uint8_t sourceType; uint16_t sourceRef; };
            std::vector<ArgSource> sources;

            for (int i = 0; i < static_cast<int>(expr->arguments.size()); ++i) {
                if (auto* varExpr = dynamic_cast<Variable*>(expr->arguments[i].get())) {
                    int localSlot = resolveLocal(varExpr->name.lexeme);
                    if (localSlot != -1) {
                        sources.push_back({ static_cast<uint8_t>(i), 2, static_cast<uint16_t>(localSlot) });
                    }
                    else {
                        int uv = resolveUpvalue(varExpr->name.lexeme);
                        if (uv != -1) {
                            sources.push_back({ static_cast<uint8_t>(i), 3, static_cast<uint16_t>(uv) });
                        }
                        else {
                            uint16_t nameIdx = identifierConstant(varExpr->name.lexeme);
                            sources.push_back({ static_cast<uint8_t>(i), 1, nameIdx });
                        }
                    }
                }
            }

            if (!sources.empty()) {
                emit(OpCode::OP_REF_WRITEBACK, expr->callee.line);
                emit(static_cast<uint8_t>(sources.size()), expr->callee.line);
                for (auto& s : sources) {
                    emit(s.argIndex, expr->callee.line);
                    emit(s.sourceType, expr->callee.line);
                    emit16(s.sourceRef, expr->callee.line);
                }
            }
        }
        return {};
    }

    std::any Compiler::visitBlock(Block* expr) {
        beginScope();
        if (expr->statements.empty()) {
            emit(OpCode::OP_NONE, lastLine);
        }
        else {
            for (size_t i = 0; i < expr->statements.size(); ++i) {
                compileNode(expr->statements[i].get());
                if (i < expr->statements.size() - 1) {
                    emit(OpCode::OP_POP, lastLine);
                }
            }
        }

        current().scopeDepth--;
        while (!current().locals.empty() &&
            current().locals.back().depth > current().scopeDepth) {
            current().locals.pop_back();
        }
        return {};
    }

    std::any Compiler::visitIfExpr(IfExpr* expr) {
        compileNode(expr->condition.get());
        int thenJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
        emit(OpCode::OP_POP, lastLine);
        compileNode(expr->thenBranch.get());
        int elseJump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
        chunk()->patchJump(thenJump);
        emit(OpCode::OP_POP, lastLine);
        if (expr->elseBranch) {
            compileNode(expr->elseBranch.get());
        }
        else {
            emit(OpCode::OP_NONE, lastLine);
        }
        chunk()->patchJump(elseJump);
        return {};
    }

    std::any Compiler::visitWhileExpr(WhileExpr* expr) {
        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        compileNode(expr->condition.get());
        int exitJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
        emit(OpCode::OP_POP, lastLine);
        compileNode(expr->body.get());

        // ★ continue 跳转目标：就在 POP body result 之前
        for (int offset : loopStack.back().continueJumps) {
            chunk()->patchJump(offset);
        }

        emit(OpCode::OP_POP, lastLine);   // POP body result（或 continue 填充的 NONE）
        chunk()->emitLoop(loopStart, lastLine);

        chunk()->patchJump(exitJump);
        emit(OpCode::OP_POP, lastLine);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_NONE, lastLine);
        return {};
    }

    std::any Compiler::visitForExpr(ForExpr* expr) {
        beginScope();
        compileNode(expr->initializer.get());
        emit(OpCode::OP_POP, lastLine);

        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        compileNode(expr->condition.get());
        int exitJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
        emit(OpCode::OP_POP, lastLine);
        compileNode(expr->body.get());

        // ★ continue 跳转目标：POP body result，然后执行 update
        for (int offset : loopStack.back().continueJumps) {
            chunk()->patchJump(offset);
        }

        emit(OpCode::OP_POP, lastLine);   // POP body result
        compileNode(expr->update.get());
        emit(OpCode::OP_POP, lastLine);   // POP update result
        chunk()->emitLoop(loopStart, lastLine);

        chunk()->patchJump(exitJump);
        emit(OpCode::OP_POP, lastLine);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_NONE, lastLine);
        endScope();
        return {};
    }

    std::any Compiler::visitFunctionDef(FunctionDef* expr) {
        lastLine = expr->name.line;
        const std::string& funcName = expr->name.lexeme;
        
        if (stateStack.size() == 1) {
            knownGlobals.insert(funcName);
        }

        // ★ 修复：先在当前外层注册函数的 Local / Upvalue 引用权限，确保允许在内部自递归捕获
        int outerSlot = resolveLocal(funcName);
        if (stateStack.size() > 1 && outerSlot == -1 && current().globalNames.count(funcName) == 0 && current().refNames.count(funcName) == 0) {
            if (resolveUpvalue(funcName) == -1) {
                addLocal(funcName);
            }
        }
        outerSlot = resolveLocal(funcName);
        // ★ 加强版初始化：包含变长标志透传与必填项扣除
        auto fn = std::make_shared<CompiledFunction>();
        fn->name = funcName;
        fn->maxArity = static_cast<int>(expr->params.size());
        fn->hasRestParam = expr->hasRestParam;

        int requiredParams = 0;
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i >= expr->defaultExprs.size() || !expr->defaultExprs[i]) {
                if (expr->hasRestParam && i == expr->params.size() - 1) break;
                requiredParams++;
            }
            else break;
        }
        fn->arity = requiredParams;

        compiledFunctions.push_back(fn);

        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        fn->paramIsRef = expr->paramIsRef; // ★ 保留：记录引用的参数标记

        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);

        // ★ 幽灵注入：参数类型检查
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i < expr->paramTypes.size() && !expr->paramTypes[i].empty()) {
                int slot = resolveLocal(expr->params[i].lexeme);
                emit(OpCode::OP_GET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);

                uint16_t typeIdx = identifierConstant(expr->paramTypes[i]);
                uint16_t nameIdx = identifierConstant(expr->params[i].lexeme);
                emit(OpCode::OP_ASSERT_PARAM_TYPE, lastLine);
                emit16(typeIdx, lastLine);
                emit16(nameIdx, lastLine);
            }
        }
        current().expectedReturnType = expr->returnType; // 记录期盼的类型
        compileNode(expr->body.get());

        // ★ 幽灵注入：隐式返回值的类型检查
        if (!expr->returnType.empty()) {
            uint16_t typeIdx = identifierConstant(expr->returnType);
            emit(OpCode::OP_ASSERT_RETURN_TYPE, lastLine);
            emit16(typeIdx, lastLine);
        }

        emit(OpCode::OP_RETURN, lastLine);
        fn->localCount = current().maxLocals;
        endScope();
        stateStack.pop_back();

        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, expr->name.line);
        emit16(fnIdx, expr->name.line);

        // ★ 使用提前抢占好的 outerSlot 并设定值
        if (outerSlot != -1) {
            emit(OpCode::OP_SET_LOCAL, expr->name.line);
            emit16(static_cast<uint16_t>(outerSlot), expr->name.line);

            uint16_t globalIdx = identifierConstant(funcName);
            emit(OpCode::OP_SET_GLOBAL, expr->name.line);
            emit16(globalIdx, expr->name.line);
        }
        else {
            int upvalue = resolveUpvalue(funcName);
            if (upvalue != -1) {
                emit(OpCode::OP_SET_UPVALUE, expr->name.line);
                emit16(static_cast<uint16_t>(upvalue), expr->name.line);
            }
            else {
                uint16_t nameIdx = identifierConstant(funcName);
                emit(OpCode::OP_SET_GLOBAL, expr->name.line);
                emit16(nameIdx, expr->name.line);
            }
        }
        return {};
    }

    std::any Compiler::visitLambdaExpr(LambdaExpr* expr) {
        auto fn = std::make_shared<CompiledFunction>();
        fn->name = "<lambda>";
        fn->maxArity = static_cast<int>(expr->params.size());
        fn->hasRestParam = expr->hasRestParam;

        int requiredParams = 0;
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i >= expr->defaultExprs.size() || !expr->defaultExprs[i]) {
                if (expr->hasRestParam && i == expr->params.size() - 1) break;
                requiredParams++;
            }
            else break;
        }
        fn->arity = requiredParams;

        compiledFunctions.push_back(fn);

        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);


        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i < expr->paramTypes.size() && !expr->paramTypes[i].empty()) {
                int slot = resolveLocal(expr->params[i].lexeme);
                emit(OpCode::OP_GET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine);

                uint16_t typeIdx = identifierConstant(expr->paramTypes[i]);
                uint16_t nameIdx = identifierConstant(expr->params[i].lexeme);
                emit(OpCode::OP_ASSERT_PARAM_TYPE, lastLine);
                emit16(typeIdx, lastLine); emit16(nameIdx, lastLine);
            }
        }
        current().expectedReturnType = expr->returnType;
        compileNode(expr->body.get());
        if (!expr->returnType.empty()) {
            uint16_t typeIdx = identifierConstant(expr->returnType);
            emit(OpCode::OP_ASSERT_RETURN_TYPE, lastLine);
            emit16(typeIdx, lastLine);
        }
        emit(OpCode::OP_RETURN, lastLine);

        // ★ 修改：提取峰值
        fn->localCount = current().maxLocals;

        endScope();
        stateStack.pop_back();

        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, lastLine);
        emit16(fnIdx, lastLine);

        return {};
    }

    int Compiler::addUpvalue(int level, const std::string& name,
        bool isLocal, int index, bool isRef) {
        auto* fn = stateStack[level].function;
        for (int j = 0; j < static_cast<int>(fn->upvalues.size()); ++j) {
            if (fn->upvalues[j].name == name) {
                if (isRef) fn->upvalues[j].isRef = true; // ★ 升级为引用捕获
                return j;
            }
        }
        fn->upvalues.push_back({ name, isLocal, index, isRef });
        return static_cast<int>(fn->upvalues.size()) - 1;
    }

    int Compiler::resolveUpvalueAt(int level, const std::string& name, bool isRef) {
        if (level <= 0) return -1;
        int enclosingLevel = level - 1;
        auto& enclosing = stateStack[enclosingLevel];

        for (int i = static_cast<int>(enclosing.locals.size()) - 1; i >= 0; --i) {
            if (enclosing.locals[i].name == name) return addUpvalue(level, name, true, i, isRef);
        }

        int upvalueInEnclosing = resolveUpvalueAt(enclosingLevel, name, isRef);
        if (upvalueInEnclosing != -1) return addUpvalue(level, name, false, upvalueInEnclosing, isRef);

        return -1;
    }

    int Compiler::resolveUpvalue(const std::string& name) {
        int currentLevel = static_cast<int>(stateStack.size()) - 1;
        bool isRef = stateStack[currentLevel].refNames.count(name) > 0;
        return resolveUpvalueAt(currentLevel, name, isRef);
    }

    std::any Compiler::visitReturnExpr(ReturnExpr* expr) {
        if (expr->value) compileNode(expr->value.get());
        else emit(OpCode::OP_NONE, lastLine);

        // ★ 幽灵注入：手工书写的返回值检查
        if (!current().expectedReturnType.empty()) {
            uint16_t typeIdx = identifierConstant(current().expectedReturnType);
            emit(OpCode::OP_ASSERT_RETURN_TYPE, lastLine);
            emit16(typeIdx, lastLine);
        }

        for (int i = 0; i < current().tryDepth; ++i) emit(OpCode::OP_TRY_END, lastLine);
        emit(OpCode::OP_RETURN, lastLine);
        return {};
    }

    std::any Compiler::visitBreakExpr(BreakExpr*) {
        if (loopStack.empty()) throw std::runtime_error("Compiler Error: 'break' outside loop.");

        // ★ 智能发散机制：清理掉在这层跳出沿途中遇到的所有 Try 处理器
        int diff = current().tryDepth - loopStack.back().tryDepth;
        for (int i = 0; i < diff; ++i) emit(OpCode::OP_TRY_END, lastLine);
        int jump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
        loopStack.back().breakJumps.push_back(jump);
        return {};
    }

    std::any Compiler::visitContinueExpr(ContinueExpr*) {
        if (loopStack.empty()) throw std::runtime_error("Compiler Error: 'continue' outside loop.");
        emit(OpCode::OP_NONE, lastLine);
        // ★ 同样智能清理
        int diff = current().tryDepth - loopStack.back().tryDepth;
        for (int i = 0; i < diff; ++i) emit(OpCode::OP_TRY_END, lastLine);
        int jump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
        loopStack.back().continueJumps.push_back(jump);
        return {};
    }

    std::any Compiler::visitCompoundAssign(CompoundAssign* expr) {
        auto emitOp = [this](TokenType op) {
            switch (op) {
            case TokenType::PLUS:    emit(OpCode::OP_ADD, lastLine); break;
            case TokenType::MINUS:   emit(OpCode::OP_SUBTRACT, lastLine); break;
            case TokenType::STAR:    emit(OpCode::OP_MULTIPLY, lastLine); break;
            case TokenType::SLASH:   emit(OpCode::OP_DIVIDE, lastLine); break;
            case TokenType::PERCENT: emit(OpCode::OP_MODULO, lastLine); break;
            case TokenType::CARET:   emit(OpCode::OP_POWER, lastLine); break;
            case TokenType::BIT_AND: emit(OpCode::OP_BIT_AND, lastLine); break; // ★
            case TokenType::BIT_OR:  emit(OpCode::OP_BIT_OR, lastLine); break;  // ★
            default: throw std::runtime_error("Compiler Error: Unknown compound operator.");
            }
            };

        if (auto* var = dynamic_cast<Variable*>(expr->target.get())) {
            const std::string& name = var->name.lexeme;
            
            if (stateStack.size() == 1) {
                knownGlobals.insert(name);
            }

            int slot = resolveLocal(name);
            int upvalue = -1;

            if (expr->isRef) {
                if (stateStack.size() <= 1) {
                    throw std::runtime_error("Compiler Error: Cannot use 'ref' in global scope.");
                }
                current().refNames.insert(name); // ★ 提前注册
                upvalue = resolveUpvalue(name);
                if (upvalue == -1 && current().globalNames.count(name) == 0 && knownGlobals.count(name) == 0) {
                    throw std::runtime_error("Compiler Error: Cannot ref undefined outer variable '" + name + "'.");
                }
            }

            // 读取当前值
            if (slot != -1 && !expr->isRef) {
                emit(OpCode::OP_GET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);
            }
            else {
                if (upvalue == -1) upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    emit(OpCode::OP_GET_UPVALUE, lastLine);
                    emit16(static_cast<uint16_t>(upvalue), lastLine);
                }
                else {
                    uint16_t idx = identifierConstant(name);
                    emit(OpCode::OP_GET_GLOBAL, lastLine);
                    emit16(idx, lastLine);
                }
            }

            compileNode(expr->value.get());
            emitOp(expr->op);

            if (!expr->isRef) {
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                    if (resolveUpvalue(name) == -1) {
                        addLocal(name);
                        slot = resolveLocal(name);
                    }
                }
            }

            if (slot != -1 && !expr->isRef) {
                emit(OpCode::OP_SET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);
            }
            else {
                if (upvalue == -1) upvalue = resolveUpvalue(name);
                if (upvalue != -1) {
                    emit(OpCode::OP_SET_UPVALUE, lastLine);
                    emit16(static_cast<uint16_t>(upvalue), lastLine);
                }
                else {
                    uint16_t idx = identifierConstant(name);
                    emit(OpCode::OP_SET_GLOBAL, lastLine);
                    emit16(idx, lastLine);
                }
            }
            return {};
        }

        if (dynamic_cast<DotAccess*>(expr->target.get()) ||
            dynamic_cast<IndexAccess*>(expr->target.get())) {
            std::function<void(Expr*)> checkSlice = [&](Expr* e) {
                if (auto* idx = dynamic_cast<IndexAccess*>(e)) {
                    for (auto& i : idx->indices)
                        if (dynamic_cast<SliceExpr*>(i.get()))
                            throw std::runtime_error(
                                "Compiler Error: Slice compound assignment not supported in VM.");
                    checkSlice(idx->object.get());
                }
                };
            checkSlice(expr->target.get());
            compileNode(expr->target.get());
            compileNode(expr->value.get());
            emitOp(expr->op);
            emitStoreTarget(expr->target.get());
            return {};
        }

        throw std::runtime_error("Compiler Error: Compound assignment target not supported in VM.");
    }

    std::any Compiler::visitForInExpr(ForInExpr* expr) {
        compileNode(expr->iterable.get());
        emit(OpCode::OP_ITER_INIT, lastLine);
        emit(static_cast<uint8_t>(expr->isDestruct() ? 1 : 0), lastLine);

        int loopStart = static_cast<int>(chunk()->code.size());
        beginLoop(loopStart);

        int exitJump = chunk()->emitJump(OpCode::OP_ITER_NEXT, lastLine);

        if (expr->isDestruct()) {
            int n = static_cast<int>(expr->destructNames.size());
            emit(OpCode::OP_DESTRUCT, lastLine);
            emit(static_cast<uint8_t>(n), lastLine);
            for (int j = n - 1; j >= 0; --j) {
                const std::string& name = expr->destructNames[j].lexeme;
                if (name == "_") { emit(OpCode::OP_POP, lastLine); continue; }
                
                if (stateStack.size() == 1) knownGlobals.insert(name);

                int slot = resolveLocal(name);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                    if (resolveUpvalue(name) == -1) {
                        addLocal(name); slot = resolveLocal(name);
                    }
                }
                if (slot != -1) { emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
                else { uint16_t idx = identifierConstant(name); emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(idx, lastLine); }
                emit(OpCode::OP_POP, lastLine);
            }
            emit(OpCode::OP_POP, lastLine);
        }
        else {
            const std::string& varName = expr->varName.lexeme;
            
            if (stateStack.size() == 1) knownGlobals.insert(varName);

            int slot = resolveLocal(varName);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(varName) == 0 && current().refNames.count(varName) == 0) {
                if (resolveUpvalue(varName) == -1) {
                    addLocal(varName); slot = resolveLocal(varName);
                }
            }
            if (slot != -1) { emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
            else { uint16_t idx = identifierConstant(varName); emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(idx, lastLine); }
            emit(OpCode::OP_POP, lastLine);
        }

        compileNode(expr->body.get());

        // ★ continue 跳转目标：POP body result
        for (int offset : loopStack.back().continueJumps) {
            chunk()->patchJump(offset);
        }

        emit(OpCode::OP_POP, lastLine);   // POP body result
        chunk()->emitLoop(loopStart, lastLine);
        chunk()->patchJump(exitJump);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_POP, lastLine);   // pop iterator element
        emit(OpCode::OP_POP, lastLine);   // pop List
        emit(OpCode::OP_NONE, lastLine);
        return {};
    }

    std::any Compiler::visitMatrixNode(MatrixNode* expr) {
        int rows = static_cast<int>(expr->elements.size());
        if (rows == 0) {
            chunk()->emitConstant(Value(RealMatrix(0, 0)), lastLine);
            return {};
        }
        int cols = static_cast<int>(expr->elements[0].size());
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                compileNode(expr->elements[i][j].get());
            }
        }
        emit(OpCode::OP_BUILD_MATRIX, lastLine);
        emit16(static_cast<uint16_t>(rows), lastLine);
        emit16(static_cast<uint16_t>(cols), lastLine);
        return {};
    }

    std::any Compiler::visitIndexAccess(IndexAccess* expr) {
        bool hasSlice = false;
        for (auto& idx : expr->indices) {
            if (dynamic_cast<SliceExpr*>(idx.get())) {
                hasSlice = true;
                break;
            }
        }

        if (hasSlice) {
            compileNode(expr->object.get());
            for (auto& idx : expr->indices) {
                if (auto* slice = dynamic_cast<SliceExpr*>(idx.get())) {
                    if (slice->start) compileNode(slice->start.get()); else emit(OpCode::OP_NONE, lastLine);
                    if (slice->end) compileNode(slice->end.get()); else emit(OpCode::OP_NONE, lastLine);
                    if (slice->step) compileNode(slice->step.get()); else emit(OpCode::OP_NONE, lastLine);
                }
                else {
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, lastLine);       // end = none
                    chunk()->emitConstant(Value(0.0), lastLine);  // ★ step = 0 (点索引标记)
                }
            }
            emit(OpCode::OP_SLICE_GET, lastLine);
            emit(static_cast<uint8_t>(expr->indices.size()), lastLine);
            return {};
        }
        compileNode(expr->object.get());
        for (auto& idx : expr->indices) compileNode(idx.get());
        emit(OpCode::OP_INDEX_GET, lastLine);
        emit(static_cast<uint8_t>(expr->indices.size()), lastLine);
        return {};
    }

    std::any Compiler::visitIndexAssign(IndexAssign* expr) {
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
            if (expr->hasObjectExpr()) compileNode(expr->objectExpr.get());
            else {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) { emit(OpCode::OP_GET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
                else {
                    // ★ 修改：引入 Upvalue 读取
                    int upvalue = resolveUpvalue(expr->name.lexeme);
                    if (upvalue != -1) { emit(OpCode::OP_GET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
                    else { uint16_t idx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(idx, lastLine); }
                }
            }

            for (auto& idx : expr->indexChain[0]) {
                if (auto* slice = dynamic_cast<SliceExpr*>(idx.get())) {
                    if (slice->start) compileNode(slice->start.get()); else emit(OpCode::OP_NONE, lastLine);
                    if (slice->end) compileNode(slice->end.get()); else emit(OpCode::OP_NONE, lastLine);
                    if (slice->step) compileNode(slice->step.get()); else emit(OpCode::OP_NONE, lastLine);
                }
                else {
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, lastLine);
                    chunk()->emitConstant(Value(0.0), lastLine);  // ★ step = 0 (点索引标记)
                }
            }

            compileNode(expr->value.get());
            emit(OpCode::OP_SLICE_SET, lastLine);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), lastLine);

            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->name.lexeme) == 0 && current().refNames.count(expr->name.lexeme) == 0) {
                    if (resolveUpvalue(expr->name.lexeme) == -1) {
                        addLocal(expr->name.lexeme); slot = resolveLocal(expr->name.lexeme);
                    }
                }
                if (slot != -1) { emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
                else {
                    // ★ 修改：引入 Upvalue 写入
                    int upvalue = resolveUpvalue(expr->name.lexeme);
                    if (upvalue != -1) { emit(OpCode::OP_SET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
                    else { uint16_t idx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(idx, lastLine); }
                }
            }
            else { emitStoreTarget(expr->objectExpr.get()); }
            return {};
        }

        int depth = static_cast<int>(expr->indexChain.size());
        auto emitLoadRoot = [&]() {
            if (expr->hasObjectExpr()) compileNode(expr->objectExpr.get());
            else {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) { emit(OpCode::OP_GET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
                else {
                    // ★ 修改引入读取环境
                    int upvalue = resolveUpvalue(expr->name.lexeme);
                    if (upvalue != -1) { emit(OpCode::OP_GET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
                    else { uint16_t nameIdx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(nameIdx, lastLine); }
                }
            }
            };

        auto emitStoreRoot = [&]() {
            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->name.lexeme) == 0 && current().refNames.count(expr->name.lexeme) == 0) {
                    if (resolveUpvalue(expr->name.lexeme) == -1) {
                        addLocal(expr->name.lexeme); slot = resolveLocal(expr->name.lexeme);
                    }
                }
                if (slot != -1) { emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
                else {
                    // ★ 修改引入写入环境
                    int upvalue = resolveUpvalue(expr->name.lexeme);
                    if (upvalue != -1) { emit(OpCode::OP_SET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
                    else { uint16_t nameIdx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(nameIdx, lastLine); }
                }
            }
            else emitStoreTarget(expr->objectExpr.get());
            };

        if (depth == 1) {
            emitLoadRoot();
            for (auto& idx : expr->indexChain[0]) compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, lastLine);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), lastLine);
            emitStoreRoot();
        }
        else {
            uint16_t tmpIdx = identifierConstant("__idx_chain_tmp__");
            emitLoadRoot();
            for (int level = 0; level < depth - 1; ++level) {
                for (auto& idx : expr->indexChain[level]) compileNode(idx.get());
                emit(OpCode::OP_INDEX_GET, lastLine);
                emit(static_cast<uint8_t>(expr->indexChain[level].size()), lastLine);
            }
            for (auto& idx : expr->indexChain[depth - 1]) compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, lastLine);
            emit(static_cast<uint8_t>(expr->indexChain[depth - 1].size()), lastLine);

            for (int level = depth - 2; level >= 0; --level) {
                emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(tmpIdx, lastLine); emit(OpCode::OP_POP, lastLine);
                emitLoadRoot();
                for (int l = 0; l < level; ++l) {
                    for (auto& idx : expr->indexChain[l]) compileNode(idx.get());
                    emit(OpCode::OP_INDEX_GET, lastLine); emit(static_cast<uint8_t>(expr->indexChain[l].size()), lastLine);
                }
                for (auto& idx : expr->indexChain[level]) compileNode(idx.get());
                emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(tmpIdx, lastLine);
                emit(OpCode::OP_INDEX_SET, lastLine); emit(static_cast<uint8_t>(expr->indexChain[level].size()), lastLine);
            }
            emitStoreRoot();
        }
        return {};
    }

    std::any Compiler::visitInvokeExpr(InvokeExpr* expr) {
        compileNode(expr->callee.get());
        for (auto& argExpr : expr->arguments) compileNode(argExpr.get());
        emit(OpCode::OP_CALL, lastLine);
        emit(static_cast<uint8_t>(expr->arguments.size()), lastLine);

        bool hasVariableArgs = false;
        for (auto& argExpr : expr->arguments) {
            if (dynamic_cast<Variable*>(argExpr.get())) { hasVariableArgs = true; break; }
        }

        if (hasVariableArgs) {
            struct ArgSource { uint8_t argIndex; uint8_t sourceType; uint16_t sourceRef; };
            std::vector<ArgSource> sources;

            for (int i = 0; i < static_cast<int>(expr->arguments.size()); ++i) {
                if (auto* varExpr = dynamic_cast<Variable*>(expr->arguments[i].get())) {
                    int localSlot = resolveLocal(varExpr->name.lexeme);
                    if (localSlot != -1) sources.push_back({ static_cast<uint8_t>(i), 2, static_cast<uint16_t>(localSlot) });
                    else {
                        int uv = resolveUpvalue(varExpr->name.lexeme);
                        if (uv != -1) sources.push_back({ static_cast<uint8_t>(i), 3, static_cast<uint16_t>(uv) });
                        else sources.push_back({ static_cast<uint8_t>(i), 1, identifierConstant(varExpr->name.lexeme) });
                    }
                }
            }

            if (!sources.empty()) {
                emit(OpCode::OP_REF_WRITEBACK, lastLine);
                emit(static_cast<uint8_t>(sources.size()), lastLine);
                for (auto& s : sources) { emit(s.argIndex, lastLine); emit(s.sourceType, lastLine); emit16(s.sourceRef, lastLine); }
            }
        }
        return {};
    }

    std::any Compiler::visitDestructAssign(DestructAssign* expr) {
        compileNode(expr->value.get());
        int n = static_cast<int>(expr->names.size());
        emit(OpCode::OP_DESTRUCT, lastLine);
        emit(static_cast<uint8_t>(n), lastLine);

        for (int i = n - 1; i >= 0; --i) {
            const std::string& name = expr->names[i].lexeme;
            if (name == "_") { emit(OpCode::OP_POP, lastLine); continue; }

            if (stateStack.size() == 1) knownGlobals.insert(name);

            int slot = resolveLocal(name);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0 && current().refNames.count(name) == 0) {
                if (resolveUpvalue(name) == -1) {
                    addLocal(name);
                    slot = resolveLocal(name);
                }
            }

            if (slot != -1) { emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
            else { uint16_t idx = identifierConstant(name); emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(idx, lastLine); }
            emit(OpCode::OP_POP, lastLine);
        }
        return {};
    }

    std::any Compiler::visitSwitchExpr(SwitchExpr* expr) {
        compileNode(expr->subject.get());
        std::vector<int> endJumps;

        for (auto& [values, body] : expr->cases) {
            std::vector<int> bodyJumps;
            int noMatchJump = -1;

            for (size_t vi = 0; vi < values.size(); ++vi) {
                emit(OpCode::OP_DUP, lastLine);
                compileNode(values[vi].get());
                emit(OpCode::OP_EQUAL, lastLine);
                int matchJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, lastLine);
                emit(OpCode::OP_POP, lastLine);
                int toBody = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
                bodyJumps.push_back(toBody);
                chunk()->patchJump(matchJump);
                emit(OpCode::OP_POP, lastLine);
            }

            noMatchJump = chunk()->emitJump(OpCode::OP_JUMP, lastLine);
            for (int bj : bodyJumps) chunk()->patchJump(bj);
            emit(OpCode::OP_POP, lastLine);
            compileNode(body.get());
            endJumps.push_back(chunk()->emitJump(OpCode::OP_JUMP, lastLine));
            chunk()->patchJump(noMatchJump);
        }
        emit(OpCode::OP_POP, lastLine);
        if (expr->defaultBody) compileNode(expr->defaultBody.get());
        else emit(OpCode::OP_NONE, lastLine);

        for (int ej : endJumps) chunk()->patchJump(ej);
        return {};
    }

    std::any Compiler::visitThrowExpr(ThrowExpr* expr) {
        compileNode(expr->value.get());
        emit(OpCode::OP_THROW, lastLine);
        return {};
    }

    std::any Compiler::visitTryCatchExpr(TryCatchExpr* expr) {
        uint16_t catchNameIdx = identifierConstant(expr->catchName.lexeme);
        emit(OpCode::OP_TRY_BEGIN, lastLine);
        int offsetSlot = static_cast<int>(chunk()->code.size());
        emit16(0, lastLine);
        emit16(catchNameIdx, lastLine);
        current().tryDepth++;                 // ★ 进入 try 块
        compileNode(expr->tryBody.get());
        current().tryDepth--;                 // ★ 离开 try 块
        emit(OpCode::OP_TRY_END, lastLine);
        int skipCatch = chunk()->emitJump(OpCode::OP_JUMP, lastLine);

        int catchAddr = static_cast<int>(chunk()->code.size());
        int relOffset = catchAddr - (offsetSlot + 4);
        chunk()->code[offsetSlot] = static_cast<uint8_t>((relOffset >> 8) & 0xFF);
        chunk()->code[offsetSlot + 1] = static_cast<uint8_t>(relOffset & 0xFF);

        if (stateStack.size() == 1) knownGlobals.insert(expr->catchName.lexeme);

        int slot = resolveLocal(expr->catchName.lexeme);
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->catchName.lexeme) == 0 && current().refNames.count(expr->catchName.lexeme) == 0) {
            if (resolveUpvalue(expr->catchName.lexeme) == -1) {
                addLocal(expr->catchName.lexeme);
                slot = resolveLocal(expr->catchName.lexeme);
            }
        }
        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, lastLine);
            emit16(static_cast<uint16_t>(slot), lastLine);
        }
        else {
            // ★ 修复：在这里补充未定义的 nameIdx
            uint16_t nameIdx = identifierConstant(expr->catchName.lexeme);
            emit(OpCode::OP_SET_GLOBAL, lastLine);
            emit16(nameIdx, lastLine);
        }
        emit(OpCode::OP_POP, lastLine);

        compileNode(expr->catchBody.get());
        chunk()->patchJump(skipCatch);
        return {};
    }

    std::any Compiler::visitDictLiteral(DictLiteral* expr) {
        for (auto& [keyExpr, valExpr] : expr->entries) {
            compileNode(keyExpr.get());
            compileNode(valExpr.get());
        }
        emit(OpCode::OP_BUILD_DICT, lastLine);
        emit16(static_cast<uint16_t>(expr->entries.size()), lastLine);
        return {};
    }

    std::any Compiler::visitRefDecl(RefDecl* expr) {
        lastLine = expr->name.line;
        const std::string& name = expr->name.lexeme;
        if (stateStack.size() <= 1) {
            throw std::runtime_error("Compiler Error: Cannot use 'ref' in global scope.");
        }
        int upvalue = resolveUpvalue(name);
        if (upvalue == -1 && current().globalNames.count(name) == 0 && knownGlobals.count(name) == 0) {
            throw std::runtime_error("Compiler Error: Cannot ref undefined outer variable '" + name + "'.");
        }
        current().refNames.insert(name);
        emit(OpCode::OP_NONE, lastLine);
        return {};
    }

    std::any Compiler::visitConstDecl(ConstDecl* expr) {
        compileNode(expr->value.get());
        const std::string& name = expr->name.lexeme;

        if (stateStack.size() == 1) knownGlobals.insert(name);

        // Const is inherently global/outermost scope definition behavior in VM. 
        // We emit DEFINE_GLOBAL directly.
        uint16_t idx = identifierConstant(name);
        emit(OpCode::OP_DEFINE_GLOBAL, lastLine);
        emit16(idx, lastLine);
        return {};
    }

    std::any Compiler::visitDeleteExpr(DeleteExpr* expr) {
        for (auto& tok : expr->names) {
            uint16_t fnIdx = identifierConstant("__vm_delete__");
            emit(OpCode::OP_CONSTANT, lastLine);
            emit16(fnIdx, lastLine);
            chunk()->emitConstant(Value(tok.lexeme), lastLine);
            emit(OpCode::OP_CALL, lastLine);
            emit(static_cast<uint8_t>(1), lastLine);
            emit(OpCode::OP_POP, lastLine);
        }
        emit(OpCode::OP_NONE, lastLine);
        return {};
    }

    std::any Compiler::visitGlobalDecl(GlobalDecl* expr) {
        for (const auto& tok : expr->names) {
            current().globalNames.insert(tok.lexeme);
        }
        emit(OpCode::OP_NONE, lastLine);
        return {};
    }

    std::any Compiler::visitFStringExpr(FStringExpr* expr) {
        int partCount = 0;
        for (size_t i = 0; i < expr->exprs.size(); ++i) {
            if (!expr->literals[i].empty()) {
                chunk()->emitConstant(Value(expr->literals[i]), lastLine);
                partCount++;
            }
            compileNode(expr->exprs[i].get());
            if (!expr->formatSpecs[i].empty()) {
                uint16_t specIdx = makeConstant(Value(expr->formatSpecs[i]));
                emit(OpCode::OP_FORMAT_STRING, lastLine);
                emit16(specIdx, lastLine);
            }
            else {
                emit(OpCode::OP_STRINGIFY, lastLine);
            }
            partCount++;
        }
        if (!expr->literals.back().empty()) {
            chunk()->emitConstant(Value(expr->literals.back()), lastLine);
            partCount++;
        }
        emit(OpCode::OP_CONCAT_STRINGS, lastLine);
        emit16(static_cast<uint16_t>(partCount), lastLine);
        return {};
    }

    std::any Compiler::visitListCompExpr(ListCompExpr* expr) {
        emit(OpCode::OP_LIST_INIT, lastLine);
        compileCompClause(expr, 0);
        return {};
    }

    std::any Compiler::visitImportExpr(ImportExpr* expr) {
        compileNode(expr->path.get());
        emit(OpCode::OP_IMPORT, lastLine);
        return {};
    }

    std::any Compiler::visitClassDefExpr(ClassDefExpr* expr) {
        lastLine = expr->name.line;
        const std::string& className = expr->name.lexeme;
        uint16_t nameIdx = identifierConstant(className);

        // 1. 生成空的类定义对象
        emit(OpCode::OP_CLASS, lastLine);
        emit16(nameIdx, lastLine);

        // 2. 将类保存进环境（局部/闭包/全局 安全判定）
        int slot = resolveLocal(className);
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(className) == 0 && current().refNames.count(className) == 0) {
            if (resolveUpvalue(className) == -1) {
                addLocal(className);
                slot = resolveLocal(className);
            }
        }
        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine);
        }
        else {
            int upvalue = resolveUpvalue(className);
            if (upvalue != -1) { emit(OpCode::OP_SET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
            else { emit(OpCode::OP_SET_GLOBAL, lastLine); emit16(nameIdx, lastLine); }
        }

        // ★ 智能加载宏：无论这个类在何处，精准找到它
        auto emitLoadClass = [&]() {
            if (slot != -1) { emit(OpCode::OP_GET_LOCAL, lastLine); emit16(static_cast<uint16_t>(slot), lastLine); }
            else {
                int upvalue = resolveUpvalue(className);
                if (upvalue != -1) { emit(OpCode::OP_GET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(upvalue), lastLine); }
                else { emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(nameIdx, lastLine); }
            }
            };

        // 3. 继承逻辑（修复：彻底兼容局部/闭包父类）
        if (!expr->superClassName.empty()) {
            uint16_t superIdx = identifierConstant(expr->superClassName);
            emitLoadClass(); // 取出当前子类

            int sSlot = resolveLocal(expr->superClassName);
            if (sSlot != -1) { emit(OpCode::OP_GET_LOCAL, lastLine); emit16(static_cast<uint16_t>(sSlot), lastLine); }
            else {
                int sUpvalue = resolveUpvalue(expr->superClassName);
                if (sUpvalue != -1) { emit(OpCode::OP_GET_UPVALUE, lastLine); emit16(static_cast<uint16_t>(sUpvalue), lastLine); }
                else { emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(superIdx, lastLine); }
            }
            emit(OpCode::OP_INHERIT, lastLine);
        }

        // 4. 方法注册逻辑
        for (auto& md : expr->methods) {
            auto fn = std::make_shared<CompiledFunction>();
            fn->name = md.name.lexeme;
            fn->maxArity = static_cast<int>(md.params.size());
            fn->hasRestParam = md.hasRestParam;

            int requiredParams = 0;
            for (size_t i = 0; i < md.params.size(); ++i) {
                if (i >= md.defaultExprs.size() || !md.defaultExprs[i]) {
                    if (md.hasRestParam && i == md.params.size() - 1) break;
                    requiredParams++;
                }
                else break;
            }
            fn->arity = requiredParams;

            compiledFunctions.push_back(fn);

            int thisFnIndex = functionIndexOffset + static_cast<int>(compiledFunctions.size()) - 1;

            initCompiler(fn.get());
            beginScope();
            for (size_t i = 0; i < md.params.size(); ++i) addLocal(md.params[i].lexeme);
            fn->paramIsRef = md.paramIsRef;

            emitDefaultPreamble(md.defaultExprs, fn->maxArity);

            // ★ 幽灵注入：参数类型检查 (换用专属变量名防遮蔽)
            for (size_t i = 0; i < md.params.size(); ++i) {
                if (i < md.paramTypes.size() && !md.paramTypes[i].empty()) {
                    int paramSlot = resolveLocal(md.params[i].lexeme);
                    emit(OpCode::OP_GET_LOCAL, lastLine);
                    emit16(static_cast<uint16_t>(paramSlot), lastLine);

                    uint16_t paramTypeIdx = identifierConstant(md.paramTypes[i]);
                    uint16_t paramNameIdx = identifierConstant(md.params[i].lexeme);
                    emit(OpCode::OP_ASSERT_PARAM_TYPE, lastLine);
                    emit16(paramTypeIdx, lastLine);
                    emit16(paramNameIdx, lastLine);
                }
            }
            current().expectedReturnType = md.returnType;
            compileNode(md.body.get());
            if (!md.returnType.empty()) {
                uint16_t retTypeIdx = identifierConstant(md.returnType);
                emit(OpCode::OP_ASSERT_RETURN_TYPE, lastLine);
                emit16(retTypeIdx, lastLine);
            }
            emit(OpCode::OP_RETURN, lastLine); // (不用动)

            fn->localCount = current().maxLocals;
            endScope();
            stateStack.pop_back();

            // ★ 修复：放弃无脑 GET_GLOBAL，智能获取正处于挂载态的方法主类
            emitLoadClass();

            uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
            emit(OpCode::OP_CLOSURE, lastLine); emit16(fnIdx, lastLine);

            uint16_t methodNameIdx = identifierConstant(md.name.lexeme);
            emit(OpCode::OP_METHOD, lastLine); emit16(methodNameIdx, lastLine);

            emit(OpCode::OP_POP, lastLine);
        }
        return {};
    }

    std::any Compiler::visitDotAccess(DotAccess* expr) {
        lastLine = expr->field.line;
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(selfIdx, lastLine);
            uint16_t nameIdx = identifierConstant(expr->field.lexeme);
            emit(OpCode::OP_GET_SUPER, lastLine); emit16(nameIdx, lastLine);
            return {};
        }

        compileNode(expr->object.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_GET_PROPERTY, lastLine);
        emit16(nameIdx, lastLine);
        return {};
    }

    std::any Compiler::visitDotAssign(DotAssign* expr) {
        lastLine = expr->field.line;
        compileNode(expr->object.get());
        compileNode(expr->value.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_SET_PROPERTY, lastLine);
        emit16(nameIdx, lastLine);
        emitStoreTarget(expr->object.get());
        return {};
    }

    std::any Compiler::visitMethodCallExpr(MethodCallExpr* expr) {
        lastLine = expr->method.line;
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, lastLine); emit16(selfIdx, lastLine);
            for (auto& arg : expr->arguments) compileNode(arg.get());
            uint16_t nameIdx = identifierConstant(expr->method.lexeme);
            emit(OpCode::OP_SUPER_INVOKE, lastLine);
            emit16(nameIdx, lastLine);
            emit(static_cast<uint8_t>(expr->arguments.size()), lastLine);
            return {};
        }

        compileNode(expr->object.get());
        for (auto& arg : expr->arguments) compileNode(arg.get());
        uint16_t nameIdx = identifierConstant(expr->method.lexeme);
        emit(OpCode::OP_INVOKE, lastLine);
        emit16(nameIdx, lastLine);
        emit(static_cast<uint8_t>(expr->arguments.size()), lastLine);
        return {};
    }

    std::any Compiler::visitSuperExpr(SuperExpr*) {
        throw std::runtime_error("Compiler Error: 'super' must be followed by '.method()'.");
    }

    std::any Compiler::visitSliceExpr(SliceExpr*) {
        throw std::runtime_error("Compiler Error: Slice expression should be handled by visitIndexAccess.");
    }

    std::any Compiler::visitDictDestructAssign(DictDestructAssign* expr) {
        lastLine = expr->targets.front().second.line;

        // 1. 将右侧要解构的数据源（Dict 或 Instance）解析并压入栈顶
        compileNode(expr->value.get());

        // 2. 依次扒取属性并分配
        for (auto& pair : expr->targets) {
            const std::string& fieldName = pair.first;
            const Token& varTok = pair.second;
            const std::string& varName = varTok.lexeme;

            // 将数据源 DUP 复制一份到栈顶，用来提取属性（因为 GET_PROPERTY 会吃掉原对象）
            emit(OpCode::OP_DUP, lastLine);

            // 执行 obj.fieldName 提取操作
            uint16_t nameIdx = identifierConstant(fieldName);
            emit(OpCode::OP_GET_PROPERTY, lastLine);
            emit16(nameIdx, lastLine);

            if (stateStack.size() == 1) knownGlobals.insert(varName);

            // 此时提取到的数值在栈顶，准备塞入目标变量 varName 中
            int slot = resolveLocal(varName);

            // Auto-local 拦截：如果我们是在局部作用域遇到新变量，自动设为 Local
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(varName) == 0 && current().refNames.count(varName) == 0) {
                if (resolveUpvalue(varName) == -1) {
                    addLocal(varName);
                    slot = resolveLocal(varName);
                }
            }

            // 存入变量体系
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, lastLine);
                emit16(static_cast<uint16_t>(slot), lastLine);
            }
            else {
                int upvalue = resolveUpvalue(varName);
                if (upvalue != -1) {
                    emit(OpCode::OP_SET_UPVALUE, lastLine);
                    emit16(static_cast<uint16_t>(upvalue), lastLine);
                }
                else {
                    uint16_t idx = identifierConstant(varName);
                    emit(OpCode::OP_SET_GLOBAL, lastLine);
                    emit16(idx, lastLine);
                }
            }

            // 将刚刚存进去的值弹栈丢弃，进入下一个 key 的解构
            emit(OpCode::OP_POP, lastLine);
        }

        return {};
    }

    std::any Compiler::visitSequenceExpr(SequenceExpr* expr) {
        for (size_t i = 0; i < expr->expressions.size(); ++i) {
            compileNode(expr->expressions[i].get());

            // 除了最后一个表达式，其余的执行完后都要清理栈（丢弃结果）
            if (i < expr->expressions.size() - 1) {
                emit(OpCode::OP_POP, lastLine);
            }
        }
        // 最后一个表达式的结果自然留在栈顶供上层读取
        return {};
    }

}

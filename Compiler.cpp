#include "Compiler.h"
#include <functional>

namespace jc {

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
        state.maxLocals = 0;  // ★ 初始化峰值
        stateStack.push_back(state);
    }

    void Compiler::beginScope() { current().scopeDepth++; }

    void Compiler::endScope() {
        current().scopeDepth--;
        while (!current().locals.empty() && current().locals.back().depth > current().scopeDepth) {
            // ★ 删除 emit(OpCode::OP_POP, 0); 我们实施了分离式局部变量锚定，变量生生不息地待在栈底
            current().locals.pop_back();
        }
    }

    void Compiler::emitDefaultPreamble(
        const std::vector<std::shared_ptr<Expr>>& defaultExprs,
        int paramCount)
    {
        for (int i = 0; i < paramCount; ++i) {
            if (i < static_cast<int>(defaultExprs.size()) && defaultExprs[i]) {
                emit(OpCode::OP_GET_LOCAL, 0);
                emit16(static_cast<uint16_t>(i), 0);
                emit(OpCode::OP_NONE, 0);
                emit(OpCode::OP_EQUAL, 0);

                int skipJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
                emit(OpCode::OP_POP, 0);

                compileNode(defaultExprs[i].get());
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(i), 0);
                emit(OpCode::OP_POP, 0);

                int endJump = chunk()->emitJump(OpCode::OP_JUMP, 0);
                chunk()->patchJump(skipJump);
                emit(OpCode::OP_POP, 0);
                chunk()->patchJump(endJump);
            }
        }
    }

    void Compiler::emitStoreTarget(Expr* target) {
        uint16_t tmpIdx = identifierConstant("__compound_tmp__");

        if (auto* var = dynamic_cast<Variable*>(target)) {
            const std::string& name = var->name.lexeme;
            int slot = resolveLocal(name);

            // ★ Auto-local
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
                addLocal(name);
                slot = resolveLocal(name);
            }

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
                    uint16_t nameIdx = identifierConstant(name);
                    emit(OpCode::OP_SET_GLOBAL, 0);
                    emit16(nameIdx, 0);
                }
            }
            return;
        }

        if (auto* dot = dynamic_cast<DotAccess*>(target)) {
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(tmpIdx, 0);
            emit(OpCode::OP_POP, 0);

            compileNode(dot->object.get());

            emit(OpCode::OP_GET_GLOBAL, 0);
            emit16(tmpIdx, 0);

            uint16_t fieldIdx = identifierConstant(dot->field.lexeme);
            emit(OpCode::OP_SET_PROPERTY, 0);
            emit16(fieldIdx, 0);

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
        emit(static_cast<uint8_t>(clause.isDestruct() ? 1 : 0), 0);

        int loopStart = static_cast<int>(chunk()->code.size());
        int exitJump = chunk()->emitJump(OpCode::OP_ITER_NEXT, 0);

        if (clause.isDestruct()) {
            int n = static_cast<int>(clause.destructNames.size());
            emit(OpCode::OP_DESTRUCT, 0);
            emit(static_cast<uint8_t>(n), 0);
            for (int j = n - 1; j >= 0; --j) {
                const std::string& name = clause.destructNames[j].lexeme;
                if (name == "_") { emit(OpCode::OP_POP, 0); continue; }

                int slot = resolveLocal(name);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
                    addLocal(name);
                    slot = resolveLocal(name);
                }
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
        }
        else {
            const std::string& varName = clause.varName.lexeme;
            int slot = resolveLocal(varName);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(varName) == 0) {
                addLocal(varName);
                slot = resolveLocal(varName);
            }
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                uint16_t idx = identifierConstant(varName);
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(idx, 0);
            }
            emit(OpCode::OP_POP, 0);
        }

        compileCompClause(expr, clauseIdx + 1);

        chunk()->emitLoop(loopStart, 0);
        chunk()->patchJump(exitJump);

        emit(OpCode::OP_POP, 0);
        emit(OpCode::OP_POP, 0);
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

        // ★ Auto-local Write — 但不能覆盖已存在的 upvalue
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
            if (resolveUpvalue(name) == -1) {   // ★ 仅当不是 upvalue 时才 auto-local
                addLocal(name);
                slot = resolveLocal(name);
            }
        }

        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, expr->name.position);
            emit16(static_cast<uint16_t>(slot), expr->name.position);
        }
        else {
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

        if (expr->op.type == TokenType::PIPE) {
            compileNode(expr->right.get());
            compileNode(expr->left.get());
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
            int upvalue = resolveUpvalue(name);
            if (upvalue != -1) {
                emit(OpCode::OP_GET_UPVALUE, expr->callee.position);
                emit16(static_cast<uint16_t>(upvalue), expr->callee.position);
            }
            else {
                // ★ 关键重构：将全局级别调用的目标变成字符串文字，把解析交接给 VM 的 OP_CALL 晚绑定操作
                uint16_t idx = identifierConstant(name);
                emit(OpCode::OP_CONSTANT, expr->callee.position);
                emit16(idx, expr->callee.position);
            }
        }
        for (auto& argExpr : expr->arguments) {
            compileNode(argExpr.get());
        }
        emit(OpCode::OP_CALL, expr->callee.position);
        emit(static_cast<uint8_t>(expr->arguments.size()), expr->callee.position);

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

        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        fn->paramIsRef = expr->paramIsRef; // ★ 保留：记录引用的参数标记

        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);

        compileNode(expr->body.get());
        emit(OpCode::OP_RETURN, 0);
        fn->localCount = current().maxLocals;
        endScope();
        stateStack.pop_back();

        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, expr->name.position);
        emit16(fnIdx, expr->name.position);

        // ★ Auto-local function names (matches python rules)
        int slot = resolveLocal(funcName);
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(funcName) == 0) {
            addLocal(funcName);
            slot = resolveLocal(funcName);
        }

        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, expr->name.position);
            emit16(static_cast<uint16_t>(slot), expr->name.position);
        }
        else {
            uint16_t nameIdx = identifierConstant(funcName);
            emit(OpCode::OP_SET_GLOBAL, expr->name.position);
            emit16(nameIdx, expr->name.position);
        }

        return {};
    }

    std::any Compiler::visitLambdaExpr(LambdaExpr* expr) {
        auto fn = std::make_shared<CompiledFunction>();
        fn->name = "<lambda>";
        fn->maxArity = static_cast<int>(expr->params.size());

        int requiredParams = 0;
        for (size_t i = 0; i < expr->params.size(); ++i) {
            if (i >= expr->defaultExprs.size() || !expr->defaultExprs[i]) requiredParams++;
            else break;
        }
        fn->arity = requiredParams;
        fn->paramIsRef.assign(fn->maxArity, false);

        compiledFunctions.push_back(fn);

        int thisFnIndex = functionIndexOffset +
            static_cast<int>(compiledFunctions.size()) - 1;

        initCompiler(fn.get());
        beginScope();

        for (size_t i = 0; i < expr->params.size(); ++i) {
            addLocal(expr->params[i].lexeme);
        }
        emitDefaultPreamble(expr->defaultExprs, fn->maxArity);

        compileNode(expr->body.get());
        emit(OpCode::OP_RETURN, 0);

        // ★ 修改：提取峰值
        fn->localCount = current().maxLocals;

        endScope();
        stateStack.pop_back();

        uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
        emit(OpCode::OP_CLOSURE, 0);
        emit16(fnIdx, 0);

        return {};
    }

    int Compiler::addUpvalue(int level, const std::string& name,
        bool isLocal, int index) {
        auto* fn = stateStack[level].function;
        for (int j = 0; j < static_cast<int>(fn->upvalues.size()); ++j) {
            if (fn->upvalues[j].name == name)
                return j;
        }
        fn->upvalues.push_back({ name, isLocal, index });
        return static_cast<int>(fn->upvalues.size()) - 1;
    }

    int Compiler::resolveUpvalueAt(int level, const std::string& name) {
        if (level <= 0) return -1;
        int enclosingLevel = level - 1;
        auto& enclosing = stateStack[enclosingLevel];

        for (int i = static_cast<int>(enclosing.locals.size()) - 1; i >= 0; --i) {
            if (enclosing.locals[i].name == name) return addUpvalue(level, name, true, i);
        }

        int upvalueInEnclosing = resolveUpvalueAt(enclosingLevel, name);
        if (upvalueInEnclosing != -1) return addUpvalue(level, name, false, upvalueInEnclosing);

        return -1;
    }

    int Compiler::resolveUpvalue(const std::string& name) {
        int currentLevel = static_cast<int>(stateStack.size()) - 1;
        return resolveUpvalueAt(currentLevel, name);
    }

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

    std::any Compiler::visitBreakExpr(BreakExpr*) {
        if (loopStack.empty())
            throw std::runtime_error("Compiler Error: 'break' outside loop.");
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

            // ★ Auto-local Write
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
                if (resolveUpvalue(name) == -1) {   // ★ 仅当不是 upvalue 时才 auto-local
                    addLocal(name);
                    slot = resolveLocal(name);
                }
            }

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

                int slot = resolveLocal(name);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
                    addLocal(name);
                    slot = resolveLocal(name);
                }
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
        }
        else {
            const std::string& varName = expr->varName.lexeme;
            int slot = resolveLocal(varName);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(varName) == 0) {
                addLocal(varName);
                slot = resolveLocal(varName);
            }
            if (slot != -1) {
                emit(OpCode::OP_SET_LOCAL, 0);
                emit16(static_cast<uint16_t>(slot), 0);
            }
            else {
                uint16_t idx = identifierConstant(varName);
                emit(OpCode::OP_SET_GLOBAL, 0);
                emit16(idx, 0);
            }
            emit(OpCode::OP_POP, 0);
        }

        compileNode(expr->body.get());
        emit(OpCode::OP_POP, 0);

        chunk()->emitLoop(loopStart, 0);
        chunk()->patchJump(exitJump);

        emitBreakJumps();
        endLoop();

        emit(OpCode::OP_POP, 0);
        emit(OpCode::OP_POP, 0);
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitMatrixNode(MatrixNode* expr) {
        int rows = static_cast<int>(expr->elements.size());
        if (rows == 0) {
            chunk()->emitConstant(Value(RealMatrix(0, 0)), 0);
            return {};
        }
        int cols = static_cast<int>(expr->elements[0].size());
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
                    if (slice->start) compileNode(slice->start.get()); else emit(OpCode::OP_NONE, 0);
                    if (slice->end) compileNode(slice->end.get()); else emit(OpCode::OP_NONE, 0);
                    if (slice->step) compileNode(slice->step.get()); else emit(OpCode::OP_NONE, 0);
                }
                else {
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, 0);
                    emit(OpCode::OP_NONE, 0);
                }
            }
            emit(OpCode::OP_SLICE_GET, 0);
            emit(static_cast<uint8_t>(expr->indices.size()), 0);
            return {};
        }
        compileNode(expr->object.get());
        for (auto& idx : expr->indices) compileNode(idx.get());
        emit(OpCode::OP_INDEX_GET, 0);
        emit(static_cast<uint8_t>(expr->indices.size()), 0);
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
                if (slot != -1) { emit(OpCode::OP_GET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
                else { uint16_t idx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_GET_GLOBAL, 0); emit16(idx, 0); }
            }

            for (auto& idx : expr->indexChain[0]) {
                if (auto* slice = dynamic_cast<SliceExpr*>(idx.get())) {
                    if (slice->start) compileNode(slice->start.get()); else emit(OpCode::OP_NONE, 0);
                    if (slice->end) compileNode(slice->end.get()); else emit(OpCode::OP_NONE, 0);
                    if (slice->step) compileNode(slice->step.get()); else emit(OpCode::OP_NONE, 0);
                }
                else {
                    compileNode(idx.get());
                    emit(OpCode::OP_NONE, 0);
                    emit(OpCode::OP_NONE, 0);
                }
            }

            compileNode(expr->value.get());
            emit(OpCode::OP_SLICE_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), 0);

            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->name.lexeme) == 0) {
                    addLocal(expr->name.lexeme); slot = resolveLocal(expr->name.lexeme);
                }
                if (slot != -1) { emit(OpCode::OP_SET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
                else { uint16_t idx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_SET_GLOBAL, 0); emit16(idx, 0); }
            }
            else { emitStoreTarget(expr->objectExpr.get()); }
            return {};
        }

        int depth = static_cast<int>(expr->indexChain.size());
        auto emitLoadRoot = [&]() {
            if (expr->hasObjectExpr()) compileNode(expr->objectExpr.get());
            else {
                int slot = resolveLocal(expr->name.lexeme);
                if (slot != -1) { emit(OpCode::OP_GET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
                else { uint16_t nameIdx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_GET_GLOBAL, 0); emit16(nameIdx, 0); }
            }
            };

        auto emitStoreRoot = [&]() {
            if (!expr->hasObjectExpr()) {
                int slot = resolveLocal(expr->name.lexeme);
                if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->name.lexeme) == 0) {
                    addLocal(expr->name.lexeme); slot = resolveLocal(expr->name.lexeme);
                }
                if (slot != -1) { emit(OpCode::OP_SET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
                else { uint16_t nameIdx = identifierConstant(expr->name.lexeme); emit(OpCode::OP_SET_GLOBAL, 0); emit16(nameIdx, 0); }
            }
            else emitStoreTarget(expr->objectExpr.get());
            };

        if (depth == 1) {
            emitLoadRoot();
            for (auto& idx : expr->indexChain[0]) compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[0].size()), 0);
            emitStoreRoot();
        }
        else {
            uint16_t tmpIdx = identifierConstant("__idx_chain_tmp__");
            emitLoadRoot();
            for (int level = 0; level < depth - 1; ++level) {
                for (auto& idx : expr->indexChain[level]) compileNode(idx.get());
                emit(OpCode::OP_INDEX_GET, 0);
                emit(static_cast<uint8_t>(expr->indexChain[level].size()), 0);
            }
            for (auto& idx : expr->indexChain[depth - 1]) compileNode(idx.get());
            compileNode(expr->value.get());
            emit(OpCode::OP_INDEX_SET, 0);
            emit(static_cast<uint8_t>(expr->indexChain[depth - 1].size()), 0);

            for (int level = depth - 2; level >= 0; --level) {
                emit(OpCode::OP_SET_GLOBAL, 0); emit16(tmpIdx, 0); emit(OpCode::OP_POP, 0);
                emitLoadRoot();
                for (int l = 0; l < level; ++l) {
                    for (auto& idx : expr->indexChain[l]) compileNode(idx.get());
                    emit(OpCode::OP_INDEX_GET, 0); emit(static_cast<uint8_t>(expr->indexChain[l].size()), 0);
                }
                for (auto& idx : expr->indexChain[level]) compileNode(idx.get());
                emit(OpCode::OP_GET_GLOBAL, 0); emit16(tmpIdx, 0);
                emit(OpCode::OP_INDEX_SET, 0); emit(static_cast<uint8_t>(expr->indexChain[level].size()), 0);
            }
            emitStoreRoot();
        }
        return {};
    }

    std::any Compiler::visitInvokeExpr(InvokeExpr* expr) {
        compileNode(expr->callee.get());
        for (auto& argExpr : expr->arguments) compileNode(argExpr.get());
        emit(OpCode::OP_CALL, 0);
        emit(static_cast<uint8_t>(expr->arguments.size()), 0);

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
                emit(OpCode::OP_REF_WRITEBACK, 0);
                emit(static_cast<uint8_t>(sources.size()), 0);
                for (auto& s : sources) { emit(s.argIndex, 0); emit(s.sourceType, 0); emit16(s.sourceRef, 0); }
            }
        }
        return {};
    }

    std::any Compiler::visitDestructAssign(DestructAssign* expr) {
        compileNode(expr->value.get());
        int n = static_cast<int>(expr->names.size());
        emit(OpCode::OP_DESTRUCT, 0);
        emit(static_cast<uint8_t>(n), 0);

        for (int i = n - 1; i >= 0; --i) {
            const std::string& name = expr->names[i].lexeme;
            if (name == "_") { emit(OpCode::OP_POP, 0); continue; }

            int slot = resolveLocal(name);
            if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(name) == 0) {
                addLocal(name);
                slot = resolveLocal(name);
            }

            if (slot != -1) { emit(OpCode::OP_SET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
            else { uint16_t idx = identifierConstant(name); emit(OpCode::OP_SET_GLOBAL, 0); emit16(idx, 0); }
            emit(OpCode::OP_POP, 0);
        }
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitSwitchExpr(SwitchExpr* expr) {
        compileNode(expr->subject.get());
        std::vector<int> endJumps;

        for (auto& [values, body] : expr->cases) {
            std::vector<int> bodyJumps;
            int noMatchJump = -1;

            for (size_t vi = 0; vi < values.size(); ++vi) {
                emit(OpCode::OP_DUP, 0);
                compileNode(values[vi].get());
                emit(OpCode::OP_EQUAL, 0);
                int matchJump = chunk()->emitJump(OpCode::OP_JUMP_IF_FALSE, 0);
                emit(OpCode::OP_POP, 0);
                int toBody = chunk()->emitJump(OpCode::OP_JUMP, 0);
                bodyJumps.push_back(toBody);
                chunk()->patchJump(matchJump);
                emit(OpCode::OP_POP, 0);
            }

            noMatchJump = chunk()->emitJump(OpCode::OP_JUMP, 0);
            for (int bj : bodyJumps) chunk()->patchJump(bj);
            emit(OpCode::OP_POP, 0);
            compileNode(body.get());
            endJumps.push_back(chunk()->emitJump(OpCode::OP_JUMP, 0));
            chunk()->patchJump(noMatchJump);
        }
        emit(OpCode::OP_POP, 0);
        if (expr->defaultBody) compileNode(expr->defaultBody.get());
        else emit(OpCode::OP_NONE, 0);

        for (int ej : endJumps) chunk()->patchJump(ej);
        return {};
    }

    std::any Compiler::visitThrowExpr(ThrowExpr* expr) {
        compileNode(expr->value.get());
        emit(OpCode::OP_THROW, 0);
        return {};
    }

    std::any Compiler::visitTryCatchExpr(TryCatchExpr* expr) {
        uint16_t catchNameIdx = identifierConstant(expr->catchName.lexeme);
        emit(OpCode::OP_TRY_BEGIN, 0);
        int offsetSlot = static_cast<int>(chunk()->code.size());
        emit16(0, 0);
        emit16(catchNameIdx, 0);
        compileNode(expr->tryBody.get());
        emit(OpCode::OP_TRY_END, 0);
        int skipCatch = chunk()->emitJump(OpCode::OP_JUMP, 0);

        int catchAddr = static_cast<int>(chunk()->code.size());
        int relOffset = catchAddr - (offsetSlot + 4);
        chunk()->code[offsetSlot] = static_cast<uint8_t>((relOffset >> 8) & 0xFF);
        chunk()->code[offsetSlot + 1] = static_cast<uint8_t>(relOffset & 0xFF);

        int slot = resolveLocal(expr->catchName.lexeme);
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(expr->catchName.lexeme) == 0) {
            addLocal(expr->catchName.lexeme);
            slot = resolveLocal(expr->catchName.lexeme);
        }
        if (slot != -1) {
            emit(OpCode::OP_SET_LOCAL, 0);
            emit16(static_cast<uint16_t>(slot), 0);
        }
        else {
            // ★ 修复：在这里补充未定义的 nameIdx
            uint16_t nameIdx = identifierConstant(expr->catchName.lexeme);
            emit(OpCode::OP_SET_GLOBAL, 0);
            emit16(nameIdx, 0);
        }
        emit(OpCode::OP_POP, 0);

        compileNode(expr->catchBody.get());
        chunk()->patchJump(skipCatch);
        return {};
    }

    std::any Compiler::visitDictLiteral(DictLiteral* expr) {
        for (auto& [keyExpr, valExpr] : expr->entries) {
            compileNode(keyExpr.get());
            compileNode(valExpr.get());
        }
        emit(OpCode::OP_BUILD_DICT, 0);
        emit16(static_cast<uint16_t>(expr->entries.size()), 0);
        return {};
    }

    std::any Compiler::visitConstDecl(ConstDecl* expr) {
        compileNode(expr->value.get());
        const std::string& name = expr->name.lexeme;

        // Const is inherently global/outermost scope definition behavior in VM. 
        // We emit DEFINE_GLOBAL directly.
        uint16_t idx = identifierConstant(name);
        emit(OpCode::OP_DEFINE_GLOBAL, 0);
        emit16(idx, 0);
        return {};
    }

    std::any Compiler::visitDeleteExpr(DeleteExpr* expr) {
        for (auto& tok : expr->names) {
            chunk()->emitConstant(Value(tok.lexeme), 0);
            uint16_t fnIdx = identifierConstant("__vm_delete__");
            emit(OpCode::OP_CALL_BUILTIN, 0);
            emit16(fnIdx, 0);
            emit(static_cast<uint8_t>(1), 0);
            emit(OpCode::OP_POP, 0);
        }
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitGlobalDecl(GlobalDecl* expr) {
        for (const auto& tok : expr->names) {
            current().globalNames.insert(tok.lexeme);
        }
        emit(OpCode::OP_NONE, 0);
        return {};
    }

    std::any Compiler::visitFStringExpr(FStringExpr* expr) {
        int partCount = 0;
        for (size_t i = 0; i < expr->exprs.size(); ++i) {
            if (!expr->literals[i].empty()) {
                chunk()->emitConstant(Value(expr->literals[i]), 0);
                partCount++;
            }
            compileNode(expr->exprs[i].get());
            if (!expr->formatSpecs[i].empty()) {
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

    std::any Compiler::visitListCompExpr(ListCompExpr* expr) {
        emit(OpCode::OP_LIST_INIT, 0);
        compileCompClause(expr, 0);
        return {};
    }

    std::any Compiler::visitImportExpr(ImportExpr* expr) {
        compileNode(expr->path.get());
        emit(OpCode::OP_IMPORT, 0);
        return {};
    }

    std::any Compiler::visitClassDefExpr(ClassDefExpr* expr) {
        const std::string& className = expr->name.lexeme;
        uint16_t nameIdx = identifierConstant(className);

        emit(OpCode::OP_CLASS, 0);
        emit16(nameIdx, 0);

        int slot = resolveLocal(className);
        if (stateStack.size() > 1 && slot == -1 && current().globalNames.count(className) == 0) {
            addLocal(className); slot = resolveLocal(className);
        }
        if (slot != -1) { emit(OpCode::OP_SET_LOCAL, 0); emit16(static_cast<uint16_t>(slot), 0); }
        else { emit(OpCode::OP_SET_GLOBAL, 0); emit16(nameIdx, 0); }

        if (!expr->superClassName.empty()) {
            uint16_t superIdx = identifierConstant(expr->superClassName);
            emit(OpCode::OP_GET_GLOBAL, 0); emit16(nameIdx, 0);
            emit(OpCode::OP_GET_GLOBAL, 0); emit16(superIdx, 0);
            emit(OpCode::OP_INHERIT, 0);
        }

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

            int thisFnIndex = functionIndexOffset + static_cast<int>(compiledFunctions.size()) - 1;

            initCompiler(fn.get());
            beginScope();
            for (size_t i = 0; i < md.params.size(); ++i) addLocal(md.params[i].lexeme);
            fn->paramIsRef = md.paramIsRef;

            emitDefaultPreamble(md.defaultExprs, fn->maxArity);
            compileNode(md.body.get());
            emit(OpCode::OP_RETURN, 0);
            fn->localCount = current().maxLocals;
            endScope();
            stateStack.pop_back();

            emit(OpCode::OP_GET_GLOBAL, 0); emit16(nameIdx, 0);

            uint16_t fnIdx = makeConstant(Value(static_cast<double>(thisFnIndex)));
            emit(OpCode::OP_CLOSURE, 0); emit16(fnIdx, 0);

            uint16_t methodNameIdx = identifierConstant(md.name.lexeme);
            emit(OpCode::OP_METHOD, 0); emit16(methodNameIdx, 0);

            emit(OpCode::OP_POP, 0);
        }
        return {};
    }

    std::any Compiler::visitDotAccess(DotAccess* expr) {
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, 0); emit16(selfIdx, 0);
            uint16_t nameIdx = identifierConstant(expr->field.lexeme);
            emit(OpCode::OP_GET_SUPER, 0); emit16(nameIdx, 0);
            return {};
        }

        compileNode(expr->object.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_GET_PROPERTY, 0);
        emit16(nameIdx, 0);
        return {};
    }

    std::any Compiler::visitDotAssign(DotAssign* expr) {
        compileNode(expr->object.get());
        compileNode(expr->value.get());
        uint16_t nameIdx = identifierConstant(expr->field.lexeme);
        emit(OpCode::OP_SET_PROPERTY, 0);
        emit16(nameIdx, 0);
        emitStoreTarget(expr->object.get());
        return {};
    }

    std::any Compiler::visitMethodCallExpr(MethodCallExpr* expr) {
        if (dynamic_cast<SuperExpr*>(expr->object.get())) {
            uint16_t selfIdx = identifierConstant("self");
            emit(OpCode::OP_GET_GLOBAL, 0); emit16(selfIdx, 0);
            for (auto& arg : expr->arguments) compileNode(arg.get());
            uint16_t nameIdx = identifierConstant(expr->method.lexeme);
            emit(OpCode::OP_SUPER_INVOKE, 0);
            emit16(nameIdx, 0);
            emit(static_cast<uint8_t>(expr->arguments.size()), 0);
            return {};
        }

        compileNode(expr->object.get());
        for (auto& arg : expr->arguments) compileNode(arg.get());
        uint16_t nameIdx = identifierConstant(expr->method.lexeme);
        emit(OpCode::OP_INVOKE, 0);
        emit16(nameIdx, 0);
        emit(static_cast<uint8_t>(expr->arguments.size()), 0);
        return {};
    }

    std::any Compiler::visitSuperExpr(SuperExpr*) {
        throw std::runtime_error("Compiler Error: 'super' must be followed by '.method()'.");
    }

    std::any Compiler::visitSliceExpr(SliceExpr*) {
        throw std::runtime_error("Compiler Error: Slice expression should be handled by visitIndexAccess.");
    }

}

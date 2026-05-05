#ifndef JC2_COMPILER_H
#define JC2_COMPILER_H

#include "Bytecode.h"
#include "Expr.h"
#include <map>
#include <string>
#include <vector>
#include <set>

namespace jc {

    struct Local {
        std::string name;
        int depth;
    };

    class Compiler : public ExprVisitor {
    private:
        struct CompilerState {
            CompiledFunction* function = nullptr;
            int scopeDepth = 0;
            std::vector<Local> locals;
            int maxLocals = 0;                  // ★ 新增：跟踪该函数所使用的最大局部变量数
            std::set<std::string> globalNames;
            int tryDepth = 0;
            std::string expectedReturnType = "";
        };
        struct LoopInfo {
            int loopStart;
            std::vector<int> breakJumps;
            std::vector<int> continueJumps;   // ★ 新增
            int scopeDepth;
            int tryDepth;
        };
        std::vector<LoopInfo> loopStack;

        void beginLoop(int loopStart);
        void endLoop();
        void emitBreakJumps();

        std::vector<CompilerState> stateStack;
        std::vector<std::shared_ptr<CompiledFunction>> compiledFunctions;
        int functionIndexOffset = 0;
        int lastLine = 0;
        std::string currentSourceFile;

        CompilerState& current() { return stateStack.back(); }
        Chunk* chunk() { return &current().function->chunk; }

        void emit(OpCode op, int line = 0);
        void emit(uint8_t byte, int line = 0);
        void emit16(uint16_t val, int line = 0);
        uint16_t makeConstant(const Value& val);
        uint16_t identifierConstant(const std::string& name);

        void beginScope();
        void endScope();
        int resolveLocal(const std::string& name);
        void addLocal(const std::string& name);
        void declareVariable(const std::string& name);

        void compileNode(Expr* expr);
        void initCompiler(CompiledFunction* fn);
        void compileCompClause(ListCompExpr* expr, size_t clauseIdx);
        void emitDefaultPreamble(const std::vector<std::shared_ptr<Expr>>& defaultExprs, int paramCount);
        int resolveUpvalue(const std::string& name);
        int resolveUpvalueAt(int level, const std::string& name);
        int addUpvalue(int level, const std::string& name, bool isLocal, int index);
        void emitStoreTarget(Expr* target);

    public:
        Chunk compile(Expr* ast, const std::string& sourceFile = "");

        const std::vector<std::shared_ptr<CompiledFunction>>& getCompiledFunctions() const { return compiledFunctions; }
        void setFunctionIndexOffset(int offset) { functionIndexOffset = offset; }

        std::any visitLiteral(Literal* expr) override;
        std::any visitVariable(Variable* expr) override;
        std::any visitAssign(Assign* expr) override;
        std::any visitUnary(Unary* expr) override;
        std::any visitBinary(Binary* expr) override;
        std::any visitCall(Call* expr) override;
        std::any visitBlock(Block* expr) override;
        std::any visitIfExpr(IfExpr* expr) override;
        std::any visitWhileExpr(WhileExpr* expr) override;
        std::any visitForExpr(ForExpr* expr) override;

        std::any visitMatrixNode(MatrixNode*) override;
        std::any visitFunctionDef(FunctionDef*) override;
        std::any visitBreakExpr(BreakExpr*) override;
        std::any visitContinueExpr(ContinueExpr*) override;
        std::any visitReturnExpr(ReturnExpr*) override;
        std::any visitIndexAccess(IndexAccess*) override;
        std::any visitIndexAssign(IndexAssign*) override;
        std::any visitConstDecl(ConstDecl*) override;
        std::any visitDeleteExpr(DeleteExpr*) override;
        std::any visitCompoundAssign(CompoundAssign*) override;
        std::any visitLambdaExpr(LambdaExpr*) override;
        std::any visitInvokeExpr(InvokeExpr*) override;
        std::any visitGlobalDecl(GlobalDecl*) override;
        std::any visitForInExpr(ForInExpr*) override;
        std::any visitThrowExpr(ThrowExpr*) override;
        std::any visitTryCatchExpr(TryCatchExpr*) override;
        std::any visitImportExpr(ImportExpr*) override;
        std::any visitSwitchExpr(SwitchExpr*) override;
        std::any visitClassDefExpr(ClassDefExpr*) override;
        std::any visitDotAccess(DotAccess*) override;
        std::any visitDotAssign(DotAssign*) override;
        std::any visitMethodCallExpr(MethodCallExpr*) override;
        std::any visitSuperExpr(SuperExpr*) override;
        std::any visitDestructAssign(DestructAssign*) override;
        std::any visitFStringExpr(FStringExpr*) override;
        std::any visitListCompExpr(ListCompExpr*) override;
        std::any visitDictLiteral(DictLiteral*) override;
        std::any visitSliceExpr(SliceExpr*) override;
        std::any visitDictDestructAssign(DictDestructAssign*) override;
        std::any visitSequenceExpr(SequenceExpr* expr) override;
    };

}
#endif

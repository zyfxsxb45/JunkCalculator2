#ifndef JC2_EVALUATOR_H
#define JC2_EVALUATOR_H

#include <any>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <functional>
#include "Expr.h"
#include "Value.h"

namespace jc {

    struct BreakSignal {};
    struct ContinueSignal {};
    struct ReturnSignal { Value value; };
    struct ErrorSignal { std::string message; };

    // ★ 更新：作用域帧——仅由函数调用创建
    struct ScopeFrame {
        std::map<std::string, std::pair<bool, Value>> savedValues;
        std::set<std::string> globalNames;   // ★ 被 `global` 声明的变量名
    };

    namespace {
        struct EnvGuard {
            std::map<std::string, Value>& env;
            std::vector<std::string> paramNames;
            std::vector<bool> existed;
            std::vector<Value> oldVals;
            EnvGuard(std::map<std::string, Value>& e, const std::vector<std::string>& pNames, const std::vector<Value>& newVals)
                : env(e), paramNames(pNames) {
                for (size_t i = 0; i < paramNames.size(); ++i) {
                    auto it = env.find(paramNames[i]);
                    if (it != env.end()) { existed.push_back(true); oldVals.push_back(it->second); }
                    else { existed.push_back(false); oldVals.push_back(Value::none()); }
                    env[paramNames[i]] = newVals[i];
                }
            }
            ~EnvGuard() {
                for (size_t i = 0; i < paramNames.size(); ++i) {
                    if (existed[i]) env[paramNames[i]] = oldVals[i];
                    else env.erase(paramNames[i]);
                }
            }
        };
    }

    using NativeCallable = std::function<Value(const std::vector<Value>&)>;

    class Evaluator : public ExprVisitor {
    private:
        std::map<std::string, Value> environment;
        std::map<std::string, NativeCallable> builtins;
        std::set<std::string> constVars;
        std::map<std::string, std::set<int>> builtinArity;
        std::set<std::string> importedFiles;                         // ★ 防重复导入

        Value evaluate(Expr* expr);
        int recursionDepth = 0;
        const int MAX_RECURSION_DEPTH = 1000;
        int functionDepth = 0;

        static bool isTruthy(const Value& v);

        Value callInstanceMethod(std::shared_ptr<Instance> inst,
            const std::string& methodName, const std::vector<Value>& args,
            std::shared_ptr<ClassDefinition> startClass = nullptr);  // ★ 新增参数
        bool hasDunder(const Value& obj, const std::string& name) const;
        static std::pair<std::shared_ptr<FunctionClosure>, std::shared_ptr<ClassDefinition>>
            resolveMethod(std::shared_ptr<ClassDefinition> cls, const std::string& name);  // ★

        // ★ 作用域系统（仅函数级）
        std::vector<ScopeFrame> scopeStack;
        void declareLocal(const std::string& name);
        void pushScope();
        void popScope();
        void declareGlobal(const std::string& name);       // ★ 新增
        bool isGlobal(const std::string& name) const;      // ★ 新增

        void assertNotConst(const std::string& name) const;
        Value callFunction(std::shared_ptr<FunctionClosure> closure, const std::vector<Value>& args);

        // ★ 脚本执行路径栈（栈顶 = 当前正在执行的脚本所在目录）
        std::string workspacePath;  // ★ 工作区默认路径
        std::string exeDir;  // ★ 可执行文件所在目录
        std::vector<std::string> scriptDirStack;
        std::string cwd() const;  // 获取当前工作目录
        Value readSingleIndex(Value& container, const std::vector<std::unique_ptr<Expr>>& indexExprs);
        void writeSingleIndex(Value& container, const std::vector<std::unique_ptr<Expr>>& indexExprs, const Value& val);
        std::pair<std::vector<int>, bool> buildIndices(Expr* expr, int dimSize);

    public:
        Evaluator();
        Value calculate(Expr* expr);

        std::any visitLiteral(Literal* expr) override;
        std::any visitVariable(Variable* expr) override;
        std::any visitAssign(Assign* expr) override;
        std::any visitUnary(Unary* expr) override;
        std::any visitBinary(Binary* expr) override;
        std::any visitCall(Call* expr) override;
        std::any visitMatrixNode(MatrixNode* expr) override;
        std::any visitFunctionDef(FunctionDef* expr) override;
        std::any visitBlock(Block* expr) override;
        std::any visitIfExpr(IfExpr* expr) override;
        std::any visitWhileExpr(WhileExpr* expr) override;
        std::any visitForExpr(ForExpr* expr) override;
        std::any visitBreakExpr(BreakExpr* expr) override;
        std::any visitContinueExpr(ContinueExpr* expr) override;
        std::any visitReturnExpr(ReturnExpr* expr) override;
        std::any visitConstDecl(ConstDecl* expr) override;
        std::any visitGlobalDecl(GlobalDecl* expr) override;       // ★ 新增
        std::any visitIndexAccess(IndexAccess* expr) override;
        std::any visitIndexAssign(IndexAssign* expr) override;
        std::any visitCompoundAssign(CompoundAssign* expr) override;
        std::any visitLambdaExpr(LambdaExpr* expr) override;
        std::any visitInvokeExpr(InvokeExpr* expr) override;
        std::any visitDeleteExpr(DeleteExpr* expr) override;
        std::any visitForInExpr(ForInExpr* expr) override;
        std::any visitThrowExpr(ThrowExpr* expr) override;         // ★
        std::any visitTryCatchExpr(TryCatchExpr* expr) override;   // ★
        std::any visitImportExpr(ImportExpr* expr) override;        // ★
        std::any visitSwitchExpr(SwitchExpr* expr) override;
        std::any visitClassDefExpr(ClassDefExpr* expr) override;       // ★
        std::any visitDotAccess(DotAccess* expr) override;             // ★
        std::any visitDotAssign(DotAssign* expr) override;             // ★
        std::any visitMethodCallExpr(MethodCallExpr* expr) override;   // ★
        std::any visitSuperExpr(SuperExpr* expr) override;           // ★
        std::any visitDestructAssign(DestructAssign* expr) override;  // ★
        std::any visitFStringExpr(FStringExpr* expr) override;  // ★
        std::any visitListCompExpr(ListCompExpr* expr) override;  // ★
        std::any visitDictLiteral(DictLiteral* expr) override;  // ★
        std::any visitSliceExpr(SliceExpr* expr) override;  // ★ 新增
        

        void showVariables() const;
        void clearVariables();
        void setVariable(const std::string& name, const Value& val);
        const std::map<std::string, Value>& getEnvironment() const { return environment; }
        // ★ 允许外部 (main.cpp) 压入/弹出主脚本路径
        void pushScriptDir(const std::string& dir);
        void popScriptDir();
        std::string resolvePath(const std::string& path) const; // 相对路径转绝对路径
        std::string getWorkspacePath() const;        // ★
        void setWorkspacePath(const std::string& p); // ★
        void setExeDir(const std::string& dir) { exeDir = dir; }  // ★
        std::string getExeDir() const;                              // ★
        // ★ 原生模块系统
        bool loadNativeModule(const std::string& name);
        bool hasNativeModule(const std::string& name) const;
        std::vector<std::string> listNativeModules() const;
    };

} // namespace jc
#endif

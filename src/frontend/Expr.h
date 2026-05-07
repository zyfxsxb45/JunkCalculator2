#ifndef JC2_EXPR_H
#define JC2_EXPR_H

#include "Token.h"
#include <any>
#include <memory>
#include <stdexcept>   // ★ std::runtime_error
#include <string>
#include <vector>

namespace jc {

    // 前向声明所有节点
    struct Binary;
    struct Unary;
    struct Literal;
    struct Variable;
    struct Assign;
    struct Call;
    struct MatrixNode;
    struct FunctionDef;
    struct Block;          // ★ 新增
    struct IfExpr;         // ★ 新增
    struct WhileExpr;      // ★ 新增
    struct ForExpr;        // ★ 新增
    struct BreakExpr;      // ★ 新增
    struct ContinueExpr;   // ★ 新增
    struct ReturnExpr;
    struct RefDecl;
    struct StateDecl;        // ★ 新增
    struct IndexAccess;      // ★ 新增
    struct IndexAssign;      // ★ 新增
    struct ConstDecl;
    struct DeleteExpr;
    struct CompoundAssign;
    struct LambdaExpr;
    struct InvokeExpr;
    struct ForInExpr;
    struct ThrowExpr;        // ★
    struct TryCatchExpr;     // ★
    struct ImportExpr;
    struct SwitchExpr;       // ★
    struct ClassDefExpr;       // ★
    struct DotAccess;          // ★
    struct DotAssign;          // ★
    struct MethodCallExpr;     // ★
    struct SuperExpr;
    struct DestructAssign;     // ★
    struct FStringExpr;
    struct ListCompExpr;       // ★
    struct DictLiteral;        // ★
    struct SliceExpr;        // ★ 新增
    struct DictDestructAssign;
    struct SequenceExpr;

    class ExprVisitor {
    public:
        virtual ~ExprVisitor() = default;
        virtual std::any visitBinary(Binary* expr) = 0;
        virtual std::any visitUnary(Unary* expr) = 0;
        virtual std::any visitLiteral(Literal* expr) = 0;
        virtual std::any visitVariable(Variable* expr) = 0;
        virtual std::any visitAssign(Assign* expr) = 0;
        virtual std::any visitCall(Call* expr) = 0;
        virtual std::any visitMatrixNode(MatrixNode* expr) = 0;
        // ★ 新增 6 个访问者接口
        virtual std::any visitBlock(Block* expr) = 0;
        virtual std::any visitIfExpr(IfExpr* expr) = 0;
        virtual std::any visitWhileExpr(WhileExpr* expr) = 0;
        virtual std::any visitForExpr(ForExpr* expr) = 0;
        virtual std::any visitBreakExpr(BreakExpr* expr) = 0;
        virtual std::any visitContinueExpr(ContinueExpr* expr) = 0;
        virtual std::any visitReturnExpr(ReturnExpr* expr) = 0;
        virtual std::any visitIndexAccess(IndexAccess* expr) = 0;    // ★ 新增
        virtual std::any visitIndexAssign(IndexAssign* expr) = 0;    // ★ 新增
        virtual std::any visitConstDecl(ConstDecl* expr) = 0;   // ★ 新增
        virtual std::any visitRefDecl(RefDecl* expr) = 0;       // ★ 新增
        virtual std::any visitStateDecl(StateDecl* expr) = 0;   // ★ 新增
        virtual std::any visitDeleteExpr(DeleteExpr* expr) = 0;   // ★ 新增
        virtual std::any visitCompoundAssign(CompoundAssign* expr) = 0;
        virtual std::any visitLambdaExpr(LambdaExpr* expr) = 0;
        virtual std::any visitInvokeExpr(InvokeExpr* expr) = 0;
        virtual std::any visitForInExpr(ForInExpr* expr) = 0;
        virtual std::any visitThrowExpr(ThrowExpr* expr) = 0;         // ★
        virtual std::any visitTryCatchExpr(TryCatchExpr* expr) = 0;   // ★
        virtual std::any visitImportExpr(ImportExpr* expr) = 0;      // ★
        virtual std::any visitSwitchExpr(SwitchExpr* expr) = 0;      // ★
        virtual std::any visitClassDefExpr(ClassDefExpr* expr) = 0;     // ★
        virtual std::any visitDotAccess(DotAccess* expr) = 0;           // ★
        virtual std::any visitDotAssign(DotAssign* expr) = 0;           // ★
        virtual std::any visitMethodCallExpr(MethodCallExpr* expr) = 0; // ★
        virtual std::any visitSuperExpr(SuperExpr* expr) = 0;
        virtual std::any visitDestructAssign(DestructAssign* expr) = 0;  // ★
        virtual std::any visitFStringExpr(FStringExpr* expr) = 0;  // ★
        virtual std::any visitListCompExpr(ListCompExpr* expr) = 0;  // ★
        virtual std::any visitDictLiteral(DictLiteral* expr) = 0;  // ★
        virtual std::any visitSliceExpr(SliceExpr* expr) = 0;  // ★ 新增
        virtual std::any visitDictDestructAssign(DictDestructAssign* expr) = 0;
        virtual std::any visitSequenceExpr(SequenceExpr* expr) = 0;
    };

    struct Expr {
        virtual ~Expr() = default;
        virtual std::any accept(ExprVisitor& visitor) = 0;
    };

    // ======== 原有节点 (不变) ========

    struct Binary : public Expr {
        std::unique_ptr<Expr> left;
        Token op;
        std::unique_ptr<Expr> right;
        Binary(std::unique_ptr<Expr> left, Token op, std::unique_ptr<Expr> right)
            : left(std::move(left)), op(std::move(op)), right(std::move(right)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitBinary(this); }
    };

    struct Unary : public Expr {
        Token op;
        std::unique_ptr<Expr> right;
        Unary(Token op, std::unique_ptr<Expr> right)
            : op(std::move(op)), right(std::move(right)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitUnary(this); }
    };

    struct Literal : public Expr {
        std::string value;
        bool isString;
        bool isImaginary;  // ★
        explicit Literal(std::string value, bool isStr = false, bool isImag = false)
            : value(std::move(value)), isString(isStr), isImaginary(isImag) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitLiteral(this); }
    };

    struct Variable : public Expr {
        Token name;
        explicit Variable(Token name) : name(std::move(name)) {}
        std::any accept(ExprVisitor& visitor) override { return visitor.visitVariable(this); }
    };

    struct Assign : public Expr {
        Token name;
        std::unique_ptr<Expr> value;
        bool isRef; // ★ 新增
        bool isState; // ★ 新增
        Assign(Token name, std::unique_ptr<Expr> value, bool isRef = false, bool isState = false)
            : name(std::move(name)), value(std::move(value)), isRef(isRef), isState(isState) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitAssign(this); }
    };

    struct Call : public Expr {
        Token callee;
        std::vector<std::unique_ptr<Expr>> arguments;
        Call(Token callee, std::vector<std::unique_ptr<Expr>> arguments)
            : callee(std::move(callee)), arguments(std::move(arguments)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitCall(this); }
    };

    struct MatrixNode : public Expr {
        std::vector<std::vector<std::unique_ptr<Expr>>> elements;
        explicit MatrixNode(std::vector<std::vector<std::unique_ptr<Expr>>> elements)
            : elements(std::move(elements)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitMatrixNode(this); }
    };

    // ======== ★ 新增控制流节点 ========

    // { stmt1; stmt2; ... stmtN } -> 返回最后一条语句的值
    struct Block : public Expr {
        std::vector<std::unique_ptr<Expr>> statements;
        explicit Block(std::vector<std::unique_ptr<Expr>> statements)
            : statements(std::move(statements)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitBlock(this); }
    };

    // if (cond) { ... } else { ... }
    struct IfExpr : public Expr {
        std::unique_ptr<Expr> condition;
        std::unique_ptr<Expr> thenBranch;
        std::unique_ptr<Expr> elseBranch; // 可为 nullptr
        IfExpr(std::unique_ptr<Expr> condition,
            std::unique_ptr<Expr> thenBranch,
            std::unique_ptr<Expr> elseBranch)
            : condition(std::move(condition)),
            thenBranch(std::move(thenBranch)),
            elseBranch(std::move(elseBranch)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitIfExpr(this); }
    };

    // while (cond) { ... }
    struct WhileExpr : public Expr {
        std::unique_ptr<Expr> condition;
        std::unique_ptr<Expr> body;
        WhileExpr(std::unique_ptr<Expr> condition, std::unique_ptr<Expr> body)
            : condition(std::move(condition)), body(std::move(body)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitWhileExpr(this); }
    };

    // for (init; cond; update) { ... }
    struct ForExpr : public Expr {
        std::unique_ptr<Expr> initializer;
        std::unique_ptr<Expr> condition;
        std::unique_ptr<Expr> update;
        std::unique_ptr<Expr> body;
        ForExpr(std::unique_ptr<Expr> init, std::unique_ptr<Expr> cond,
            std::unique_ptr<Expr> upd, std::unique_ptr<Expr> body)
            : initializer(std::move(init)), condition(std::move(cond)),
            update(std::move(upd)), body(std::move(body)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitForExpr(this); }
    };

    struct BreakExpr : public Expr {
        std::any accept(ExprVisitor& visitor) override { return visitor.visitBreakExpr(this); }
    };

    struct ContinueExpr : public Expr {
        std::any accept(ExprVisitor& visitor) override { return visitor.visitContinueExpr(this); }
    };

    struct ReturnExpr : public Expr {
        std::unique_ptr<Expr> value; // 可为 nullptr（裸 return）
        explicit ReturnExpr(std::unique_ptr<Expr> value)
            : value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitReturnExpr(this); }
    };

    struct RefDecl : public Expr {
        Token name;
        explicit RefDecl(Token name) : name(std::move(name)) {}
        std::any accept(ExprVisitor& visitor) override { return visitor.visitRefDecl(this); }
    };

    struct StateDecl : public Expr {
        Token name;
        explicit StateDecl(Token name) : name(std::move(name)) {}
        std::any accept(ExprVisitor& visitor) override { return visitor.visitStateDecl(this); }
    };

    struct IndexAccess : public Expr {
        std::unique_ptr<Expr> object;                   // 被索引的表达式
        std::vector<std::unique_ptr<Expr>> indices;     // 1 或 2 个索引
        IndexAccess(std::unique_ptr<Expr> object, std::vector<std::unique_ptr<Expr>> indices)
            : object(std::move(object)), indices(std::move(indices)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitIndexAccess(this); }
    };

    struct IndexAssign : public Expr {
        Token name;
        std::unique_ptr<Expr> objectExpr;  // ★ 非空时表示根是表达式（如 self.data）
        std::vector<std::vector<std::unique_ptr<Expr>>> indexChain;
        std::unique_ptr<Expr> value;

        // 原始构造函数（Variable 根）
        IndexAssign(Token name, std::vector<std::vector<std::unique_ptr<Expr>>> indexChain,
            std::unique_ptr<Expr> value)
            : name(std::move(name)), objectExpr(nullptr),
            indexChain(std::move(indexChain)), value(std::move(value)) {
        }

        // ★ 新构造函数（表达式根，如 self.data[i,j] = v）
        IndexAssign(std::unique_ptr<Expr> objectExpr,
            std::vector<std::vector<std::unique_ptr<Expr>>> indexChain,
            std::unique_ptr<Expr> value)
            : name(Token(TokenType::IDENTIFIER, "", 0)), objectExpr(std::move(objectExpr)),
            indexChain(std::move(indexChain)), value(std::move(value)) {
        }

        bool hasObjectExpr() const { return objectExpr != nullptr; }

        std::any accept(ExprVisitor& visitor) override { return visitor.visitIndexAssign(this); }
    };

    struct ConstDecl : public Expr {
        Token name;
        std::unique_ptr<Expr> value;
        ConstDecl(Token name, std::unique_ptr<Expr> value)
            : name(std::move(name)), value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitConstDecl(this); }
    };

    struct DeleteExpr : public Expr {
        std::vector<Token> names;
        explicit DeleteExpr(std::vector<Token> names)
            : names(std::move(names)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDeleteExpr(this); }
    };

    struct CompoundAssign : public Expr {
        std::unique_ptr<Expr> target;   // Variable 或 IndexAccess
        TokenType op;                    // PLUS, MINUS, STAR, SLASH, PERCENT, CARET
        std::unique_ptr<Expr> value;
        bool isRef; // ★ 新增
        bool isState; // ★ 新增
        CompoundAssign(std::unique_ptr<Expr> target, TokenType op, std::unique_ptr<Expr> value, bool isRef = false, bool isState = false)
            : target(std::move(target)), op(op), value(std::move(value)), isRef(isRef), isState(isState) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitCompoundAssign(this); }
    };

    struct LambdaExpr : public Expr {
        std::string name;
        std::vector<Token> params;
        std::vector<bool> paramIsRef;
        std::vector<std::shared_ptr<Expr>> defaultExprs;
        bool hasRestParam;

        std::vector<std::string> paramTypes; // ★ 新增：参数类型约束   
        std::string returnType;              // ★ 新增：返回值约束    

        std::string rawBody;
        std::shared_ptr<Expr> body;

        LambdaExpr(std::string name, std::vector<Token> params, std::vector<bool> paramIsRef,
            std::vector<std::shared_ptr<Expr>> defaultExprs, bool hasRestParam,
            std::vector<std::string> paramTypes, std::string returnType, // ★ 新增
            std::string rawBody, std::shared_ptr<Expr> body)
            : name(std::move(name)), params(std::move(params)), paramIsRef(std::move(paramIsRef)),
            defaultExprs(std::move(defaultExprs)),
            hasRestParam(hasRestParam),
            paramTypes(std::move(paramTypes)), returnType(std::move(returnType)), // ★ 新增
            rawBody(std::move(rawBody)), body(std::move(body)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitLambdaExpr(this); }
    };

    struct InvokeExpr : public Expr {
        std::unique_ptr<Expr> callee;
        std::vector<std::unique_ptr<Expr>> arguments;
        InvokeExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> arguments)
            : callee(std::move(callee)), arguments(std::move(arguments)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitInvokeExpr(this); }
    };

    struct ForInExpr : public Expr {
        Token varName;
        std::vector<Token> destructNames;  // ★ non-empty = destructured [a, b, ...]
        std::unique_ptr<Expr> iterable;
        std::unique_ptr<Expr> body;

        bool isDestruct() const { return !destructNames.empty(); }

        // Single variable: for (x in ...)
        ForInExpr(Token varName, std::unique_ptr<Expr> iterable, std::unique_ptr<Expr> body)
            : varName(std::move(varName)), iterable(std::move(iterable)), body(std::move(body)) {
        }

        // ★ Destructured: for ([a, b] in ...)
        ForInExpr(std::vector<Token> destructNames, std::unique_ptr<Expr> iterable,
            std::unique_ptr<Expr> body)
            : varName(Token(TokenType::IDENTIFIER, "", 0)),
            destructNames(std::move(destructNames)),
            iterable(std::move(iterable)), body(std::move(body)) {
        }

        std::any accept(ExprVisitor& visitor) override { return visitor.visitForInExpr(this); }
    };

    // ★ throw expr
    struct ThrowExpr : public Expr {
        std::unique_ptr<Expr> value;
        explicit ThrowExpr(std::unique_ptr<Expr> value)
            : value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitThrowExpr(this); }
    };

    // ★ try { ... } catch (e) { ... }
    struct TryCatchExpr : public Expr {
        std::unique_ptr<Expr> tryBody;
        Token catchName;
        std::unique_ptr<Expr> catchBody;
        TryCatchExpr(std::unique_ptr<Expr> tryBody, Token catchName, std::unique_ptr<Expr> catchBody)
            : tryBody(std::move(tryBody)), catchName(std::move(catchName)), catchBody(std::move(catchBody)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitTryCatchExpr(this); }
    };

    struct ImportExpr : public Expr {
        std::unique_ptr<Expr> path;
        ImportExpr(std::unique_ptr<Expr> path)
            : path(std::move(path)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitImportExpr(this); }
    };

    // ★ switch (expr) { case v1: { body } case v2, v3: { body } default: { body } }
    struct SwitchExpr : public Expr {
        std::unique_ptr<Expr> subject;
        // 每个 case: (匹配值列表, body)
        std::vector<std::pair<std::vector<std::unique_ptr<Expr>>, std::unique_ptr<Expr>>> cases;
        std::unique_ptr<Expr> defaultBody; // 可为 nullptr
        SwitchExpr(std::unique_ptr<Expr> subject,
            std::vector<std::pair<std::vector<std::unique_ptr<Expr>>, std::unique_ptr<Expr>>> cases,
            std::unique_ptr<Expr> defaultBody)
            : subject(std::move(subject)), cases(std::move(cases)),
            defaultBody(std::move(defaultBody)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitSwitchExpr(this); }
    };

    struct ClassDefExpr : public Expr {
        Token name;
        std::string superClassName;
        struct MethodDef {
            Token name;
            std::vector<Token> params;
            std::vector<bool> paramIsRef;
            std::vector<std::shared_ptr<Expr>> defaultExprs;
            bool hasRestParam;

            std::vector<std::string> paramTypes; // ★ 新增
            std::string returnType;              // ★ 新增

            std::string rawBody;
            std::shared_ptr<Expr> body;
        };
        std::vector<MethodDef> methods;

        ClassDefExpr(Token name, std::string superClassName, std::vector<MethodDef> methods)
            : name(std::move(name)), superClassName(std::move(superClassName)),
            methods(std::move(methods)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitClassDefExpr(this); }
    };

    // ★ obj.field
    struct DotAccess : public Expr {
        std::unique_ptr<Expr> object;
        Token field;
        DotAccess(std::unique_ptr<Expr> object, Token field)
            : object(std::move(object)), field(std::move(field)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDotAccess(this); }
    };
    // ★ obj.field = value
    struct DotAssign : public Expr {
        std::unique_ptr<Expr> object;
        Token field;
        std::unique_ptr<Expr> value;
        DotAssign(std::unique_ptr<Expr> object, Token field, std::unique_ptr<Expr> value)
            : object(std::move(object)), field(std::move(field)), value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDotAssign(this); }
    };
    // ★ obj.method(args)
    struct MethodCallExpr : public Expr {
        std::unique_ptr<Expr> object;
        Token method;
        std::vector<std::unique_ptr<Expr>> arguments;
        MethodCallExpr(std::unique_ptr<Expr> object, Token method,
            std::vector<std::unique_ptr<Expr>> arguments)
            : object(std::move(object)), method(std::move(method)),
            arguments(std::move(arguments)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitMethodCallExpr(this); }
    };

    // ★ super — evaluates to a proxy that dispatches to parent class
    struct SuperExpr : public Expr {
        std::any accept(ExprVisitor& visitor) override { return visitor.visitSuperExpr(this); }
    };

    // ★ [a, b, c] = expr
    struct DestructAssign : public Expr {
        struct Target {
            Token name;
            bool isRef;
            bool isState;
        };
        std::vector<Target> targets;
        std::unique_ptr<Expr> value;
        DestructAssign(std::vector<Target> targets, std::unique_ptr<Expr> value)
            : targets(std::move(targets)), value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDestructAssign(this); }
    };

    // ★ f"Hello, {name}! x = {x:.2f}"
    struct FStringExpr : public Expr {
        // literals[0] {exprs[0]} literals[1] {exprs[1]} ... literals[N]
        std::vector<std::string> literals;            // N+1 段纯文本
        std::vector<std::unique_ptr<Expr>> exprs;     // N 个表达式
        std::vector<std::string> formatSpecs;         // N 个格式说明符（空 = 默认）
        FStringExpr(std::vector<std::string> literals,
            std::vector<std::unique_ptr<Expr>> exprs,
            std::vector<std::string> formatSpecs)
            : literals(std::move(literals)), exprs(std::move(exprs)),
            formatSpecs(std::move(formatSpecs)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitFStringExpr(this); }
    };

    // ★ [expr for var in iterable if condition]
    struct ListCompExpr : public Expr {
        struct CompClause {
            Token varName;
            std::vector<Token> destructNames;
            std::shared_ptr<Expr> iterable;
            bool isDestruct() const { return !destructNames.empty(); }
            CompClause(Token var, std::shared_ptr<Expr> iter)
                : varName(std::move(var)), iterable(std::move(iter)) {
            }
            CompClause(std::vector<Token> destruct, std::shared_ptr<Expr> iter)
                : varName(Token(TokenType::IDENTIFIER, "", 0)),
                destructNames(std::move(destruct)), iterable(std::move(iter)) {
            }
        };

        std::unique_ptr<Expr> valueExpr;
        std::vector<CompClause> clauses;
        std::unique_ptr<Expr> condition;  // nullptr = no filter

        ListCompExpr(std::unique_ptr<Expr> valueExpr, std::vector<CompClause> clauses,
            std::unique_ptr<Expr> condition)
            : valueExpr(std::move(valueExpr)), clauses(std::move(clauses)),
            condition(std::move(condition)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitListCompExpr(this); }
    };

    // ★ {key: value, key: value, ...}
    struct DictLiteral : public Expr {
        // 每个 entry: (key 表达式, value 表达式)
        // 裸标识符 key 在 Parser 中被转换为 Literal(string)
        std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries;
        explicit DictLiteral(std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> entries)
            : entries(std::move(entries)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDictLiteral(this); }
    };

    // ★ 切片表达式 start:end:step (仅在索引中有效)
    struct SliceExpr : public Expr {
        std::unique_ptr<Expr> start; // nullptr = 缺省
        std::unique_ptr<Expr> end;   // nullptr = 缺省
        std::unique_ptr<Expr> step;  // nullptr = 缺省
        SliceExpr(std::unique_ptr<Expr> start, std::unique_ptr<Expr> end, std::unique_ptr<Expr> step)
            : start(std::move(start)), end(std::move(end)), step(std::move(step)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitSliceExpr(this); }
    };

    struct DictDestructAssign : public Expr {
        struct Target {
            std::string key;
            Token name;
            bool isRef;
            bool isState;
        };
        std::vector<Target> targets;
        std::unique_ptr<Expr> value;
        DictDestructAssign(std::vector<Target> targets, std::unique_ptr<Expr> value)
            : targets(std::move(targets)), value(std::move(value)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitDictDestructAssign(this); }
    };

    // ★ 逗号表达式序列 (expr1, expr2, expr3) -> 返回 expr3 的值
    struct SequenceExpr : public Expr {
        std::vector<std::unique_ptr<Expr>> expressions;
        explicit SequenceExpr(std::vector<std::unique_ptr<Expr>> expressions)
            : expressions(std::move(expressions)) {
        }
        std::any accept(ExprVisitor& visitor) override { return visitor.visitSequenceExpr(this); }
    };

} // namespace jc
#endif // JC2_EXPR_H

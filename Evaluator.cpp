#include "Evaluator.h"
#include "Lexer.h"       // ★ 新增：eval() 需要
#include "Parser.h"      // ★ 新增：eval() 需要
#include <cmath>
#include <iostream>
#include <random>
#include <algorithm>     // ★ 新增：transform
#include <cctype>        // ★ 新增：toupper/tolower
#include <chrono>
#include <thread>
#include "Highlight.h"       
#include "Module.h"
#include "Image.h"
#include "Probability.h"

namespace jc {
    using CapturedEnv = std::map<std::string, Value>;

    static bool endsWithContinuation(const std::string& line) {
        size_t e = line.find_last_not_of(" \t\r\n");
        if (e == std::string::npos) return false;
        if (e >= 1 && line[e - 1] == '|' && line[e] == '>') return true;
        if (e >= 1 && line[e - 1] == '&' && line[e] == '&') return true;
        if (e >= 1 && line[e - 1] == '|' && line[e] == '|') return true;
        char c = line[e];
        return (c == '+' || c == '-' || c == '*' || c == '/' || c == '\\' ||
            c == '%' || c == '^' || c == ',' || c == '=');
    }

    std::string Evaluator::cwd() const {
        if (!scriptDirStack.empty()) return scriptDirStack.back();
        return std::filesystem::current_path().string();
    }

    std::string Evaluator::getExeDir() const {
        if (!exeDir.empty()) return exeDir;
        return std::filesystem::current_path().string();
    }

    void Evaluator::pushScriptDir(const std::string& dir) {
        scriptDirStack.push_back(dir);
    }

    void Evaluator::popScriptDir() {
        if (!scriptDirStack.empty()) scriptDirStack.pop_back();
    }

    std::string Evaluator::resolvePath(const std::string& path) const {
        namespace fs = std::filesystem;
        fs::path p(path);

        // 已经是绝对路径，直接返回
        if (p.is_absolute()) return p.string();

        // ★ 获取当前脚本所在目录
        fs::path currentDir = fs::path(cwd());

        // 拼接路径，并调用 weakly_canonical 清理多余的 ".\" 或 "\data\data" 冗余
        fs::path resolved = currentDir / p;
        return fs::weakly_canonical(resolved).string();
    }

    std::string Evaluator::getWorkspacePath() const {
        if (!workspacePath.empty()) return workspacePath;
        namespace fs = std::filesystem;
        fs::path dir = fs::current_path() / "data";
        if (!fs::exists(dir)) fs::create_directory(dir);
        return dir.string();
    }

    void Evaluator::setWorkspacePath(const std::string& p) {
        namespace fs = std::filesystem;
        fs::path dir(p);
        if (!dir.is_absolute())
            dir = fs::path(cwd()) / dir;
        if (!fs::exists(dir))
            fs::create_directories(dir);
        workspacePath = fs::weakly_canonical(dir).string();
    }

    static std::vector<Value> fillDefaults(
        const std::shared_ptr<FunctionClosure>& cl,
        std::vector<Value> args,
        const std::string& name = "")
    {
        size_t np = cl->paramNames.size();
        size_t na = args.size();
        if (static_cast<int>(na) > cl->maxArgs()) {
            throw std::runtime_error("Runtime Error: " +
                (name.empty() ? std::string("Function") : "'" + name + "'") +
                " expects at most " + std::to_string(np) +
                " arguments, got " + std::to_string(na) + ".");
        }
        for (size_t i = na; i < np; ++i) {
            if (i < cl->defaultValues.size() && !cl->defaultValues[i].isNone())
                args.push_back(cl->defaultValues[i]);
            else
                throw std::runtime_error("Runtime Error: " +
                    (name.empty() ? std::string("Function") : "'" + name + "'") +
                    " missing required argument '" + cl->paramNames[i] + "'.");
        }
        return args;
    }

    bool Evaluator::loadNativeModule(const std::string& name) {
        auto& modules = getNativeModules();
        auto it = modules.find(name);
        if (it == modules.end()) return false;

        // 防重复加载
        if (importedFiles.count("__native__:" + name)) return true;
        importedFiles.insert("__native__:" + name);

        it->second.loader(environment, builtins, builtinArity);
        std::cout << "[System] Native module '" << name << "' loaded." << std::endl;
        return true;
    }

    bool Evaluator::hasNativeModule(const std::string& name) const {
        return getNativeModules().count(name) > 0;
    }

    std::vector<std::string> Evaluator::listNativeModules() const {
        std::vector<std::string> result;
        for (const auto& [k, v] : getNativeModules()) {
            result.push_back(k);
        }
        return result;
    }

    static void validateDefaultOrder(
        const std::vector<std::string>& paramNames,
        const std::vector<Value>& defaults,
        const std::string& funcName)
    {
        bool seenDefault = false;
        for (size_t i = 0; i < paramNames.size(); ++i) {
            bool hasDefault = i < defaults.size() && !defaults[i].isNone();
            if (hasDefault) {
                seenDefault = true;
            }
            else if (seenDefault) {
                throw std::runtime_error("Syntax Error: In '" + funcName +
                    "', required parameter '" + paramNames[i] +
                    "' cannot follow a parameter with a default value.");
            }
        }
    }

    static std::vector<Value> getIterableElements(const Value& iterVal) {
        std::vector<Value> elems;
        if (std::holds_alternative<RealMatrix>(iterVal.data)) {
            const auto& m = std::get<RealMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) elems.push_back(Value(m(0, j)));
            }
            else if (m.getCols() == 1) {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m(i, 0)));
            }
            else {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m.getRow(i)));
            }
        }
        else if (std::holds_alternative<ComplexMatrix>(iterVal.data)) {
            const auto& m = std::get<ComplexMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) elems.push_back(Value(m(0, j)));
            }
            else if (m.getCols() == 1) {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m(i, 0)));
            }
            else {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m.getRow(i)));
            }
        }
        else if (std::holds_alternative<std::string>(iterVal.data)) {
            for (char c : std::get<std::string>(iterVal.data))
                elems.push_back(Value(std::string(1, c)));
        }
        else if (std::holds_alternative<StringMatrix>(iterVal.data)) {
            const auto& m = std::get<StringMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) elems.push_back(Value(m(0, j)));
            }
            else if (m.getCols() == 1) {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m(i, 0)));
            }
            else {
                for (int i = 0; i < m.getRows(); ++i) elems.push_back(Value(m.getRow(i)));
            }
        }
        else if (std::holds_alternative<List>(iterVal.data)) {
            for (const auto& e : std::get<List>(iterVal.data).raw())
                elems.push_back(std::any_cast<Value>(e));
        }
        else if (std::holds_alternative<Dict>(iterVal.data)) {
            for (const auto& [key, val] : std::get<Dict>(iterVal.data).getEntries())
                elems.push_back(Value(key));
        }
        else {
            throw std::runtime_error("Type Error: Cannot iterate over this type in list comprehension.");
        }
        return elems;
    }

    static std::vector<std::pair<std::string, std::pair<bool, Value>>>
        injectCaptures(std::map<std::string, Value>& env,
            const std::any& capturedAny,                    // ★ 改
            const std::vector<std::string>& paramNames) {
        std::vector<std::pair<std::string, std::pair<bool, Value>>> saved;
        if (!capturedAny.has_value()) return saved;                // ★ 无捕获直接返回
        const auto& captured = std::any_cast<const CapturedEnv&>(capturedAny);
        for (const auto& [k, v] : captured) {
            bool isParam = false;
            for (const auto& p : paramNames)
                if (k == p) { isParam = true; break; }
            if (isParam) continue;
            auto it = env.find(k);
            if (it != env.end()) saved.push_back({ k, {true, it->second} });
            else saved.push_back({ k, {false, Value::none()} });
            env[k] = v;
        }
        return saved;
    }

    static void restoreCaptures(std::map<std::string, Value>& env,
        const std::vector<std::pair<std::string, std::pair<bool, Value>>>& saved) {
        for (auto& [name, info] : saved) {
            if (info.first) env[name] = info.second;
            else env.erase(name);
        }
    }

    static std::vector<Value> extractElements(const Value& val) {
        std::vector<Value> elements;
        if (std::holds_alternative<RealMatrix>(val.data)) {
            for (double d : std::get<RealMatrix>(val.data).rawData())
                elements.push_back(Value(d));
        }
        else if (std::holds_alternative<ComplexMatrix>(val.data)) {
            for (const auto& c : std::get<ComplexMatrix>(val.data).rawData())
                elements.push_back(Value(c));
        }
        else if (std::holds_alternative<List>(val.data)) {
            for (const auto& e : std::get<List>(val.data).raw())
                elements.push_back(std::any_cast<Value>(e));
        }
        else if (std::holds_alternative<StringMatrix>(val.data)) {
            for (const auto& s : std::get<StringMatrix>(val.data).rawData())
                elements.push_back(Value(s));
        }
        else {
            throw std::runtime_error(
                "Type Error: Cannot destructure this type. "
                "Expected an array, list, vector, or matrix.");
        }
        return elements;
    }

    

    // =================================================================
    // 比较运算核心引擎
    // =================================================================
    static bool valuesEqual(const Value& lhs, const Value& rhs) {
        if (lhs.data.index() == rhs.data.index()) {
            if (std::holds_alternative<double>(lhs.data))
                return Tol::isEq(std::get<double>(lhs.data), std::get<double>(rhs.data));
            if (std::holds_alternative<BigInt>(lhs.data))
                return std::get<BigInt>(lhs.data) == std::get<BigInt>(rhs.data);
            if (std::holds_alternative<Complex>(lhs.data))
                return std::get<Complex>(lhs.data) == std::get<Complex>(rhs.data);
            if (std::holds_alternative<Fraction>(lhs.data))
                return std::get<Fraction>(lhs.data) == std::get<Fraction>(rhs.data);
            if (std::holds_alternative<std::string>(lhs.data))
                return std::get<std::string>(lhs.data) == std::get<std::string>(rhs.data);
            if (std::holds_alternative<BaseNum>(lhs.data))
                return std::get<BaseNum>(lhs.data).getValue() == std::get<BaseNum>(rhs.data).getValue();
            if (std::holds_alternative<RealMatrix>(lhs.data)) {
                const auto& a = std::get<RealMatrix>(lhs.data);
                const auto& b = std::get<RealMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!Tol::isEq(a(i, j), b(i, j))) return false;
                return true;
            }
            if (std::holds_alternative<ComplexMatrix>(lhs.data)) {
                const auto& a = std::get<ComplexMatrix>(lhs.data);
                const auto& b = std::get<ComplexMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!(a(i, j) == b(i, j))) return false;
                return true;
            }
            return false;
        }
        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data))
            return Fraction(std::get<BigInt>(lhs.data)) == std::get<Fraction>(rhs.data);
        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))
            return std::get<Fraction>(lhs.data) == Fraction(std::get<BigInt>(rhs.data));
        if ((std::holds_alternative<RealMatrix>(lhs.data) && std::holds_alternative<ComplexMatrix>(rhs.data)) ||
            (std::holds_alternative<ComplexMatrix>(lhs.data) && std::holds_alternative<RealMatrix>(rhs.data))) {
            try {
                ComplexMatrix a = lhs.asComplexMatrix(), b = rhs.asComplexMatrix();
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!(a(i, j) == b(i, j))) return false;
                return true;
            }
            catch (...) { return false; }
        }
        if (std::holds_alternative<std::shared_ptr<Instance>>(lhs.data) &&
            std::holds_alternative<std::shared_ptr<Instance>>(rhs.data))
            return std::get<std::shared_ptr<Instance>>(lhs.data).get() ==
            std::get<std::shared_ptr<Instance>>(rhs.data).get();
        try { return lhs.asComplex() == rhs.asComplex(); }
        catch (...) { return false; }
    }

    std::any Evaluator::visitSliceExpr(SliceExpr*) {
        throw std::runtime_error("Syntax Error: Slice operator ':' can only be used inside brackets [ ].");
    }

    std::pair<std::vector<int>, bool> Evaluator::buildIndices(Expr* expr, int dimSize) {
        if (auto* slice = dynamic_cast<SliceExpr*>(expr)) {
            int st = 0, en = dimSize, sp = 1;
            if (slice->step) {
                sp = static_cast<int>(std::round(evaluate(slice->step.get()).asDouble()));
                if (sp == 0) throw std::runtime_error("Index Error: Slice step cannot be zero.");
            }
            if (slice->start) {
                st = static_cast<int>(std::round(evaluate(slice->start.get()).asDouble()));
                if (st < 0) st = dimSize + st;
            }
            else if (sp < 0) { st = dimSize - 1; }

            if (slice->end) {
                en = static_cast<int>(std::round(evaluate(slice->end.get()).asDouble()));
                if (en < 0) en = dimSize + en;
            }
            else if (sp < 0) { en = -1; }

            std::vector<int> ids;
            if (sp > 0) {
                st = std::max(0, std::min(dimSize, st));
                en = std::max(0, std::min(dimSize, en));
                for (int i = st; i < en; i += sp) ids.push_back(i);
            }
            else {
                st = std::max(-1, std::min(dimSize - 1, st));
                en = std::max(-1, std::min(dimSize - 1, en));
                for (int i = st; i > en; i += sp) ids.push_back(i);
            }
            return { ids, true }; // isSlice = true
        }

        Value val = evaluate(expr);
        if (std::holds_alternative<RealMatrix>(val.data)) {
            std::vector<int> ids;
            for (double d : std::get<RealMatrix>(val.data).rawData()) {
                int i = static_cast<int>(std::round(d));
                if (i < 0) i = dimSize + i;
                if (i < 0 || i >= dimSize) throw std::out_of_range("Index Error: Array index out of bounds.");
                ids.push_back(i);
            }
            return { ids, true }; // Array index is treated as slice-like bulk
        }

        int i = static_cast<int>(std::round(val.asDouble()));
        if (i < 0) i = dimSize + i;
        if (i < 0 || i >= dimSize) throw std::out_of_range("Index Error: Index out of bounds.");
        return { {i}, false }; // isSlice = false
    }

    Value Evaluator::readSingleIndex(Value& container, const std::vector<std::unique_ptr<Expr>>& indices) {
        if (std::holds_alternative<Dict>(container.data)) {
            if (indices.size() != 1) throw std::runtime_error("Type Error: Dict access requires exactly 1 key.");
            Value keyVal = evaluate(indices[0].get());
            std::string key = (std::holds_alternative<std::string>(keyVal.data)) ? std::get<std::string>(keyVal.data) : keyVal.toJC2Expression();
            const auto* entry = std::get<Dict>(container.data).get(key);
            if (!entry) throw std::runtime_error("Runtime Error: Key '" + key + "' not found.");
            return std::any_cast<Value>(*entry);
        }

        if (std::holds_alternative<List>(container.data)) {
            if (indices.size() != 1) throw std::runtime_error("Type Error: List access requires exactly 1 index.");
            auto [ids, isSlice] = buildIndices(indices[0].get(), static_cast<int>(std::get<List>(container.data).size()));
            if (!isSlice) return std::any_cast<Value>(std::get<List>(container.data).at(ids[0]));
            List result;
            for (int id : ids) result.push_back(std::get<List>(container.data).at(id));
            return Value(result);
        }

        if (std::holds_alternative<std::string>(container.data)) {
            if (indices.size() != 1) throw std::runtime_error("Type Error: String access requires exactly 1 index.");
            const auto& s = std::get<std::string>(container.data);
            auto [ids, isSlice] = buildIndices(indices[0].get(), static_cast<int>(s.size()));
            if (!isSlice) return Value(std::string(1, s[ids[0]]));
            std::string result;
            for (int id : ids) result += s[id];
            return Value(result);
        }

        auto processMatrixRead = [&](const auto& m) -> Value {
            std::vector<int> rIds, cIds;
            bool sR = false, sC = false;

            if (indices.size() == 2) {
                std::tie(rIds, sR) = buildIndices(indices[0].get(), m.getRows());
                std::tie(cIds, sC) = buildIndices(indices[1].get(), m.getCols());
            }
            else if (indices.size() == 1) {
                if (m.getCols() == 1) {
                    std::tie(rIds, sR) = buildIndices(indices[0].get(), m.getRows());
                    cIds = { 0 }; sC = false;
                }
                else if (m.getRows() == 1) {
                    std::tie(cIds, sC) = buildIndices(indices[0].get(), m.getCols());
                    rIds = { 0 }; sR = false;
                }
                else {
                    std::tie(rIds, sR) = buildIndices(indices[0].get(), m.getRows());
                    for (int i = 0; i < m.getCols(); i++) cIds.push_back(i);
                    sC = true;
                }
            }
            else {
                throw std::runtime_error("Type Error: Invalid matrix indexing.");
            }

            if (!sR && !sC) return Value(m(rIds[0], cIds[0]));

            using MatType = typename std::decay_t<decltype(m)>;
            MatType res(static_cast<int>(rIds.size()), static_cast<int>(cIds.size()));
            for (size_t i = 0; i < rIds.size(); ++i) {
                for (size_t j = 0; j < cIds.size(); ++j) {
                    res(static_cast<int>(i), static_cast<int>(j)) = m(rIds[i], cIds[j]);
                }
            }
            return Value(res);
            };

        if (std::holds_alternative<RealMatrix>(container.data)) return processMatrixRead(std::get<RealMatrix>(container.data));
        if (std::holds_alternative<ComplexMatrix>(container.data)) return processMatrixRead(std::get<ComplexMatrix>(container.data));
        if (std::holds_alternative<StringMatrix>(container.data)) return processMatrixRead(std::get<StringMatrix>(container.data));

        throw std::runtime_error("Type Error: Cannot index this type.");
    }

    void Evaluator::writeSingleIndex(Value& container, const std::vector<std::unique_ptr<Expr>>& indices, const Value& val) {
        if (std::holds_alternative<Dict>(container.data)) {
            Value keyVal = evaluate(indices[0].get());
            std::string key = (std::holds_alternative<std::string>(keyVal.data)) ? std::get<std::string>(keyVal.data) : keyVal.toJC2Expression();
            std::get<Dict>(container.data).set(key, std::make_any<Value>(val));
            return;
        }
        if (std::holds_alternative<List>(container.data)) {
            auto [ids, isSlice] = buildIndices(indices[0].get(), static_cast<int>(std::get<List>(container.data).size()));
            if (!isSlice) { std::get<List>(container.data).set(ids[0], std::make_any<Value>(val)); return; }
            if (!std::holds_alternative<List>(val.data)) throw std::runtime_error("Type Error: List slice assignment requires a list RHS.");
            const auto& rhsList = std::get<List>(val.data);
            if (rhsList.size() != ids.size()) throw std::runtime_error("Type Error: List slice assignment size mismatch.");
            for (size_t i = 0; i < ids.size(); i++) std::get<List>(container.data).set(ids[i], rhsList.raw()[i]);
            return;
        }

        auto processMatrixWrite = [&](auto& m) {
            std::vector<int> rIds, cIds;
            if (indices.size() == 2) {
                rIds = buildIndices(indices[0].get(), m.getRows()).first;
                cIds = buildIndices(indices[1].get(), m.getCols()).first;
            }
            else if (indices.size() == 1) {
                if (m.getCols() == 1) { rIds = buildIndices(indices[0].get(), m.getRows()).first; cIds = { 0 }; }
                else if (m.getRows() == 1) { cIds = buildIndices(indices[0].get(), m.getCols()).first; rIds = { 0 }; }
                else { rIds = buildIndices(indices[0].get(), m.getRows()).first; for (int i = 0; i < m.getCols(); ++i) cIds.push_back(i); }
            }
            int dstR = static_cast<int>(rIds.size()), dstC = static_cast<int>(cIds.size());

            // ★ 纯标量广播 (修复 BigInt、Fraction 等类型标量无法广播的问题)
            bool isScalar = std::holds_alternative<double>(val.data) ||
                std::holds_alternative<BigInt>(val.data) ||
                std::holds_alternative<Fraction>(val.data) ||
                std::holds_alternative<BaseNum>(val.data) ||
                std::holds_alternative<Complex>(val.data) ||
                std::holds_alternative<std::string>(val.data);

            if (isScalar) {
                for (int r : rIds) {
                    for (int c : cIds) {
                        if constexpr (std::is_same_v<std::decay_t<decltype(m)>, RealMatrix>) m(r, c) = val.asDouble();
                        else if constexpr (std::is_same_v<std::decay_t<decltype(m)>, ComplexMatrix>) m(r, c) = val.asComplex();
                        else {
                            if (std::holds_alternative<std::string>(val.data)) m(r, c) = std::get<std::string>(val.data);
                            else { std::ostringstream oss; oss << val; m(r, c) = oss.str(); }
                        }
                    }
                }
                return;
            }

            // 矩阵块注入
            using MatType = typename std::decay_t<decltype(m)>;

            // 若左侧是 StringMatrix，允许右侧是各种类型的数值矩阵，在其注入时隐式进行 str 降维
            if constexpr (std::is_same_v<MatType, StringMatrix>) {
                if (std::holds_alternative<RealMatrix>(val.data)) {
                    auto rhsMat = std::get<RealMatrix>(val.data);
                    if (rhsMat.getRows() != dstR || rhsMat.getCols() != dstC) throw std::runtime_error("Math Error: Matrix slice assignment size mismatch.");
                    for (int i = 0; i < dstR; ++i) for (int j = 0; j < dstC; ++j) { std::ostringstream oss; oss << Value(rhsMat(i, j)); m(rIds[i], cIds[j]) = oss.str(); } return;
                }
                if (std::holds_alternative<ComplexMatrix>(val.data)) {
                    auto rhsMat = std::get<ComplexMatrix>(val.data);
                    if (rhsMat.getRows() != dstR || rhsMat.getCols() != dstC) throw std::runtime_error("Math Error: Matrix slice assignment size mismatch.");
                    for (int i = 0; i < dstR; ++i) for (int j = 0; j < dstC; ++j) { std::ostringstream oss; oss << Value(rhsMat(i, j)); m(rIds[i], cIds[j]) = oss.str(); } return;
                }
            }

            if (!std::holds_alternative<MatType>(val.data)) throw std::runtime_error("Type Error: Type mismatch in matrix slice assignment.");
            MatType rhsMat = std::get<MatType>(val.data);
            if (rhsMat.getRows() != dstR || rhsMat.getCols() != dstC) throw std::runtime_error("Math Error: Matrix slice assignment size mismatch.");
            for (int i = 0; i < dstR; ++i) {
                for (int j = 0; j < dstC; ++j) m(rIds[i], cIds[j]) = rhsMat(i, j);
            }
            };

        if (std::holds_alternative<RealMatrix>(container.data)) {
            // 如果实数矩阵被注入了复数标量/阵列，自动进行升维扩容
            if (std::holds_alternative<Complex>(val.data) || std::holds_alternative<ComplexMatrix>(val.data)) {
                ComplexMatrix cm = std::get<RealMatrix>(container.data).toComplexMatrix();
                processMatrixWrite(cm);
                container = Value(cm);
                return;
            }
            processMatrixWrite(std::get<RealMatrix>(container.data));
            return;
        }
        if (std::holds_alternative<ComplexMatrix>(container.data)) { processMatrixWrite(std::get<ComplexMatrix>(container.data)); return; }
        if (std::holds_alternative<StringMatrix>(container.data)) { processMatrixWrite(std::get<StringMatrix>(container.data)); return; }

        throw std::runtime_error("Type Error: Cannot assign slice to this type.");
    }

    static Value compareValues(const Value& lhs, const Value& rhs, TokenType op) {
        if (op == TokenType::EQUAL || op == TokenType::BANG_EQUAL) {
            bool eq = valuesEqual(lhs, rhs);
            return Value((op == TokenType::EQUAL) == eq ? 1.0 : 0.0);
        }
        if (std::holds_alternative<RealMatrix>(lhs.data) || std::holds_alternative<ComplexMatrix>(lhs.data) ||
            std::holds_alternative<RealMatrix>(rhs.data) || std::holds_alternative<ComplexMatrix>(rhs.data))
            throw std::runtime_error("Type Error: Ordering comparison not defined for matrices.");
        if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data)) {
            const std::string& a = std::get<std::string>(lhs.data);
            const std::string& b = std::get<std::string>(rhs.data);
            bool r = false;
            switch (op) {
            case TokenType::LESS:          r = a < b; break;
            case TokenType::LESS_EQUAL:    r = a <= b; break;
            case TokenType::GREATER:       r = a > b; break;
            case TokenType::GREATER_EQUAL: r = a >= b; break;
            default: break;
            }
            return Value(r ? 1.0 : 0.0);
        }
        if (std::holds_alternative<std::string>(lhs.data) || std::holds_alternative<std::string>(rhs.data))
            throw std::runtime_error("Type Error: Cannot compare string with non-string type.");
        if (std::holds_alternative<Complex>(lhs.data) && !std::get<Complex>(lhs.data).isNumber())
            throw std::runtime_error("Type Error: Cannot order complex numbers with nonzero imaginary part.");
        if (std::holds_alternative<Complex>(rhs.data) && !std::get<Complex>(rhs.data).isNumber())
            throw std::runtime_error("Type Error: Cannot order complex numbers with nonzero imaginary part.");
        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<BigInt>(rhs.data)) {
            const BigInt& a = std::get<BigInt>(lhs.data); const BigInt& b = std::get<BigInt>(rhs.data);
            bool r = false;
            switch (op) {
            case TokenType::LESS: r = a < b; break; case TokenType::LESS_EQUAL: r = a <= b; break;
            case TokenType::GREATER: r = a > b; break; case TokenType::GREATER_EQUAL: r = a >= b; break;
            default: break;
            }
            return Value(r ? 1.0 : 0.0);
        }
        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<Fraction>(rhs.data)) {
            const Fraction& a = std::get<Fraction>(lhs.data); const Fraction& b = std::get<Fraction>(rhs.data);
            bool r = false;
            switch (op) {
            case TokenType::LESS: r = a < b; break; case TokenType::LESS_EQUAL: r = a <= b; break;
            case TokenType::GREATER: r = a > b; break; case TokenType::GREATER_EQUAL: r = a >= b; break;
            default: break;
            }
            return Value(r ? 1.0 : 0.0);
        }
        if ((std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data)) ||
            (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))) {
            Fraction a = std::holds_alternative<Fraction>(lhs.data) ? std::get<Fraction>(lhs.data) : Fraction(std::get<BigInt>(lhs.data));
            Fraction b = std::holds_alternative<Fraction>(rhs.data) ? std::get<Fraction>(rhs.data) : Fraction(std::get<BigInt>(rhs.data));
            bool r = false;
            switch (op) {
            case TokenType::LESS: r = a < b; break; case TokenType::LESS_EQUAL: r = a <= b; break;
            case TokenType::GREATER: r = a > b; break; case TokenType::GREATER_EQUAL: r = a >= b; break;
            default: break;
            }
            return Value(r ? 1.0 : 0.0);
        }
        double a = lhs.asDouble(), b = rhs.asDouble();
        bool r = false;
        switch (op) {
        case TokenType::LESS:          r = a < b && !Tol::isEq(a, b); break;
        case TokenType::LESS_EQUAL:    r = a < b || Tol::isEq(a, b); break;
        case TokenType::GREATER:       r = a > b && !Tol::isEq(a, b); break;
        case TokenType::GREATER_EQUAL: r = a > b || Tol::isEq(a, b); break;
        default: break;
        }
        return Value(r ? 1.0 : 0.0);
    }

    // =================================================================
    // 构造函数
    // =================================================================
    Evaluator::Evaluator() {

        // 统一注册器
        auto reg = [this](const std::string& name, std::set<int> arity, NativeCallable fn) {
            builtins[name] = std::move(fn);
            builtinArity[name] = std::move(arity);
            };

        // [0] 系统常量
        environment["PI"] = Value(3.14159265358979323846);
        environment["E"] = Value(2.71828182845904523536);
        environment["i"] = Value(Complex(0.0, 1.0));
        environment["I"] = Value(Complex(0.0, 1.0));
        environment["true"] = Value(1.0);
        environment["false"] = Value(0.0);

        auto matrixDispatch1 = [](const Value& arg, auto func) -> Value {
            if (std::holds_alternative<RealMatrix>(arg.data)) return Value(func(std::get<RealMatrix>(arg.data)));
            if (std::holds_alternative<ComplexMatrix>(arg.data)) return Value(func(std::get<ComplexMatrix>(arg.data)));
            throw std::runtime_error("Type Error: Expected a matrix.");
            };

        // =================================================================
        // [1] 基础数学函数
        // =================================================================

        reg("sin", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: sin() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSin());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSin());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(sin(std::get<Complex>(args[0].data)));
            return Value(std::sin(args[0].asDouble()));
            });

        reg("cos", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: cos() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCos());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCos());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(cos(std::get<Complex>(args[0].data)));
            return Value(std::cos(args[0].asDouble()));
            });

        reg("tan", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: tan() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTan());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTan());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(tan(std::get<Complex>(args[0].data)));
            return Value(std::tan(args[0].asDouble()));
            });

        reg("exp", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: exp() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matExp());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matExp());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(exp(std::get<Complex>(args[0].data)));
            return Value(std::exp(args[0].asDouble()));
            });

        reg("sinh", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: sinh() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSinh());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSinh());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(sinh(std::get<Complex>(args[0].data)));
            return Value(std::sinh(args[0].asDouble()));
            });

        reg("cosh", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: cosh() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCosh());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCosh());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(cosh(std::get<Complex>(args[0].data)));
            return Value(std::cosh(args[0].asDouble()));
            });

        reg("tanh", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: tanh() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTanh());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTanh());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(tanh(std::get<Complex>(args[0].data)));
            return Value(std::tanh(args[0].asDouble()));
            });

        reg("log", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() == 1) {
                if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(matLog(std::get<RealMatrix>(args[0].data)));
                if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(matLog(std::get<ComplexMatrix>(args[0].data)));
                if (std::holds_alternative<Complex>(args[0].data)) return Value(log(std::get<Complex>(args[0].data)));
                double x = args[0].asDouble();
                if (x == 0) throw std::runtime_error("Math Error: Logarithm of zero.");
                if (x < 0) return Value(log(Complex(x, 0.0)));
                return Value(std::log(x));
            }
            if (args.size() == 2) {
                Complex base = args[0].asComplex(), x = args[1].asComplex();
                return Value(log(x) / log(base));
            }
            throw std::runtime_error("Runtime Error: log() expects 1 or 2 arguments.");
            });

        reg("ln", { 1, 2 }, builtins["log"]);

        reg("sqrt", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: sqrt() expects 1 argument.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(matSqrt(std::get<RealMatrix>(args[0].data)));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(matSqrt(std::get<ComplexMatrix>(args[0].data)));
            if (std::holds_alternative<Complex>(args[0].data)) return Value(sqrt(std::get<Complex>(args[0].data)));
            double x = args[0].asDouble();
            if (x < 0) return Value(Complex(0, std::sqrt(-x)));
            return Value(std::sqrt(x));
            });

        reg("matpow", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: matpow(A, B) expects 2 arguments.");
            return Value(matPow(args[0].asComplexMatrix(), args[1].asComplexMatrix()));
            });

        reg("asin", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: asin() expects 1 argument.");
            if (std::holds_alternative<Complex>(args[0].data)) return Value(asin(std::get<Complex>(args[0].data)));
            double x = args[0].asDouble();
            if (x < -1.0 || x > 1.0) return Value(asin(Complex(x, 0.0)));
            return Value(std::asin(x));
            });

        reg("acos", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: acos() expects 1 argument.");
            if (std::holds_alternative<Complex>(args[0].data)) return Value(acos(std::get<Complex>(args[0].data)));
            double x = args[0].asDouble();
            if (x < -1.0 || x > 1.0) return Value(acos(Complex(x, 0.0)));
            return Value(std::acos(x));
            });

        reg("atan", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: atan() expects 1 argument.");
            if (std::holds_alternative<Complex>(args[0].data)) return Value(atan(std::get<Complex>(args[0].data)));
            return Value(std::atan(args[0].asDouble()));
            });

        reg("abs", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: abs() expects 1 argument.");
            if (std::holds_alternative<BigInt>(args[0].data)) return Value(std::get<BigInt>(args[0].data).abs());
            if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).modulus());
            if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).abs());
            return Value(std::abs(args[0].asDouble()));
            });

        reg("pow", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: pow() expects 2 arguments.");
            return args[0] ^ args[1];
            });

        // =================================================================
        // 通用取整分发器
        // =================================================================
        auto roundDispatch = [](const std::vector<Value>& args, const std::string& name,
            std::function<double(double)> baseFn) -> Value {
                if (args.size() < 1 || args.size() > 2)
                    throw std::runtime_error("Runtime Error: " + name + "() expects 1 or 2 arguments.");
                int n = 0;
                bool hasN = (args.size() == 2);
                if (hasN) n = static_cast<int>(std::round(args[1].asDouble()));
                double factor = std::pow(10.0, n);
                auto fn = [baseFn, factor](double x) -> double { return baseFn(x * factor) / factor; };
                if (std::holds_alternative<Complex>(args[0].data)) {
                    const Complex& c = std::get<Complex>(args[0].data);
                    Complex result(fn(c.real), fn(c.imag));
                    if (jc::Tol::isEq(result.imag, 0.0)) {
                        if (!hasN || n <= 0) return Value(BigInt(static_cast<int64_t>(result.real)));
                        return Value(result.real);
                    }
                    return Value(result);
                }
                if (std::holds_alternative<RealMatrix>(args[0].data)) {
                    const RealMatrix& m = std::get<RealMatrix>(args[0].data);
                    std::vector<double> flat;
                    flat.reserve(static_cast<size_t>(m.getRows()) * m.getCols());
                    for (int i = 0; i < m.getRows(); ++i)
                        for (int j = 0; j < m.getCols(); ++j)
                            flat.push_back(fn(m(i, j)));
                    return Value(RealMatrix(m.getRows(), m.getCols(), flat));
                }
                if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                    const ComplexMatrix& m = std::get<ComplexMatrix>(args[0].data);
                    std::vector<Complex> flat;
                    flat.reserve(static_cast<size_t>(m.getRows()) * m.getCols());
                    for (int i = 0; i < m.getRows(); ++i)
                        for (int j = 0; j < m.getCols(); ++j)
                            flat.push_back(Complex(fn(m(i, j).real), fn(m(i, j).imag)));
                    return Value(ComplexMatrix(m.getRows(), m.getCols(), flat));
                }
                double result = fn(args[0].asDouble());
                if (!hasN || n <= 0) return Value(BigInt(static_cast<int64_t>(result)));
                return Value(result);
            };

        reg("round", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "round", [](double x) { return std::round(x); }); });
        reg("floor", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "floor", [](double x) { return std::floor(x); }); });
        reg("ceil", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "ceil", [](double x) { return std::ceil(x); }); });
        reg("trunc", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "trunc", [](double x) { return std::trunc(x); }); });

        reg("sgn", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            double x = args[0].asDouble(); return Value(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0));
            });
        reg("deg", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            return Value(args[0].asDouble() / Complex::PI * 180.0);
            });
        reg("rad", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            return Value(args[0].asDouble() / 180.0 * Complex::PI);
            });
        reg("evalf", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            return Value(args[0].asDouble());
            });
        reg("idiv", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: idiv() expects 2 arguments.");
            if (args[0].isBigInt() && args[1].isBigInt()) {
                BigInt a = std::get<BigInt>(args[0].data), b = std::get<BigInt>(args[1].data);
                if (b.isZero()) throw std::runtime_error("Math Error: Division by zero.");
                return Value(a / b);
            }
            double a = args[0].asDouble(), b = args[1].asDouble();
            if (b == 0.0) throw std::runtime_error("Math Error: Division by zero.");
            return Value(std::trunc(a / b));
            });

        // =================================================================
        // [2] 复数专属属性
        // =================================================================
        reg("Re", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).real); return args[0]; });
        reg("Im", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).imag); return Value(0.0); });
        reg("arg", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).argument()); return Value(args[0].asDouble() >= 0 ? 0.0 : Complex::PI); });
        reg("conj", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).conjugate()); return args[0]; });

        // =================================================================
        // [3] 分数引擎
        // =================================================================
        reg("frac", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: frac() expects 2 arguments.");
            BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble())));
            BigInt d = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble())));
            return Value(Fraction(n, d));
            });
        reg("num", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).getNum()); return args[0]; });
        reg("den", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).getDen()); return Value(1.0); });

        // =================================================================
        // [4] 多项式方程求解
        // =================================================================
        reg("solve", { 2, 3, 4, 5 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() < 2 || args.size() > 5) throw std::runtime_error("Runtime Error: solve() expects 2 to 5 coefficients.");
            std::vector<Complex> roots;
            if (args.size() == 2) roots = Complex::solveDegreeOne(args[0].asComplex(), args[1].asComplex());
            else if (args.size() == 3) roots = Complex::solveDegreeTwo(args[0].asComplex(), args[1].asComplex(), args[2].asComplex());
            else if (args.size() == 4) roots = Complex::solveDegreeThree(args[0].asComplex(), args[1].asComplex(), args[2].asComplex(), args[3].asComplex());
            else if (args.size() == 5) roots = Complex::solveDegreeFour(args[0].asComplex(), args[1].asComplex(), args[2].asComplex(), args[3].asComplex(), args[4].asComplex());
            return Value(ComplexMatrix(static_cast<int>(roots.size()), 1, roots));
            });

        // =================================================================
        // [5] 矩阵基础运算
        // =================================================================
        reg("addS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: addS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v += c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v + c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: addS() requires a matrix.");
            });
        reg("subS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: subS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v -= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v - c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: subS() requires a matrix.");
            });
        reg("mulS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: mulS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v *= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v * c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: mulS() requires a matrix.");
            });
        reg("divS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: divS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Division by zero."); std::vector<double> flat = m.rawData(); for (auto& v : flat) v /= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); if (jc::Tol::isEq(c.modulus(), 0.0)) throw std::runtime_error("Math Error: Division by zero."); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v / c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: divS() requires a matrix.");
            });
        reg("powS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: powS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v = std::pow(v, c); return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v ^ c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: powS() requires a matrix.");
            });
        reg("modS", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: modS(A, c) expects 2 arguments.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Modulo by zero."); std::vector<double> flat = m.rawData(); for (auto& v : flat) { v = std::fmod(v, c); if (v < 0) v += std::abs(c); } return Value(RealMatrix(m.getRows(), m.getCols(), flat)); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Modulo by zero."); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) { double re = std::fmod(v.real, c); double im = std::fmod(v.imag, c); if (re < 0) re += std::abs(c); if (im < 0) im += std::abs(c); v = Complex(re, im); } return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); }
            throw std::runtime_error("Type Error: modS() requires a matrix.");
            });

        reg("det", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).determinant()); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).determinant()); throw std::runtime_error("Type Error: det() requires a matrix."); });
        reg("inv", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).inverse()); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).inverse()); throw std::runtime_error("Type Error: inv() requires a matrix."); });
        reg("trans", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).transpose());
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).transpose());
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).transpose());  // ★
            throw std::runtime_error("Type Error: trans() requires a matrix.");
            });        
        reg("gauss", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).gaussianElimination().first); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).gaussianElimination().first); throw std::runtime_error("Type Error: gauss() requires a matrix."); });

        reg("rank", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return static_cast<double>(m.rank()); }); });
        reg("tr", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.trace(); }); });
        reg("norm", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.norm(); }); });
        reg("cond", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.condition(); }); });
        reg("adj", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.adjugate(); }); });
        reg("perm", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.permanent(); }); });
        reg("sum", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.sum(); }); });
        reg("prod", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.product(); }); });
        reg("null", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.nullSpace(); }); });
        reg("orth", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.orthogonalize(); }); });
        reg("ctrans", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return matrixDispatch1(args[0], [](const auto& m) { return m.conjugateTranspose(); }); });

        reg("row", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            if (std::holds_alternative<RealMatrix>(args[0].data))
                return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getRows()));
            if (std::holds_alternative<ComplexMatrix>(args[0].data))
                return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getRows()));
            if (std::holds_alternative<StringMatrix>(args[0].data))                              // ★
                return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getRows()));// ★
            throw std::runtime_error("Type Error: row() requires a matrix.");
            });        
        reg("col", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            if (std::holds_alternative<RealMatrix>(args[0].data))
                return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getCols()));
            if (std::holds_alternative<ComplexMatrix>(args[0].data))
                return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getCols()));
            if (std::holds_alternative<StringMatrix>(args[0].data))                              // ★
                return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getCols()));// ★
            throw std::runtime_error("Type Error: col() requires a matrix.");
            });        
        reg("rows", { 1 }, builtins["row"]);
        reg("cols", { 1 }, builtins["col"]);

        reg("mpow", { 2 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: mpow() expects 2 arguments."); int n = static_cast<int>(std::round(args[1].asDouble())); return matrixDispatch1(args[0], [n](const auto& m) { return m.power(n); }); });

        reg("get", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: get() expects 3 arguments.");
            int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data)(r, c));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data)(r, c));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data)(r, c));  // ★
            throw std::runtime_error("Type Error: get() requires a matrix.");
            });        
        reg("set", { 4 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 4) throw std::runtime_error("Runtime Error: set() expects 4 arguments.");
            int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix res = std::get<RealMatrix>(args[0].data); res(r, c) = args[3].asDouble(); return Value(res); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix res = std::get<ComplexMatrix>(args[0].data); res(r, c) = args[3].asComplex(); return Value(res); }
            if (std::holds_alternative<StringMatrix>(args[0].data)) {  // ★
                StringMatrix res = std::get<StringMatrix>(args[0].data);
                if (std::holds_alternative<std::string>(args[3].data)) res(r, c) = std::get<std::string>(args[3].data);
                else { std::ostringstream oss; oss << args[3]; res(r, c) = oss.str(); }
                return Value(res);
            }
            throw std::runtime_error("Type Error: set() requires a matrix.");
            });

        reg("getR", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            int r = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).getRow(r));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).getRow(r));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).getRow(r));  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });        
        reg("getC", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            int c = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).getCol(c));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).getCol(c));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).getCol(c));  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });        
        reg("delR", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            int r = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).deleteRow(r));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).deleteRow(r));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).deleteRow(r));  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });        
        reg("delC", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            int c = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).deleteCol(c));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).deleteCol(c));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).deleteCol(c));  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });
        reg("swapR", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: swapR() expects 3 args.");
            int r1 = static_cast<int>(std::round(args[1].asDouble())), r2 = static_cast<int>(std::round(args[2].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); }
            if (std::holds_alternative<StringMatrix>(args[0].data)) { StringMatrix m = std::get<StringMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); }  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });        
        reg("swapC", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: swapC() expects 3 args.");
            int c1 = static_cast<int>(std::round(args[1].asDouble())), c2 = static_cast<int>(std::round(args[2].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); }
            if (std::holds_alternative<StringMatrix>(args[0].data)) { StringMatrix m = std::get<StringMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); }  // ★
            throw std::runtime_error("Type Error: requires a matrix.");
            });        
        reg("multiR", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: multiR() expects 3 args."); int r = static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.multiplyRow(r, args[2].asDouble()); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.multiplyRow(r, args[2].asComplex()); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
        reg("multiC", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: multiC() expects 3 args."); int c = static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double s = args[2].asDouble(); for (int r = 0; r < m.getRows(); ++r) m(r, c) = m(r, c) * s; return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex s = args[2].asComplex(); for (int r = 0; r < m.getRows(); ++r) m(r, c) = m(r, c) * s; return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
        reg("addR", { 4 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 4) throw std::runtime_error("Runtime Error: addR() expects 4 args."); int r1 = static_cast<int>(std::round(args[1].asDouble())), r2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.addRows(r1, r2, args[3].asDouble()); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.addRows(r1, r2, args[3].asComplex()); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
        reg("addC", { 4 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 4) throw std::runtime_error("Runtime Error: addC() expects 4 args."); int c1 = static_cast<int>(std::round(args[1].asDouble())), c2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double s = args[3].asDouble(); for (int r = 0; r < m.getRows(); ++r) m(r, c1) = m(r, c1) + s * m(r, c2); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex s = args[3].asComplex(); for (int r = 0; r < m.getRows(); ++r) m(r, c1) = m(r, c1) + s * m(r, c2); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });

        reg("reshape", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: reshape() expects 3 args.");
            int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).reshape(r, c));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).reshape(r, c));
            if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).reshape(r, c));  // ★
            throw std::runtime_error("Type Error: reshape() requires a matrix.");
            });        
        reg("sub", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: sub() expects 3 args."); int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).subMatrix(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).subMatrix(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("cof", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: cof() expects 3 args."); int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).cofactor(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).cofactor(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("Acof", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: Acof() expects 3 args."); int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).algebraicCofactor(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).algebraicCofactor(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });

        reg("integR", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data))
                return Value(std::get<RealMatrix>(args[0].data).integR(std::get<RealMatrix>(args[1].data)));
            if (std::holds_alternative<StringMatrix>(args[0].data) && std::holds_alternative<StringMatrix>(args[1].data))  // ★
                return Value(std::get<StringMatrix>(args[0].data).integR(std::get<StringMatrix>(args[1].data)));          // ★
            return Value(args[0].asComplexMatrix().integR(args[1].asComplexMatrix()));
            });        
        reg("integC", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args.");
            if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data))
                return Value(std::get<RealMatrix>(args[0].data).integC(std::get<RealMatrix>(args[1].data)));
            if (std::holds_alternative<StringMatrix>(args[0].data) && std::holds_alternative<StringMatrix>(args[1].data))  // ★
                return Value(std::get<StringMatrix>(args[0].data).integC(std::get<StringMatrix>(args[1].data)));          // ★
            return Value(args[0].asComplexMatrix().integC(args[1].asComplexMatrix()));
            });        
        reg("integD", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data)) return Value(std::get<RealMatrix>(args[0].data).integD(std::get<RealMatrix>(args[1].data))); return Value(args[0].asComplexMatrix().integD(args[1].asComplexMatrix())); });

        reg("id", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); int n = static_cast<int>(std::round(args[0].asDouble())); if (n < 1) throw std::runtime_error("Runtime Error: Size must be positive."); return Value(RealMatrix::identity(n)); });
        reg("ones", { 1, 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() == 1) { int n = static_cast<int>(std::round(args[0].asDouble())); return Value(RealMatrix::ones(n, n)); } if (args.size() == 2) { int r = static_cast<int>(std::round(args[0].asDouble())), c = static_cast<int>(std::round(args[1].asDouble())); return Value(RealMatrix::ones(r, c)); } throw std::runtime_error("Runtime Error: ones() expects 1 or 2 arguments."); });
        reg("zeros", { 1, 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() == 1) { int n = static_cast<int>(std::round(args[0].asDouble())); return Value(RealMatrix::zeros(n, n)); } if (args.size() == 2) { int r = static_cast<int>(std::round(args[0].asDouble())), c = static_cast<int>(std::round(args[1].asDouble())); return Value(RealMatrix::zeros(r, c)); } throw std::runtime_error("Runtime Error: zeros() expects 1 or 2 arguments."); });

        // =================================================================
        // [6] 矩阵分解与特征值
        // =================================================================
        reg("qr_Q", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).qrDecomposition().first); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).qrDecomposition().first); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("qr_R", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).qrDecomposition().second); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).qrDecomposition().second); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("lu_L", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().L); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().L); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("lu_U", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().U); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().U); throw std::runtime_error("Type Error: requires a matrix."); });
        reg("lu_P", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().P); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().P); throw std::runtime_error("Type Error: requires a matrix."); });

        reg("eig", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); std::vector<Complex> vals; if (std::holds_alternative<RealMatrix>(args[0].data)) vals = computeEigenvalues(std::get<RealMatrix>(args[0].data)); else if (std::holds_alternative<ComplexMatrix>(args[0].data)) vals = computeEigenvalues(std::get<ComplexMatrix>(args[0].data)); else throw std::runtime_error("Type Error: requires a matrix."); return Value(ComplexMatrix(static_cast<int>(vals.size()), 1, vals)); });
        reg("eigvec", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto vals = computeEigenvalues(A); return Value(computeEigenvectors(A, vals)); });
        reg("diag", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto [P, D] = diagonalize(A); return Value(D); });
        reg("diagP", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto [P, D] = diagonalize(A); return Value(P); });

        // =================================================================
        // [7] 线性方程组求解
        // =================================================================
        reg("lsolve", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: lsolve(A, b) expects 2 arguments.");
            ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix();
            if (A.getRows() != b.getRows()) throw std::runtime_error("Math Error: Row count mismatch.");
            if (b.getCols() != 1) throw std::runtime_error("Math Error: b must be Nx1.");
            int n = A.getCols();
            ComplexMatrix aug = A.integR(b);
            int rankA = A.rank(), rankAug = aug.rank();
            if (rankA != rankAug) { ComplexMatrix AH = A.conjugateTranspose(); ComplexMatrix nA = AH * A, nb = AH * b; ComplexMatrix a2 = nA.integR(nb); auto [r2, s2] = a2.gaussianElimination(); int n2 = nA.getCols(); std::vector<Complex> sol(n2); for (int ii = 0; ii < n2; ++ii) sol[ii] = r2(ii, n2); return Value(ComplexMatrix(n2, 1, sol)); }
            auto [rref, swaps] = aug.gaussianElimination();
            std::vector<int> pivotCols; for (int ii = 0; ii < A.getRows(); ++ii) for (int j = 0; j < n; ++j) { if (!ComplexMatrix::isEssentiallyZero(rref(ii, j))) { pivotCols.push_back(j); break; } }
            std::vector<Complex> particular(n, Complex(0, 0)); for (int p = 0; p < static_cast<int>(pivotCols.size()); ++p) particular[pivotCols[p]] = rref(p, n);
            return Value(ComplexMatrix(n, 1, particular));
            });

        reg("linfo", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: linfo(A, b) expects 2 arguments.");
            ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix();
            if (A.getRows() != b.getRows()) throw std::runtime_error("Math Error: Row count mismatch.");
            int m = A.getRows(), n = A.getCols(); ComplexMatrix aug = A.integR(b); int rA = A.rank(), rAug = aug.rank();
            std::cout << "Equations: " << m << "  Variables: " << n << "  rank(A): " << rA << "  rank([A|b]): " << rAug << std::endl;
            if (rA != rAug) std::cout << "Status: NO SOLUTION -> lsolve gives least squares." << std::endl;
            else if (rA == n) std::cout << "Status: UNIQUE SOLUTION" << std::endl;
            else std::cout << "Status: INFINITE SOLUTIONS  Free vars: " << (n - rA) << std::endl;
            return Value::none();
            });

        reg("lstsq", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: lstsq(A, b) expects 2 arguments."); ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); ComplexMatrix AH = A.conjugateTranspose(); ComplexMatrix aug = (AH * A).integR(AH * b); auto [rref, sw] = aug.gaussianElimination(); int n = (AH * A).getCols(); std::vector<Complex> sol(n); for (int ii = 0; ii < n; ++ii) sol[ii] = rref(ii, n); return Value(ComplexMatrix(n, 1, sol)); });
        reg("residual", { 3 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: residual(A, x, b) expects 3 arguments."); return Value(args[2].asComplexMatrix() - args[0].asComplexMatrix() * args[1].asComplexMatrix()); });

        // =================================================================
        // [8] 向量引擎
        // =================================================================
        auto assertVec = [](const Value& v, const std::string& f) { if (std::holds_alternative<RealMatrix>(v.data)) { if (std::get<RealMatrix>(v.data).getCols() != 1) throw std::runtime_error(f + "() expects Nx1 column vector."); } else if (std::holds_alternative<ComplexMatrix>(v.data)) { if (std::get<ComplexMatrix>(v.data).getCols() != 1) throw std::runtime_error(f + "() expects Nx1 column vector."); } else throw std::runtime_error(f + "() requires a matrix."); };

        reg("dim", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); assertVec(args[0], "dim"); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getRows())); return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getRows())); });
        reg("dot", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "dot"); assertVec(args[1], "dot"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); if (a.getRows() != b.getRows()) throw std::runtime_error("Math Error: Dimension mismatch."); return Value((a.conjugateTranspose() * b)(0, 0)); });
        reg("vnorm", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); assertVec(args[0], "vnorm"); ComplexMatrix v = args[0].asComplexMatrix(); return Value(std::sqrt((v.conjugateTranspose() * v)(0, 0).real)); });
        reg("normalize", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); assertVec(args[0], "normalize"); ComplexMatrix v = args[0].asComplexMatrix(); double len = std::sqrt((v.conjugateTranspose() * v)(0, 0).real); if (jc::Tol::isEq(len, 0.0)) throw std::runtime_error("Math Error: Cannot normalize a zero vector."); return Value(v / Complex(len)); });
        reg("cross", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "cross"); assertVec(args[1], "cross"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); if (a.getRows() != 3 || b.getRows() != 3) throw std::runtime_error("Math Error: Cross product is 3D only."); std::vector<Complex> r = { a(1,0) * b(2,0) - a(2,0) * b(1,0), a(2,0) * b(0,0) - a(0,0) * b(2,0), a(0,0) * b(1,0) - a(1,0) * b(0,0) }; return Value(ComplexMatrix(3, 1, r)); });
        reg("angle", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "angle"); assertVec(args[1], "angle"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double nA = std::sqrt((a.conjugateTranspose() * a)(0, 0).real), nB = std::sqrt((b.conjugateTranspose() * b)(0, 0).real); if (jc::Tol::isEq(nA, 0.0) || jc::Tol::isEq(nB, 0.0)) throw std::runtime_error("Math Error: Zero vector."); double ct = (a.conjugateTranspose() * b)(0, 0).real / (nA * nB); ct = std::max(-1.0, std::min(1.0, ct)); return Value(std::acos(ct)); });
        reg("sproj", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "sproj"); assertVec(args[1], "sproj"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double nB = std::sqrt((b.conjugateTranspose() * b)(0, 0).real); if (jc::Tol::isEq(nB, 0.0)) throw std::runtime_error("Math Error: Zero vector."); return Value((a.conjugateTranspose() * b)(0, 0).real / nB); });
        reg("vproj", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "vproj"); assertVec(args[1], "vproj"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); Complex dBB = (b.conjugateTranspose() * b)(0, 0); if (jc::Tol::isEq(dBB.modulus(), 0.0)) throw std::runtime_error("Math Error: Zero vector."); return Value(b * ((a.conjugateTranspose() * b)(0, 0) / dBB)); });
        reg("triple", { 3 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 3) throw std::runtime_error("Runtime Error: expects 3 args."); assertVec(args[0], "triple"); assertVec(args[1], "triple"); assertVec(args[2], "triple"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(), c = args[2].asComplexMatrix(); if (a.getRows() != 3 || b.getRows() != 3 || c.getRows() != 3) throw std::runtime_error("Math Error: 3D only."); std::vector<Complex> bc = { b(1,0) * c(2,0) - b(2,0) * c(1,0),b(2,0) * c(0,0) - b(0,0) * c(2,0),b(0,0) * c(1,0) - b(1,0) * c(0,0) }; return Value(a(0, 0) * bc[0] + a(1, 0) * bc[1] + a(2, 0) * bc[2]); });
        reg("isperp", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "isperp"); assertVec(args[1], "isperp"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double innerScale = a.norm() * b.norm(); return Value(Tol::clean((a.conjugateTranspose() * b)(0, 0).modulus(), innerScale) == 0.0 ? 1.0 : 0.0); });
        reg("isparallel", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); assertVec(args[0], "isparallel"); assertVec(args[1], "isparallel"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); return Value(a.integR(b).rank() <= 1 ? 1.0 : 0.0); });

        // =================================================================
        // [9] 大整数与数论
        // =================================================================
        reg("factorial", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return Value(BigInt::factorial(static_cast<int64_t>(std::round(args[0].asDouble())))); });
        reg("fib", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return Value(BigInt::fibonacci(static_cast<int64_t>(std::round(args[0].asDouble())))); });
        reg("gcd", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); BigInt a = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); BigInt b = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble()))); return Value(BigInt::gcd(a, b)); });
        reg("lcm", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); BigInt a = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); BigInt b = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble()))); return Value(BigInt::lcm(a, b)); });
        reg("digits", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); if (!args[0].isBigInt()) throw std::runtime_error("Type Error: expects BigInt."); return Value(static_cast<double>(std::get<BigInt>(args[0].data).digitCount())); });
        reg("isPrime", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(n.isPrime() ? 1.0 : 0.0); });
        reg("nextPrime", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(n.nextPrime()); });
        reg("nthPrime", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return Value(BigInt::nthPrime(static_cast<int64_t>(std::round(args[0].asDouble())))); });
        reg("primePi", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(BigInt(n.primePi())); });
        reg("factor", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); auto factors = n.factorize(); int rows_ = static_cast<int>(factors.size()); std::vector<double> flat; flat.reserve(static_cast<size_t>(rows_) * 2); for (const auto& f : factors) { flat.push_back(f.first.toDouble()); flat.push_back(static_cast<double>(f.second)); } return Value(RealMatrix(rows_, 2, flat)); });
        reg("phi", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(n.eulerPhi()); });
        reg("divisors", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(n.divisorCount()); });
        reg("sigma", { 1, 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() < 1 || args.size() > 2) throw std::runtime_error("Runtime Error: sigma() expects 1 or 2 args."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); int64_t k = (args.size() == 2) ? static_cast<int64_t>(std::round(args[1].asDouble())) : 1; return Value(n.divisorSum(k)); });
        reg("omega", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(BigInt(n.omega())); });
        reg("bigOmega", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(BigInt(n.bigOmega())); });
        reg("mobius", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(BigInt(n.mobius())); });
        reg("isPerfect", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble()))); return Value(n.isPerfect() ? 1.0 : 0.0); });
        reg("mod", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: mod(a, b) expects 2 arguments.");
            if (args[0].isBigInt() && args[1].isBigInt()) return Value(BigInt::mathMod(std::get<BigInt>(args[0].data), std::get<BigInt>(args[1].data)));
            if (std::holds_alternative<Fraction>(args[0].data)) { const auto& f = std::get<Fraction>(args[0].data); if (f.getDen() == BigInt(1)) { BigInt b = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble()))); return Value(BigInt::mathMod(f.getNum(), b)); } }
            double a = args[0].asDouble(), b = args[1].asDouble();
            if (jc::Tol::isEq(b, 0.0)) throw std::runtime_error("Math Error: Modulo by zero.");
            double r = std::fmod(a, b); if (r < 0) r += std::abs(b); return Value(r);
            });
        reg("modpow", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: modpow(base, exp, mod) expects 3 arguments.");
            BigInt base = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble())));
            BigInt exp = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble())));
            BigInt mod = args[2].isBigInt() ? std::get<BigInt>(args[2].data) : BigInt(static_cast<int64_t>(std::round(args[2].asDouble())));
            return Value(BigInt::modPow(base, exp, mod));
            });
        reg("C", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: C(n, k) expects 2 arguments.");
            int64_t n = static_cast<int64_t>(std::round(args[0].asDouble()));
            int64_t k = static_cast<int64_t>(std::round(args[1].asDouble()));
            if (n < 0 || k < 0) throw std::runtime_error("Math Error: C(n, k) requires non-negative integers.");
            if (k > n) return Value(BigInt(0));
            if (k > n - k) k = n - k; // C(n,k) = C(n, n-k) 优化
            // 逐步乘除避免中间结果爆炸：C(n,k) = n/1 * (n-1)/2 * ... * (n-k+1)/k
            BigInt result(1);
            for (int64_t i = 0; i < k; ++i) {
                result = result * BigInt(n - i);
                result = result / BigInt(i + 1);
            }
            return Value(result);
            });

        reg("A", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: A(n, k) expects 2 arguments.");
            int64_t n = static_cast<int64_t>(std::round(args[0].asDouble()));
            int64_t k = static_cast<int64_t>(std::round(args[1].asDouble()));
            if (n < 0 || k < 0) throw std::runtime_error("Math Error: A(n, k) requires non-negative integers.");
            if (k > n) return Value(BigInt(0));
            // A(n,k) = n * (n-1) * ... * (n-k+1)
            BigInt result(1);
            for (int64_t i = 0; i < k; ++i) {
                result = result * BigInt(n - i);
            }
            return Value(result);
            });

        reg("catalan", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: catalan(n) expects 1 argument.");
            int64_t n = static_cast<int64_t>(std::round(args[0].asDouble()));
            if (n < 0) throw std::runtime_error("Math Error: catalan(n) requires non-negative integer.");
            // C_n = C(2n, n) / (n+1)
            BigInt result(1);
            for (int64_t i = 0; i < n; ++i) {
                result = result * BigInt(2 * n - i);
                result = result / BigInt(i + 1);
            }
            result = result / BigInt(n + 1);
            return Value(result);
            });

        // =================================================================
        // [10] 多进制与位运算
        // =================================================================
        reg("base", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: base(val, radix) expects 2 args."); return Value(BaseNum(args[0].asBigInt(), static_cast<int>(std::round(args[1].asDouble())))); });
        reg("bnum", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: bnum(str, radix) expects 2 args."); if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: First arg must be a string."); return Value(BaseNum::fromString(std::get<std::string>(args[0].data), static_cast<int>(std::round(args[1].asDouble())))); });
        reg("changeBase", { 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: changeBase(val, r) expects 2 args."); return Value(BaseNum(args[0].asBigInt(), static_cast<int>(std::round(args[1].asDouble())))); });
        reg("data", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return Value(args[0].asBigInt()); });

        auto extractBin = [](const Value& v) -> BaseNum { if (std::holds_alternative<BaseNum>(v.data) && std::get<BaseNum>(v.data).getRadix() == 2) return std::get<BaseNum>(v.data); return BaseNum(v.asBigInt(), 2); };
        reg("bitand", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); return Value(extractBin(args[0]).bitAnd(extractBin(args[1]))); });
        reg("bitor", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); return Value(extractBin(args[0]).bitOr(extractBin(args[1]))); });
        reg("bitxor", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); return Value(extractBin(args[0]).bitXor(extractBin(args[1]))); });
        reg("bitnot", { 1, 2 }, [extractBin](const std::vector<Value>& args) -> Value {
            if (args.size() == 1) return Value(extractBin(args[0]).bitNot());
            if (args.size() == 2) { int width = static_cast<int>(std::round(args[1].asDouble())); if (width <= 0) throw std::runtime_error("Math Error: Bit width must be positive."); return Value(extractBin(args[0]).bitNot(width)); }
            throw std::runtime_error("Runtime Error: bitnot() expects 1 or 2 args.");
            });
        reg("bitshift", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); int shift = static_cast<int>(std::round(args[1].asDouble())); return shift > 0 ? Value(extractBin(args[0]).shiftLeft(shift)) : Value(extractBin(args[0]).shiftRight(-shift)); });

        // =================================================================
        // [11] 微积分与函数引擎
        // =================================================================


        auto evalFunc = [this](std::shared_ptr<FunctionClosure> closure, double x) -> double {
            // ★ Native 函数快速路径
            if (closure->isNative()) {
                auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
                return fn({ Value(x) }).asDouble();
            }

            if (!closure->acceptsArgCount(1))
                throw std::runtime_error("Math Error: Calculus functions require a function that accepts 1 argument.");
            if (closure->hasRef())
                throw std::runtime_error("Math Error: Calculus functions do not accept 'ref' parameters.");

            pushScope();
            scopeStack.back().globalNames.insert(closure->paramNames[0]);
            auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);

            functionDepth++;

            std::vector<Value> evalArgs = { Value(x) };
            for (size_t _i = evalArgs.size(); _i < closure->paramNames.size(); ++_i) {
                if (_i < closure->defaultValues.size() && !closure->defaultValues[_i].isNone())
                    evalArgs.push_back(closure->defaultValues[_i]);
            }
            EnvGuard guard(environment, closure->paramNames, evalArgs);
            try {
                double result;
                try { result = evaluate(closure->body.get()).asDouble(); }
                catch (ReturnSignal& sig) { result = sig.value.asDouble(); }
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                return result;
            }
            catch (...) {
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                throw;
            }
            };

        reg("diff", { 2 }, [evalFunc](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: diff(f, x) expects 2 args.");
            auto cl = args[0].asFunction(); double x = args[1].asDouble(); double h = 1e-4;
            double d = (-evalFunc(cl, x + 2 * h) + 8 * evalFunc(cl, x + h) - 8 * evalFunc(cl, x - h) + evalFunc(cl, x - 2 * h)) / (12 * h);
            return Value(d);
            });

        reg("integ", { 3, 4 }, [evalFunc](const std::vector<Value>& args) -> Value {
            if (args.size() < 3 || args.size() > 4) throw std::runtime_error("Runtime Error: integ(f, a, b [, n]) expects 3 or 4 args.");
            auto cl = args[0].asFunction(); double a = args[1].asDouble(), b = args[2].asDouble();
            int n = (args.size() == 4) ? static_cast<int>(std::round(args[3].asDouble())) : 100000;
            if (n <= 0 || n % 2 != 0) n = 100000;
            double h = (b - a) / n, s = evalFunc(cl, a) + evalFunc(cl, b);
            for (int ii = 1; ii < n; ii += 2) s += 4 * evalFunc(cl, a + ii * h);
            for (int ii = 2; ii < n - 1; ii += 2) s += 2 * evalFunc(cl, a + ii * h);
            return Value(s * h / 3.0);
            });

        reg("solveE", { 2 }, [evalFunc](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: solveE(f, x0) expects 2 args.");
            auto cl = args[0].asFunction(); double x = args[1].asDouble(); double h = 1e-5;
            for (int ii = 0; ii < 1000; ++ii) {
                double y = evalFunc(cl, x);
                if (Tol::clean(y, std::max(1.0, std::abs(x)), 1e7) == 0.0) return Value(x);
                double df = (evalFunc(cl, x + h) - evalFunc(cl, x - h)) / (2 * h);
                if (Tol::isEq(df, 0.0)) x += 1e-4; else x -= y / df;
            }
            throw std::runtime_error("Math Error: Equation solver did not converge.");
            });

        reg("table", {}, [this](const std::vector<Value>& args) -> Value {
            if (args.size() < 2) throw std::runtime_error("Runtime Error: table() expects at least 2 arguments.");
            auto cl = args[0].asFunction();
            int k = static_cast<int>(cl->paramNames.size());
            auto evalRow = [&](const std::vector<Value>& rowArgs, std::vector<double>& res_d, std::vector<Complex>& res_c, bool& hasComplex) {
                Value y_val = callFunction(cl, rowArgs);  // ★ 替换
                if (!hasComplex) {
                    try { res_d.push_back(y_val.asDouble()); }
                    catch (...) { hasComplex = true; for (double d : res_d) res_c.push_back(Complex(d)); res_c.push_back(y_val.asComplex()); }
                }
                else { res_c.push_back(y_val.asComplex()); }
                };
            if (args.size() == 4 && k == 1 && !std::holds_alternative<RealMatrix>(args[1].data) && !std::holds_alternative<ComplexMatrix>(args[1].data)) {
                double start = args[1].asDouble(), step = args[2].asDouble(); int count = static_cast<int>(std::round(args[3].asDouble()));
                if (count <= 0) throw std::runtime_error("Math Error: count must be positive.");
                std::vector<double> res_d; std::vector<Complex> res_c; bool hasComplex = false;
                for (int ii = 0; ii < count; ++ii) evalRow({ Value(start + ii * step) }, res_d, res_c, hasComplex);
                if (hasComplex) return Value(ComplexMatrix(count, 1, res_c)); return Value(RealMatrix(count, 1, res_d));
            }
            if (args.size() == 2) {
                int N = 0; std::vector<double> res_d; std::vector<Complex> res_c; bool hasComplex = false;
                if (std::holds_alternative<RealMatrix>(args[1].data)) { RealMatrix M = std::get<RealMatrix>(args[1].data); if (M.getCols() != k) throw std::runtime_error("Math Error: Matrix columns must match function parameter count."); N = M.getRows(); for (int ii = 0; ii < N; ++ii) { std::vector<Value> rowArgs; for (int j = 0; j < k; ++j) rowArgs.push_back(Value(M(ii, j))); evalRow(rowArgs, res_d, res_c, hasComplex); } }
                else if (std::holds_alternative<ComplexMatrix>(args[1].data)) { ComplexMatrix M = std::get<ComplexMatrix>(args[1].data); if (M.getCols() != k) throw std::runtime_error("Math Error: Matrix columns must match function parameter count."); N = M.getRows(); for (int ii = 0; ii < N; ++ii) { std::vector<Value> rowArgs; for (int j = 0; j < k; ++j) rowArgs.push_back(Value(M(ii, j))); evalRow(rowArgs, res_d, res_c, hasComplex); } }
                else throw std::runtime_error("Type Error: Expected a matrix for 2-argument multivariate table().");
                if (N == 0) return Value(RealMatrix(0, 0)); if (hasComplex) return Value(ComplexMatrix(N, 1, res_c)); return Value(RealMatrix(N, 1, res_d));
            }
            if (args.size() == static_cast<size_t>(k + 1)) {
                int N = -1;
                for (int ii = 1; ii <= k; ++ii) {
                    if (std::holds_alternative<RealMatrix>(args[ii].data)) { if (std::get<RealMatrix>(args[ii].data).getCols() != 1) throw std::runtime_error("Math Error: Arguments must be column vectors."); if (N == -1) N = std::get<RealMatrix>(args[ii].data).getRows(); else if (N != std::get<RealMatrix>(args[ii].data).getRows()) throw std::runtime_error("Math Error: Vectors must have same length."); }
                    else if (std::holds_alternative<ComplexMatrix>(args[ii].data)) { if (std::get<ComplexMatrix>(args[ii].data).getCols() != 1) throw std::runtime_error("Math Error: Arguments must be column vectors."); if (N == -1) N = std::get<ComplexMatrix>(args[ii].data).getRows(); else if (N != std::get<ComplexMatrix>(args[ii].data).getRows()) throw std::runtime_error("Math Error: Vectors must have same length."); }
                    else throw std::runtime_error("Type Error: Expected column vectors for multivariate table().");
                }
                if (N <= 0) return Value(RealMatrix(0, 0));
                std::vector<double> res_d; std::vector<Complex> res_c; bool hasComplex = false;
                for (int r = 0; r < N; ++r) { std::vector<Value> rowArgs; for (int c = 1; c <= k; ++c) { if (std::holds_alternative<RealMatrix>(args[c].data)) rowArgs.push_back(Value(std::get<RealMatrix>(args[c].data)(r, 0))); else rowArgs.push_back(Value(std::get<ComplexMatrix>(args[c].data)(r, 0))); } evalRow(rowArgs, res_d, res_c, hasComplex); }
                if (hasComplex) return Value(ComplexMatrix(N, 1, res_c)); return Value(RealMatrix(N, 1, res_d));
            }
            throw std::runtime_error("Runtime Error: Argument count mismatch. Use table(f, M) or table(f, v1, ..., vk).");
            });

        // =================================================================
        // [12] 统计与回归
        // =================================================================
        auto extractDS = [](const Value& v, const std::string& f) -> std::vector<double> { if (std::holds_alternative<RealMatrix>(v.data)) return std::get<RealMatrix>(v.data).rawData(); if (std::holds_alternative<ComplexMatrix>(v.data)) { const auto& cd = std::get<ComplexMatrix>(v.data).rawData(); std::vector<double> r(cd.size()); for (size_t ii = 0; ii < cd.size(); ++ii) { if (std::abs(cd[ii].imag) > 1e-15) throw std::runtime_error(f + "() requires real data."); r[ii] = cd[ii].real; } return r; } throw std::runtime_error(f + "() requires a matrix/vector."); };

        reg("mean", { 1 }, [extractDS](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); auto d = extractDS(args[0], "mean"); double s = 0; for (double v : d) s += v; return Value(s / d.size()); });
        reg("var", { 1 }, [extractDS](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); auto d = extractDS(args[0], "var"); double s = 0, sq = 0; for (double v : d) { s += v; sq += v * v; } double m = s / d.size(); return Value(sq / d.size() - m * m); });
        reg("svar", { 1 }, [extractDS, this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: svar() expects 1 argument.");
            auto d = extractDS(args[0], "svar");
            if (d.size() < 2) throw std::runtime_error("Math Error: Sample variance requires at least 2 data points.");
            double v = builtins["var"](args).asDouble();
            return Value(v * static_cast<double>(d.size()) / static_cast<double>(d.size() - 1));
            });
        reg("std", { 1 }, [this](const std::vector<Value>& args) -> Value { return Value(std::sqrt(builtins["var"](args).asDouble())); });
        reg("sstd", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: sstd() expects 1 argument.");
            return Value(std::sqrt(builtins["svar"](args).asDouble()));
            });
        reg("max", { 1 }, [extractDS](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); auto d = extractDS(args[0], "max"); double mx = d[0]; for (double v : d) if (v > mx) mx = v; return Value(mx); });
        reg("min", { 1 }, [extractDS](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); auto d = extractDS(args[0], "min"); double mn = d[0]; for (double v : d) if (v < mn) mn = v; return Value(mn); });
        reg("span", { 1 }, [this](const std::vector<Value>& args) -> Value { return Value(builtins["max"](args).asDouble() - builtins["min"](args).asDouble()); });
        // 辅助：从 any 取 Value
        auto anyToVal = [](const std::any& a) -> Value {
            return std::any_cast<Value>(a);
            };
        auto valToAny = [](const Value& v) -> std::any {
            return std::make_any<Value>(v);
            }; 
        reg("sort", { 1, 2 }, [this, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            // sort(array) — 数值排序（原有）
            if (args.size() == 1 && std::holds_alternative<RealMatrix>(args[0].data)) {
                auto f = std::get<RealMatrix>(args[0].data).rawData();
                std::sort(f.begin(), f.end());
                int n = static_cast<int>(f.size());
                return Value(RealMatrix(1, n, f));
            }
            // ★ sort(list) — 按 str() 字典序
            if (args.size() == 1 && std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                std::sort(L.raw().begin(), L.raw().end(), [&](const std::any& a, const std::any& b) {
                    std::ostringstream oa, ob;
                    oa << anyToVal(a); ob << anyToVal(b);
                    return oa.str() < ob.str();
                    });
                return Value(L);
            }
            // ★ sort(list/array, comparator)
            if (args.size() == 2) {
                auto cmp = args[1].asFunction();
                if (!cmp->acceptsArgCount(2))
                    throw std::runtime_error("Runtime Error: sort() comparator must be a 2-parameter function.");

                if (std::holds_alternative<List>(args[0].data)) {
                    List L = std::get<List>(args[0].data);
                    std::sort(L.raw().begin(), L.raw().end(), [&](const std::any& a, const std::any& b) {
                        Value va = anyToVal(a), vb = anyToVal(b);
                        Value result = callFunction(cmp, { va, vb });  // ★
                        return isTruthy(result);
                        });
                    return Value(L);
                }
                if (std::holds_alternative<RealMatrix>(args[0].data)) {
                    auto f = std::get<RealMatrix>(args[0].data).rawData();
                    std::sort(f.begin(), f.end(), [&](double a, double b) {
                        Value result = callFunction(cmp, { Value(a), Value(b) });  // ★
                        return isTruthy(result);
                        });
                    int n = static_cast<int>(f.size());
                    return Value(RealMatrix(1, n, f));
                }
                throw std::runtime_error("Type Error: sort() expects an array or list.");
            }
            if (args.size() == 1) throw std::runtime_error("Type Error: sort() expects a real array or list.");
            throw std::runtime_error("Runtime Error: sort() expects 1 or 2 arguments.");
            });
        reg("perc", { 2 }, [extractDS](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: perc(X, p) expects 2 args.");
            auto d = extractDS(args[0], "perc");
            if (d.empty()) throw std::runtime_error("Math Error: Cannot compute percentile of empty dataset.");
            double p = args[1].asDouble();
            if (p < 0 || p > 100) throw std::runtime_error("Math Error: Percentile must be [0,100].");
            std::sort(d.begin(), d.end());
            int n = static_cast<int>(d.size());
            double pos = (p / 100.0) * n;
            int kk = static_cast<int>(std::ceil(pos));
            if (kk <= 0) return Value(d[0]);
            if (kk >= n) return Value(d[n - 1]);
            int m = kk - 1;
            double f = pos - kk;
            if (std::abs(f) > 1e-9) return Value(d[m]);
            if (kk < n) return Value((d[m] + d[kk]) / 2.0);
            return Value(d[m]);
            });
        reg("median", { 1 }, [this](const std::vector<Value>& args) -> Value { return builtins["perc"]({ args[0], Value(50.0) }); });
        reg("mode", { 1 }, [extractDS](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg.");
            auto d = extractDS(args[0], "mode");
            if (d.empty()) throw std::runtime_error("Math Error: Cannot compute mode of empty dataset.");
            struct Bucket { double representative; int count; };
            std::vector<Bucket> buckets;
            for (double v : d) { bool found = false; for (auto& bkt : buckets) { if (jc::Tol::isEq(v, bkt.representative, 1e4)) { bkt.count++; found = true; break; } } if (!found) buckets.push_back({ v, 1 }); }
            int mx = 0; for (const auto& bkt : buckets) if (bkt.count > mx) mx = bkt.count;
            std::vector<double> modes; for (const auto& bkt : buckets) if (bkt.count == mx) modes.push_back(bkt.representative);
            std::sort(modes.begin(), modes.end());
            if (modes.size() == 1) return Value(modes[0]);
            return Value(RealMatrix(1, static_cast<int>(modes.size()), modes));
            });
        reg("cov", { 2 }, [extractDS, this](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); auto X = extractDS(args[0], "cov"), Y = extractDS(args[1], "cov"); if (X.size() != Y.size()) throw std::runtime_error("Math Error: Size mismatch."); double mX = builtins["mean"]({ args[0] }).asDouble(), mY = builtins["mean"]({ args[1] }).asDouble(); double c = 0; for (size_t ii = 0; ii < X.size(); ++ii) c += (X[ii] - mX) * (Y[ii] - mY); return Value(c / X.size()); });
        reg("corr", { 2 }, [this](const std::vector<Value>& args) -> Value { double c = builtins["cov"](args).asDouble(), sx = builtins["std"]({ args[0] }).asDouble(), sy = builtins["std"]({ args[1] }).asDouble(); return Value(c / (sx * sy)); });
        reg("rsq", { 2 }, [this](const std::vector<Value>& args) -> Value { double r = builtins["corr"](args).asDouble(); return Value(r * r); });
        reg("regress", { 2 }, [extractDS, this](const std::vector<Value>& args) -> Value { if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); double vX = builtins["var"]({ args[0] }).asDouble(); if (jc::Tol::isEq(vX, 0.0)) throw std::runtime_error("Math Error: Zero variance in X."); double c = builtins["cov"](args).asDouble(); double b = c / vX, a = builtins["mean"]({ args[1] }).asDouble() - b * builtins["mean"]({ args[0] }).asDouble(); std::cout << "Linear Model: Y = " << a << " + " << b << " * X" << std::endl; std::cout << "Correlation r: " << builtins["corr"](args).asDouble() << std::endl; return Value(RealMatrix(1, 2, { a, b })); });

        // =================================================================
        // [13] 随机数与幻方
        // =================================================================
        reg("rand", { 0, 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); if (args.size() == 0) return Value(std::uniform_real_distribution<double>(0, 1)(gen)); if (args.size() == 2) { double lo = args[0].asDouble(), hi = args[1].asDouble(); return Value(std::uniform_real_distribution<double>(lo, hi)(gen)); } throw std::runtime_error("Runtime Error: rand() expects 0 or 2 arguments."); });
        reg("randint", { 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); if (args.size() != 2) throw std::runtime_error("Runtime Error: expects 2 args."); int lo = static_cast<int>(std::round(args[0].asDouble())), hi = static_cast<int>(std::round(args[1].asDouble())); return Value(static_cast<double>(std::uniform_int_distribution<int>(lo, hi)(gen))); });
        reg("randc", { 0, 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); double lo = 0, hi = 1; if (args.size() == 2) { lo = args[0].asDouble(); hi = args[1].asDouble(); } else if (args.size() != 0) throw std::runtime_error("Runtime Error: randc() expects 0 or 2 args."); std::uniform_real_distribution<double> dist(lo, hi); return Value(Complex(dist(gen), dist(gen))); });
        reg("randmat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r, c; double lo = 0, hi = 1; if (args.size() == 2) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); } else if (args.size() == 4) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); lo = args[2].asDouble(); hi = args[3].asDouble(); } else throw std::runtime_error("Runtime Error: randmat() expects 2 or 4 args."); std::uniform_real_distribution<double> dist(lo, hi); std::vector<double> d(r * c); for (auto& v : d) v = dist(gen); return Value(RealMatrix(r, c, d)); });
        reg("randimat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r, c, lo = 0, hi = 10; if (args.size() == 2) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); } else if (args.size() == 4) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); lo = static_cast<int>(std::round(args[2].asDouble())); hi = static_cast<int>(std::round(args[3].asDouble())); } else throw std::runtime_error("Runtime Error: randimat() expects 2 or 4 args."); std::uniform_int_distribution<int> dist(lo, hi); std::vector<double> d(r * c); for (auto& v : d) v = static_cast<double>(dist(gen)); return Value(RealMatrix(r, c, d)); });
        reg("randcmat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r, c; double lo = 0, hi = 1; if (args.size() == 2) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); } else if (args.size() == 4) { r = static_cast<int>(std::round(args[0].asDouble())); c = static_cast<int>(std::round(args[1].asDouble())); lo = args[2].asDouble(); hi = args[3].asDouble(); } else throw std::runtime_error("Runtime Error: randcmat() expects 2 or 4 args."); std::uniform_real_distribution<double> dist(lo, hi); std::vector<Complex> d(r * c); for (auto& v : d) v = Complex(dist(gen), dist(gen)); return Value(ComplexMatrix(r, c, d)); });
        reg("magic", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1) throw std::runtime_error("Runtime Error: expects 1 arg."); return Value(RealMatrix::magic(static_cast<int>(std::round(args[0].asDouble())))); });

        // =================================================================
        // [14] 系统与硬盘引擎
        // =================================================================
        reg("buildIndex", { 0 }, [](const std::vector<Value>& args) -> Value { if (args.size() > 0) throw std::runtime_error("Runtime Error: expects 0 args."); BigInt::buildFileIndex(); return Value::none(); });
        reg("loadPrimes", { 0 }, builtins["buildIndex"]);
        reg("mountPrimes", { 1 }, [](const std::vector<Value>& args) -> Value { if (args.size() != 1 || !std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Runtime Error: mountPrimes(\"path\") expects a string argument."); BigInt::setPrimeFilePath(std::get<std::string>(args[0].data)); return Value::none(); });
        reg("sysinfo", { 0 }, [](const std::vector<Value>& args) -> Value { if (args.size() > 0) throw std::runtime_error("Runtime Error: expects 0 args."); std::cout << "--- Junk Calculator System Info ---\n" << "Prime DB: " << BigInt::getPrimeFilePath() << "\n" << "Indexed:  " << BigInt::totalPrimesInFile << " primes\n"; if (BigInt::totalPrimesInFile > 0) std::cout << "Max:      " << BigInt::largestPrimeInFile << "\n"; std::cout << "-----------------------------------" << std::endl; return Value::none(); });
        reg("resetConst", { 0 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() > 0) throw std::runtime_error("Runtime Error: resetConst() expects 0 arguments.");
            environment["PI"] = Value(3.14159265358979323846);
            environment["E"] = Value(2.71828182845904523536);
            environment["i"] = Value(Complex(0.0, 1.0));
            environment["I"] = Value(Complex(0.0, 1.0));
            environment["true"] = Value(1.0);
            environment["false"] = Value(0.0);
            std::cout << "System constants restored: PI, E, i, I, true, false" << std::endl;
            return Value::none();
            });
        reg("run", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1 || !std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Runtime Error: run(\"path\") expects a string argument.");
            std::string filepath = std::get<std::string>(args[0].data);

            namespace fs = std::filesystem;

            // ★ 搜索顺序：脚本目录 → +.jc2 → workspace → workspace+.jc2 → 全局 data/
            std::string baseDir = cwd();
            std::string wsDir = getWorkspacePath();
            std::string resolved;

            if (fs::path(filepath).is_absolute() && fs::exists(filepath)) {
                resolved = filepath;
            }
            else {
                std::string withExt = filepath + ".jc2";
                std::string exDir = getExeDir();
                std::vector<std::string> candidates = {
                    // 1. 脚本所在目录
                    (fs::path(baseDir) / filepath).string(),
                    (fs::path(baseDir) / withExt).string(),
                    (fs::path(baseDir) / "data" / filepath).string(),
                    (fs::path(baseDir) / "data" / withExt).string(),
                    // 2. 工作区目录
                    (fs::path(wsDir) / filepath).string(),
                    (fs::path(wsDir) / withExt).string(),
                    // 3. exe 所在目录（全局兜底）
                    (fs::path(exDir) / filepath).string(),
                    (fs::path(exDir) / withExt).string(),
                    (fs::path(exDir) / "data" / filepath).string(),
                    (fs::path(exDir) / "data" / withExt).string(),
                    (fs::path(exDir) / "lib" / filepath).string(),
                    (fs::path(exDir) / "lib" / withExt).string(),
                };
                for (const auto& c : candidates) {
                    if (fs::exists(c)) { resolved = c; break; }
                }
            }

            if (resolved.empty())
                throw std::runtime_error("IO Error: Cannot open script '" + filepath + "'.");

            std::ifstream file(resolved);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open script '" + filepath + "'.");

            // ★ 压入脚本所在目录
            std::string runDir = fs::weakly_canonical(resolved).parent_path().string();
            pushScriptDir(runDir);

            std::string rawLine;
            Value lastResult = Value::none();

            while (std::getline(file, rawLine)) {
                size_t s = rawLine.find_first_not_of(" \t");
                size_t e = rawLine.find_last_not_of(" \t\r\n");
                if (s == std::string::npos) continue;
                std::string line = rawLine.substr(s, e - s + 1);
                if (line.empty()) continue;
                if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

                // 剥离行尾注释
                {
                    size_t commentPos = line.find("//");
                    if (commentPos != std::string::npos)
                        line = line.substr(0, commentPos);
                    size_t ss = line.find_first_not_of(" \t");
                    if (ss == std::string::npos) continue;
                    line = line.substr(ss);
                    size_t ee = line.find_last_not_of(" \t");
                    line = line.substr(0, ee + 1);
                    if (line.empty()) continue;
                }

                // 多行续读
                {
                    int braces = 0, parens = 0, brackets = 0;
                    for (char c : line) {
                        if (c == '{') braces++; else if (c == '}') braces--;
                        if (c == '(') parens++; else if (c == ')') parens--;
                        if (c == '[') brackets++; else if (c == ']') brackets--;
                    }
                    while ((braces > 0 || parens > 0 || brackets > 0 || endsWithContinuation(line)) && std::getline(file, rawLine)) {
                        size_t commentPos = rawLine.find("//");
                        std::string stripped = (commentPos != std::string::npos) ? rawLine.substr(0, commentPos) : rawLine;
                        line += " " + stripped;
                        for (char c : stripped) {
                            if (c == '{') braces++; else if (c == '}') braces--;
                            if (c == '(') parens++; else if (c == ')') parens--;
                            if (c == '[') brackets++; else if (c == ']') brackets--;
                        }
                    }
                }

                jc::Lexer lexer(line);
                auto tokens = lexer.tokenize();
                jc::Parser parser(tokens);
                auto ast = parser.parse();
                lastResult = evaluate(ast.get());
                if (!lastResult.isNone())
                    environment["ANS"] = lastResult;
            }
            file.close();

            // ★ 弹出目录
            popScriptDir();

            return lastResult;
            });

        reg("setWorkspace", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: setWorkspace() expects a string path.");
            std::string p = std::get<std::string>(args[0].data);
            if (p == "default") {
                workspacePath = "";
                std::cout << "[System] Workspace reset to: " << getWorkspacePath() << std::endl;
            }
            else {
                setWorkspacePath(p);
                std::cout << "[System] Workspace set to: " << getWorkspacePath() << std::endl;
            }
            return Value::none();
            });

        reg("getWorkspace", { 0 }, [this](const std::vector<Value>& args) -> Value {
            (void)args;
            return Value(getWorkspacePath());
            });

        reg("pwd", { 0 }, [this](const std::vector<Value>& args) -> Value {
            (void)args;
            std::cout << "  Script dir:    " << cwd() << std::endl;
            std::cout << "  Workspace dir: " << getWorkspacePath() << std::endl;
            return Value::none();
            });

        // =================================================================
        // [15] 控制流辅助函数
        // =================================================================
        reg("print", {}, [](const std::vector<Value>& args) -> Value {
            for (size_t ii = 0; ii < args.size(); ++ii) { if (ii > 0) std::cout << " "; std::cout << args[ii]; }
            std::cout << std::endl; return Value::none();
            });
        reg("println", {}, builtins["print"]);

        reg("bool", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: bool() expects 1 argument.");
            return Value(isTruthy(args[0]) ? 1.0 : 0.0);
            });
        reg("not", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: not() expects 1 argument.");
            return Value(isTruthy(args[0]) ? 0.0 : 1.0);
            });
        reg("and", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: and() expects 2 arguments.");
            return Value((isTruthy(args[0]) && isTruthy(args[1])) ? 1.0 : 0.0);
            });
        reg("or", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: or() expects 2 arguments.");
            return Value((isTruthy(args[0]) || isTruthy(args[1])) ? 1.0 : 0.0);
            });
        reg("seq", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
            double start, step, end;
            if (args.size() == 2) { start = args[0].asDouble(); end = args[1].asDouble(); step = (start <= end) ? 1.0 : -1.0; }
            else if (args.size() == 3) { start = args[0].asDouble(); step = args[1].asDouble(); end = args[2].asDouble(); }
            else throw std::runtime_error("Runtime Error: seq() expects 2 or 3 arguments.");
            if (Tol::isEq(step, 0.0)) throw std::runtime_error("Math Error: Step cannot be zero.");
            std::vector<double> vals;
            if (step > 0) { for (double v = start; v <= end + Tol::EPS * 100; v += step) vals.push_back(v); }
            else { for (double v = start; v >= end - Tol::EPS * 100; v += step) vals.push_back(v); }
            if (vals.empty()) throw std::runtime_error("Math Error: seq() produced empty sequence.");
            return Value(RealMatrix(static_cast<int>(vals.size()), 1, vals));  // ★ 列向量
            });
        reg("assert", { 1, 2, 3 }, [this](const std::vector<Value>& args) -> Value {
            // assert(condition)
            if (args.size() == 1) {
                if (!isTruthy(args[0]))
                    throw std::runtime_error("Assertion Failed.");
                return Value(1.0);
            }
            // assert(condition, message)
            if (args.size() == 2) {
                if (!isTruthy(args[0])) {
                    std::string msg = "Assertion Failed";
                    if (std::holds_alternative<std::string>(args[1].data))
                        msg += ": " + std::get<std::string>(args[1].data);
                    else {
                        std::ostringstream oss; oss << args[1];
                        msg += ": " + oss.str();
                    }
                    throw std::runtime_error(msg);
                }
                return Value(1.0);
            }
            // assert(name, got, expected)
            if (args.size() == 3) {
                std::string name;
                if (std::holds_alternative<std::string>(args[0].data))
                    name = std::get<std::string>(args[0].data);
                else { std::ostringstream oss; oss << args[0]; name = oss.str(); }

                Value got = args[1], expected = args[2];
                bool pass = valuesEqual(got, expected);

                if (!pass) {
                    std::ostringstream oss;
                    oss << "Assertion Failed: [" << name << "]\n"
                        << "       Expected: " << expected << "\n"
                        << "       Got:      " << got;
                    throw std::runtime_error(oss.str());
                }
                return Value(1.0);
            }
            throw std::runtime_error("Runtime Error: assert() expects 1, 2, or 3 arguments.");
            });
        reg("error", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: error() expects 1 argument.");
            std::string msg;
            if (std::holds_alternative<std::string>(args[0].data))
                msg = std::get<std::string>(args[0].data);
            else {
                std::ostringstream oss; oss << args[0];
                msg = oss.str();
            }
            throw ErrorSignal{ msg };
            });

        reg("pcall", { 1 }, [this](const std::vector<Value>& args) -> Value {
            // pcall(f) — protected call: 执行零参函数，返回 list(true, result) 或 list(false, error_msg)
            if (args.size() != 1) throw std::runtime_error("Runtime Error: pcall() expects 1 argument.");
            auto cl = args[0].asFunction();
            if (!cl->paramNames.empty())
                throw std::runtime_error("Runtime Error: pcall() expects a zero-parameter function.");

            try {
                pushScope();
                functionDepth++;
                auto capSaved = injectCaptures(environment, cl->capturedEnv, cl->paramNames);
                Value result;
                {
                    EnvGuard guard(environment, cl->paramNames, {});
                    try { result = evaluate(cl->body.get()); }
                    catch (ReturnSignal& sig) { result = sig.value; }
                }
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();

                List L;
                L.push_back(std::make_any<Value>(Value(1.0)));
                L.push_back(std::make_any<Value>(result));
                return Value(L);
            }
            catch (BreakSignal&) { throw; }
            catch (ContinueSignal&) { throw; }
            catch (ErrorSignal& sig) {
                functionDepth--;
                popScope();
                List L;
                L.push_back(std::make_any<Value>(Value(0.0)));
                L.push_back(std::make_any<Value>(Value(sig.message)));
                return Value(L);
            }
            catch (const std::exception& ex) {
                functionDepth--;
                popScope();
                List L;
                L.push_back(std::make_any<Value>(Value(0.0)));
                L.push_back(std::make_any<Value>(Value(std::string(ex.what()))));
                return Value(L);
            }
            });

        reg("isError", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<List>(args[0].data)) return Value(0.0);
            const auto& L = std::get<List>(args[0].data);
            if (L.size() != 2) return Value(0.0);
            Value first = std::any_cast<Value>(L.raw()[0]);
            if (std::holds_alternative<double>(first.data))
                return Value(Tol::isEq(std::get<double>(first.data), 0.0) ? 1.0 : 0.0);
            return Value(0.0);
            });

        reg("input", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() == 1) {
                if (std::holds_alternative<std::string>(args[0].data))
                    std::cout << std::get<std::string>(args[0].data);
                else
                    std::cout << args[0];
                std::cout << std::flush;
            }
            std::string line;
            if (!std::getline(std::cin, line))
                throw std::runtime_error("IO Error: Failed to read input.");
            return Value(line);
            });

        reg("highlight", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: highlight() expects a string.");
            return Value(jc::highlightCode(std::get<std::string>(args[0].data)));
            });

        reg("color", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: color() expects \"on\" or \"off\".");
            std::string arg = std::get<std::string>(args[0].data);
            if (arg == "on") jc::colorsEnabled = true;
            else if (arg == "off") jc::colorsEnabled = false;
            else throw std::runtime_error("Runtime Error: color() expects \"on\" or \"off\".");
            return Value::none();
            });

        reg("clock", { 0 }, [](const std::vector<Value>& args) -> Value {
            (void)args;
            auto now = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            return Value(static_cast<double>(ms) / 1e6);
            });

        reg("sleep", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: sleep() expects 1 argument.");
            int ms = static_cast<int>(std::round(args[0].asDouble() * 1000));
            if (ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return Value::none();
            });

        reg("modules", { 0 }, [this](const std::vector<Value>& args) -> Value {
            (void)args;
            auto mods = listNativeModules();
            if (mods.empty()) {
                std::cout << "  No native modules available." << std::endl;
                return Value::none();
            }
            std::cout << "  Available native modules:" << std::endl;
            for (const auto& m : mods) {
                bool loaded = importedFiles.count("__native__:" + m) > 0;
                std::cout << "    " << m << (loaded ? "  (loaded)" : "") << std::endl;
            }
            return Value::none();
            });

        // =================================================================
        // [16] 字符串引擎
        // =================================================================

        reg("str", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: str() expects 1 argument.");
            if (std::holds_alternative<std::string>(args[0].data))
                return args[0]; // 已经是字符串
            std::ostringstream oss;
            oss << args[0];
            return Value(oss.str());
            });

        reg("eval", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: eval() expects 1 argument.");
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: eval() expects a string.");
            const std::string& code = std::get<std::string>(args[0].data);
            Lexer lexer(code);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            auto ast = parser.parse();
            return evaluate(ast.get());
            });

        reg("len", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: len() expects 1 argument.");
            if (std::holds_alternative<std::string>(args[0].data))
                return Value(static_cast<double>(std::get<std::string>(args[0].data).size()));
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                if (m.getCols() == 1) return Value(static_cast<double>(m.getRows()));
                if (m.getRows() == 1) return Value(static_cast<double>(m.getCols()));
                return Value(static_cast<double>(m.getRows() * m.getCols()));
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                if (m.getCols() == 1) return Value(static_cast<double>(m.getRows()));
                if (m.getRows() == 1) return Value(static_cast<double>(m.getCols()));
                return Value(static_cast<double>(m.getRows() * m.getCols()));
            }
            if (std::holds_alternative<Dict>(args[0].data)) 
                return Value(static_cast<double>(std::get<Dict>(args[0].data).size()));
            if (std::holds_alternative<List>(args[0].data))
                return Value(static_cast<double>(std::get<List>(args[0].data).size()));
            throw std::runtime_error("Type Error: len() expects a string, vector, or matrix.");
            });

        reg("format", {}, [](const std::vector<Value>& args) -> Value {
            if (args.size() < 1) throw std::runtime_error("Runtime Error: format() expects at least 1 argument.");
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: format() first argument must be a format string.");

            std::string fmt = std::get<std::string>(args[0].data);
            std::string result;
            size_t argIdx = 1;

            for (size_t i = 0; i < fmt.size(); ++i) {
                if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') {
                    // {} — 默认格式
                    if (argIdx >= args.size())
                        throw std::runtime_error("Runtime Error: format() too few arguments for placeholders.");
                    std::ostringstream oss;
                    oss << args[argIdx++];
                    result += oss.str();
                    i += 1; // 跳过 }
                }
                else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == ':') {
                    // {:spec} — 带格式说明
                    size_t close = fmt.find('}', i);
                    if (close == std::string::npos)
                        throw std::runtime_error("Runtime Error: format() unclosed '{' in format string.");
                    std::string spec = fmt.substr(i + 2, close - i - 2);
                    if (argIdx >= args.size())
                        throw std::runtime_error("Runtime Error: format() too few arguments for placeholders.");

                    std::ostringstream oss;

                    // 解析格式说明符
                    char fillChar = ' ';
                    char align = '\0';
                    int width = 0;
                    int precision = -1;
                    char type = '\0';

                    size_t si = 0;
                    // 对齐: <  >  ^
                    if (si < spec.size() && (spec[si] == '<' || spec[si] == '>' || spec[si] == '^')) {
                        align = spec[si++];
                    }
                    // 宽度
                    while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9') {
                        width = width * 10 + (spec[si++] - '0');
                    }
                    // .precision
                    if (si < spec.size() && spec[si] == '.') {
                        si++;
                        precision = 0;
                        while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9') {
                            precision = precision * 10 + (spec[si++] - '0');
                        }
                    }
                    // 类型: f d e s x
                    if (si < spec.size()) {
                        type = spec[si++];
                    }

                    // 格式化值
                    std::string valStr;
                    if (type == 'f' || type == 'e') {
                        double v = args[argIdx].asDouble();
                        if (precision >= 0) oss << std::fixed << std::setprecision(precision);
                        if (type == 'e') oss << std::scientific;
                        oss << v;
                        valStr = oss.str();
                    }
                    else if (type == 'd') {
                        valStr = std::to_string(static_cast<int64_t>(std::round(args[argIdx].asDouble())));
                    }
                    else if (type == 'x') {
                        int64_t v = static_cast<int64_t>(std::round(args[argIdx].asDouble()));
                        oss << std::hex << v;
                        valStr = oss.str();
                    }
                    else {
                        if (precision >= 0) oss << std::fixed << std::setprecision(precision);
                        oss << args[argIdx];
                        valStr = oss.str();
                    }

                    // 对齐填充
                    if (width > 0 && static_cast<int>(valStr.size()) < width) {
                        int pad = width - static_cast<int>(valStr.size());
                        if (align == '<') {
                            valStr = valStr + std::string(pad, fillChar);
                        }
                        else if (align == '^') {
                            int left = pad / 2, right = pad - left;
                            valStr = std::string(left, fillChar) + valStr + std::string(right, fillChar);
                        }
                        else { // 默认右对齐
                            valStr = std::string(pad, fillChar) + valStr;
                        }
                    }

                    result += valStr;
                    argIdx++;
                    i = close; // 跳到 }
                }
                else {
                    result += fmt[i];
                }
            }
            return Value(result);
            });

        reg("type", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: type() expects 1 argument.");
            return std::visit([](auto&& a) -> Value {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, std::monostate>) return Value(std::string("none"));
                else if constexpr (std::is_same_v<T, double>) return Value(std::string("double"));
                else if constexpr (std::is_same_v<T, BigInt>) return Value(std::string("BigInt"));
                else if constexpr (std::is_same_v<T, Fraction>) return Value(std::string("Fraction"));
                else if constexpr (std::is_same_v<T, Complex>) return Value(std::string("Complex"));
                else if constexpr (std::is_same_v<T, std::string>) return Value(std::string("String"));
                else if constexpr (std::is_same_v<T, RealMatrix>) return Value(std::string("RealMatrix"));
                else if constexpr (std::is_same_v<T, ComplexMatrix>) return Value(std::string("ComplexMatrix"));
                else if constexpr (std::is_same_v<T, BaseNum>) return Value(std::string("BaseNum"));
                else if constexpr (std::is_same_v<T, StringMatrix>) return Value(std::string("StringMatrix"));
                else if constexpr (std::is_same_v<T, Dict>) return Value(std::string("Dict"));
                else if constexpr (std::is_same_v<T, List>) return Value(std::string("List"));
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) return Value(std::string("Function"));
                else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) return Value(std::string("Class"));
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) return Value(a->classDef->name);
                else if constexpr (std::is_same_v<T, SuperProxyPtr>) return Value(std::string("super"));
                else return Value(std::string("unknown"));
                }, args[0].data);
            });

        reg("substr", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: substr() expects a string.");
            const std::string& s = std::get<std::string>(args[0].data);
            int n = static_cast<int>(s.size());
            int start = static_cast<int>(std::round(args[1].asDouble()));
            if (start < 0) start = n + start;
            if (start < 0 || start > n)
                throw std::runtime_error("Runtime Error: substr() start index out of range.");
            if (args.size() == 2) return Value(s.substr(start));
            int length = static_cast<int>(std::round(args[2].asDouble()));
            if (length < 0) throw std::runtime_error("Runtime Error: substr() length must be non-negative.");
            return Value(s.substr(start, length));
            });

        reg("charAt", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: charAt() expects a string.");
            const std::string& s = std::get<std::string>(args[0].data);
            int n = static_cast<int>(s.size());
            int idx = static_cast<int>(std::round(args[1].asDouble()));
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::runtime_error("Runtime Error: charAt() index out of range.");
            return Value(std::string(1, s[idx]));
            });

        reg("upper", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: upper() expects a string.");
            std::string s = std::get<std::string>(args[0].data);
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) -> char { return static_cast<char>(std::toupper(c)); });  // ★ 加 -> char 和 static_cast
            return Value(s);
            });

        reg("lower", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: lower() expects a string.");
            std::string s = std::get<std::string>(args[0].data);
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });  // ★ 同上
            return Value(s);
            });

        reg("trim", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: trim() expects a string.");
            std::string s = std::get<std::string>(args[0].data);
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) return Value(std::string(""));
            return Value(s.substr(a, b - a + 1));
            });

        reg("find", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: find() expects two strings.");
            const std::string& s = std::get<std::string>(args[0].data);
            const std::string& sub = std::get<std::string>(args[1].data);
            size_t start = 0;
            if (args.size() == 3) start = static_cast<size_t>(std::round(args[2].asDouble()));
            size_t pos = s.find(sub, start);
            return Value(pos == std::string::npos ? -1.0 : static_cast<double>(pos));
            });

        reg("contains", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: contains() expects two strings.");
            return Value(std::get<std::string>(args[0].data).find(std::get<std::string>(args[1].data))
                != std::string::npos ? 1.0 : 0.0);
            });

        reg("replace", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data) ||
                !std::holds_alternative<std::string>(args[2].data))
                throw std::runtime_error("Type Error: replace() expects three strings.");
            std::string s = std::get<std::string>(args[0].data);
            const std::string& from = std::get<std::string>(args[1].data);
            const std::string& to = std::get<std::string>(args[2].data);
            if (from.empty()) return Value(s);
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return Value(s);
            });

        reg("repeat", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: repeat() expects a string.");
            const std::string& s = std::get<std::string>(args[0].data);
            int n = static_cast<int>(std::round(args[1].asDouble()));
            if (n < 0) throw std::runtime_error("Runtime Error: repeat() count must be non-negative.");
            std::string result;
            result.reserve(s.size() * n);
            for (int ii = 0; ii < n; ++ii) result += s;
            return Value(result);
            });

        reg("concat", {}, [](const std::vector<Value>& args) -> Value {
            std::ostringstream oss;
            for (const auto& a : args) oss << a;
            return Value(oss.str());
            });

        reg("startsWith", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: startsWith() expects two strings.");
            const std::string& s = std::get<std::string>(args[0].data);
            const std::string& prefix = std::get<std::string>(args[1].data);
            return Value(s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0 ? 1.0 : 0.0);
            });

        reg("endsWith", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: endsWith() expects two strings.");
            const std::string& s = std::get<std::string>(args[0].data);
            const std::string& suffix = std::get<std::string>(args[1].data);
            return Value(s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0 ? 1.0 : 0.0);
            });

        reg("split", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: split() expects two strings.");
            const std::string& s = std::get<std::string>(args[0].data);
            const std::string& delim = std::get<std::string>(args[1].data);
            if (delim.empty()) throw std::runtime_error("Runtime Error: split() delimiter cannot be empty.");
            List result;
            size_t start = 0, pos;
            while ((pos = s.find(delim, start)) != std::string::npos) {
                result.push_back(std::make_any<Value>(Value(s.substr(start, pos - start))));
                start = pos + delim.size();
            }
            result.push_back(std::make_any<Value>(Value(s.substr(start))));
            return Value(result);
            });

        reg("ord", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: ord() expects a string.");
            const std::string& s = std::get<std::string>(args[0].data);
            if (s.empty()) throw std::runtime_error("Runtime Error: ord() requires a non-empty string.");
            return Value(static_cast<double>(static_cast<unsigned char>(s[0])));
            });

        reg("chr", { 1 }, [](const std::vector<Value>& args) -> Value {
            int code = static_cast<int>(std::round(args[0].asDouble()));
            if (code < 0 || code > 127) throw std::runtime_error("Runtime Error: chr() code must be 0–127.");
            return Value(std::string(1, static_cast<char>(code)));
            });

        // =================================================================
        // [17] 数组引擎 (Array Engine)
        //   JC2 中数组 = 列向量或行向量 (Nx1 / 1xN RealMatrix)
        // =================================================================

       // --- 提取 double 向量的通用辅助 ---
        auto toVec = [](const Value& v, const std::string& fn) -> std::vector<double> {
            if (std::holds_alternative<RealMatrix>(v.data))
                return std::get<RealMatrix>(v.data).rawData();
            if (std::holds_alternative<ComplexMatrix>(v.data)) {
                const auto& cd = std::get<ComplexMatrix>(v.data).rawData();
                std::vector<double> r(cd.size());
                for (size_t i = 0; i < cd.size(); ++i) {
                    if (!Tol::isEq(cd[i].imag, 0.0))
                        throw std::runtime_error(fn + "() requires real data.");
                    r[i] = cd[i].real;
                }
                return r;
            }
            throw std::runtime_error(fn + "() expects a matrix/vector.");
            };
        // ★ 永远输出行向量
        auto toRow = [](const std::vector<double>& v) -> Value {
            int n = static_cast<int>(v.size());
            if (n == 0) return Value(RealMatrix(1, 0));
            return Value(RealMatrix(1, n, v));
            };

        

        reg("first", { 1 }, [anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: first() expects 1 argument.");
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                if (L.empty()) throw std::runtime_error("Runtime Error: first() on empty list.");
                return anyToVal(L.raw()[0]);
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                if (m.getRows() * m.getCols() == 0) throw std::runtime_error("Runtime Error: first() on empty vector.");
                return Value(m.rawData()[0]);
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                if (m.getRows() * m.getCols() == 0) throw std::runtime_error("Runtime Error: first() on empty vector.");
                return Value(m.rawData()[0]);
            }
            throw std::runtime_error("Type Error: first() expects a vector or list.");
            });
        reg("last", { 1 }, [anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: last() expects 1 argument.");
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                if (L.empty()) throw std::runtime_error("Runtime Error: last() on empty list.");
                return anyToVal(L.raw().back());
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                int n = m.getRows() * m.getCols();
                if (n == 0) throw std::runtime_error("Runtime Error: last() on empty vector.");
                return Value(m.rawData()[n - 1]);
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                int n = m.getRows() * m.getCols();
                if (n == 0) throw std::runtime_error("Runtime Error: last() on empty vector.");
                return Value(m.rawData()[n - 1]);
            }
            throw std::runtime_error("Type Error: last() expects a vector or list.");
            });

        // ── 增删改 ──
        reg("push", { 2 }, [toVec, toRow, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: push(v, val) expects 2 arguments.");
            // ★ List
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                L.push_back(valToAny(args[1]));
                return Value(L);
            }
            auto v = toVec(args[0], "push");
            v.push_back(args[1].asDouble());
            return toRow(v);
            });

        reg("prepend", { 2 }, [toVec, toRow, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: prepend(v, val) expects 2 arguments.");
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                L.insert(0, valToAny(args[1]));
                return Value(L);
            }
            auto v = toVec(args[0], "prepend");
            v.insert(v.begin(), args[1].asDouble());
            return toRow(v);
            });

        reg("insert", { 3 }, [toVec, toRow, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: insert(v, idx, val) expects 3 arguments.");
            int idx = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                L.insert(idx, valToAny(args[2]));
                return Value(L);
            }
            auto v = toVec(args[0], "insert");
            if (idx < 0) idx = static_cast<int>(v.size()) + idx;
            if (idx < 0 || idx > static_cast<int>(v.size()))
                throw std::runtime_error("Runtime Error: insert() index out of range.");
            v.insert(v.begin() + idx, args[2].asDouble());
            return toRow(v);
            });

        reg("removeAt", { 2 }, [toVec, toRow](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: removeAt(v, idx) expects 2 arguments.");
            int idx = static_cast<int>(std::round(args[1].asDouble()));
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                L.removeAt(idx);
                return Value(L);
            }
            auto v = toVec(args[0], "removeAt");
            if (v.empty()) throw std::runtime_error("Runtime Error: removeAt() on empty vector.");
            if (idx < 0) idx = static_cast<int>(v.size()) + idx;
            if (idx < 0 || idx >= static_cast<int>(v.size()))
                throw std::runtime_error("Runtime Error: removeAt() index out of range.");
            v.erase(v.begin() + idx);
            return toRow(v);
            });

        reg("pop", { 1 }, [anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: pop(v) expects 1 argument.");
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                if (L.empty()) throw std::runtime_error("Runtime Error: pop() on empty list.");
                return anyToVal(L.raw().back());
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                auto flat = std::get<RealMatrix>(args[0].data).rawData();
                if (flat.empty()) throw std::runtime_error("Runtime Error: pop() on empty vector.");
                return Value(flat.back());
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                auto flat = std::get<ComplexMatrix>(args[0].data).rawData();
                if (flat.empty()) throw std::runtime_error("Runtime Error: pop() on empty vector.");
                return Value(flat.back());
            }
            throw std::runtime_error("Type Error: pop() expects a vector or list.");
            });

        // ── 切片与结构 ──

        reg("slice", { 2, 3 }, [toVec, toRow, anyToVal](const std::vector<Value>& args) -> Value {
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                int n = static_cast<int>(L.size());
                int start = static_cast<int>(std::round(args[1].asDouble()));
                if (start < 0) start = n + start;
                start = std::max(0, std::min(start, n));
                int end = n;
                if (args.size() == 3) {
                    end = static_cast<int>(std::round(args[2].asDouble()));
                    if (end < 0) end = n + end;
                    end = std::max(0, std::min(end, n));
                }
                List result;
                for (int ii = start; ii < end; ++ii)
                    result.push_back(L.raw()[ii]);
                return Value(result);
            }
            auto v = toVec(args[0], "slice");
            int n = static_cast<int>(v.size());
            int start = static_cast<int>(std::round(args[1].asDouble()));
            if (start < 0) start = n + start;
            start = std::max(0, std::min(start, n));
            int end = n;
            if (args.size() == 3) {
                end = static_cast<int>(std::round(args[2].asDouble()));
                if (end < 0) end = n + end;
                end = std::max(0, std::min(end, n));
            }
            if (start >= end) return toRow({});
            return toRow(std::vector<double>(v.begin() + start, v.begin() + end));
            });

        reg("reverse", { 1 }, [toVec, toRow, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: reverse() expects 1 argument.");
            if (std::holds_alternative<std::string>(args[0].data)) {
                std::string s = std::get<std::string>(args[0].data);
                std::reverse(s.begin(), s.end());
                return Value(s);
            }
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                std::reverse(L.raw().begin(), L.raw().end());
                return Value(L);
            }
            auto v = toVec(args[0], "reverse");
            std::reverse(v.begin(), v.end());
            return toRow(v);
            });

        reg("flatten", { 1 }, [anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: flatten() expects 1 argument.");
            // ★ List：递归展平嵌套 List
            if (std::holds_alternative<List>(args[0].data)) {
                List result;
                std::function<void(const List&)> flattenList = [&](const List& L) {
                    for (const auto& e : L.raw()) {
                        Value elem = anyToVal(e);
                        if (std::holds_alternative<List>(elem.data)) {
                            flattenList(std::get<List>(elem.data));
                        }
                        else {
                            result.push_back(e);
                        }
                    }
                    };
                flattenList(std::get<List>(args[0].data));
                return Value(result);
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                int n = m.getRows() * m.getCols();
                return Value(RealMatrix(n, 1, m.rawData()));
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                int n = m.getRows() * m.getCols();
                return Value(ComplexMatrix(n, 1, m.rawData()));
            }
            if (std::holds_alternative<StringMatrix>(args[0].data)) {
                const auto& m = std::get<StringMatrix>(args[0].data);
                int n = m.getRows() * m.getCols();
                return Value(StringMatrix(n, 1, m.rawData()));
            }
            throw std::runtime_error("Type Error: flatten() expects a matrix or list.");
            });

        reg("unique", { 1 }, [toVec, toRow, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: unique() expects 1 argument.");
            // ★ List
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                List result;
                for (const auto& e : L.raw()) {
                    Value elem = anyToVal(e);
                    bool found = false;
                    for (const auto& r : result.raw()) {
                        if (valuesEqual(elem, anyToVal(r))) { found = true; break; }
                    }
                    if (!found) result.push_back(e);
                }
                return Value(result);
            }
            auto v = toVec(args[0], "unique");
            std::vector<double> result;
            for (double x : v) {
                bool found = false;
                for (double y : result) {
                    if (Tol::isEq(x, y, 1e4)) { found = true; break; }
                }
                if (!found) result.push_back(x);
            }
            return toRow(result);
            });

        // ── 生成 ──
        reg("range", { 1, 2, 3 }, [](const std::vector<Value>& args) -> Value {
            // range(end) → [0, 1, ..., end-1]
            if (args.size() == 1) {
                int end = static_cast<int>(std::round(args[0].asDouble()));
                if (end <= 0) return Value(RealMatrix(1, 0));
                std::vector<double> v(end);
                for (int i = 0; i < end; ++i) v[i] = static_cast<double>(i);
                return Value(RealMatrix(1, end, v));
            }
            // range(start, end) → [start, start+1, ..., end-1]
            if (args.size() == 2) {
                int start = static_cast<int>(std::round(args[0].asDouble()));
                int end = static_cast<int>(std::round(args[1].asDouble()));
                if (start >= end) return Value(RealMatrix(1, 0));
                int n = end - start;
                std::vector<double> v(n);
                for (int i = 0; i < n; ++i) v[i] = static_cast<double>(start + i);
                return Value(RealMatrix(1, n, v));
            }
            // range(start, end, step) → [start, start+step, ...] while < end
            if (args.size() == 3) {
                double start = args[0].asDouble(), end = args[1].asDouble(), step = args[2].asDouble();
                if (Tol::isEq(step, 0.0)) throw std::runtime_error("Math Error: range() step cannot be zero.");
                std::vector<double> v;
                if (step > 0) { for (double x = start; x < end - Tol::EPS * 100; x += step) v.push_back(x); }
                else { for (double x = start; x > end + Tol::EPS * 100; x += step) v.push_back(x); }
                int n = static_cast<int>(v.size());
                if (n == 0) return Value(RealMatrix(1, 0));
                return Value(RealMatrix(1, n, v));
            }
            throw std::runtime_error("Runtime Error: range() expects 1, 2, or 3 arguments.");
            });

        reg("fill", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: fill(val, n) expects 2 arguments.");
            int n = static_cast<int>(std::round(args[1].asDouble()));
            if (n < 0) throw std::runtime_error("Runtime Error: fill() count must be non-negative.");
            double val = args[0].asDouble();
            return Value(RealMatrix(1, n, std::vector<double>(n, val)));  // ★ 行向量，与 seq 区分
            });

        reg("linspace", { 3 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: linspace(a, b, n) expects 3 arguments.");
            double a = args[0].asDouble(), b = args[1].asDouble();
            int n = static_cast<int>(std::round(args[2].asDouble()));
            if (n < 1) throw std::runtime_error("Runtime Error: linspace() requires n >= 1.");
            std::vector<double> v(n);
            if (n == 1) { v[0] = a; }
            else {
                for (int ii = 0; ii < n; ++ii)
                    v[ii] = a + (b - a) * ii / (n - 1);
            }
            return Value(RealMatrix(1, n, v));  // ★ 行向量
            });

        // ── 累积 ──

        reg("cumsum", { 1 }, [toVec, toRow](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: cumsum() expects 1 argument.");
            auto v = toVec(args[0], "cumsum");
            for (size_t i = 1; i < v.size(); ++i) v[i] += v[i - 1];
            return toRow(v);
            });

        reg("cumprod", { 1 }, [toVec, toRow](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: cumprod() expects 1 argument.");
            auto v = toVec(args[0], "cumprod");
            for (size_t i = 1; i < v.size(); ++i) v[i] *= v[i - 1];
            return toRow(v);
            });

        reg("diffs", { 1 }, [toVec, toRow](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: diffs() expects 1 argument.");
            auto v = toVec(args[0], "diffs");
            if (v.size() < 2) throw std::runtime_error("Runtime Error: diffs() requires at least 2 elements.");
            std::vector<double> d(v.size() - 1);
            for (size_t i = 0; i < d.size(); ++i) d[i] = v[i + 1] - v[i];
            return toRow(d);
            });

        // ── 搜索 ──

        reg("indexOf", { 2 }, [toVec, anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: indexOf(v, val) expects 2 arguments.");
            // ★ List
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                for (size_t ii = 0; ii < L.size(); ++ii) {
                    if (valuesEqual(anyToVal(L.raw()[ii]), args[1]))
                        return Value(static_cast<double>(ii));
                }
                return Value(-1.0);
            }
            auto v = toVec(args[0], "indexOf");
            double target = args[1].asDouble();
            for (size_t ii = 0; ii < v.size(); ++ii) {
                if (Tol::isEq(v[ii], target, 1e4)) return Value(static_cast<double>(ii));
            }
            return Value(-1.0);
            });

        reg("count", { 2 }, [toVec, anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: count(v, val) expects 2 arguments.");
            // ★ List
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                int c = 0;
                for (const auto& e : L.raw()) {
                    if (valuesEqual(anyToVal(e), args[1])) c++;
                }
                return Value(static_cast<double>(c));
            }
            auto v = toVec(args[0], "count");
            double target = args[1].asDouble();
            int c = 0;
            for (double x : v) if (Tol::isEq(x, target, 1e4)) c++;
            return Value(static_cast<double>(c));
            });

        // ── 函数式编程 ──

        reg("map", { 2 }, [this, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: map(f, v) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: map() requires a single-parameter function.");
            // ★ List
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                List result;
                for (const auto& e : L.raw()) {
                    Value elem = anyToVal(e);
                    Value y = callFunction(cl, { elem });  // ★
                    result.push_back(valToAny(y));
                }
                return Value(result);
            }
            if (std::holds_alternative<RealMatrix>(args[1].data)) {
                auto flat = std::get<RealMatrix>(args[1].data).rawData();
                std::vector<double> rd; std::vector<Complex> rc; bool hasComplex = false;
                for (double x : flat) {
                    Value y = callFunction(cl, { Value(x) });  // ★
                    if (!hasComplex) {
                        try { rd.push_back(y.asDouble()); }
                        catch (...) { hasComplex = true; for (double d : rd) rc.push_back(Complex(d)); rc.push_back(y.asComplex()); }
                    }
                    else { rc.push_back(y.asComplex()); }
                }
                int n = static_cast<int>(flat.size());
                if (hasComplex) return Value(ComplexMatrix(1, n, rc));
                return Value(RealMatrix(1, n, rd));
            }
            if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
                auto flat = std::get<ComplexMatrix>(args[1].data).rawData();
                std::vector<Complex> rc;
                for (const auto& x : flat) {
                    Value y = callFunction(cl, { Value(x) });  // ★
                    rc.push_back(y.asComplex());
                }
                int n = static_cast<int>(flat.size());
                return Value(ComplexMatrix(1, n, rc));
            }
            throw std::runtime_error("Type Error: map() expects a vector/matrix as second argument.");
            });

        reg("filter", { 2 }, [this, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: filter(f, v) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: filter() requires a single-parameter function.");
            // ★ List
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                List result;
                for (const auto& e : L.raw()) {
                    Value elem = anyToVal(e);
                    Value y = callFunction(cl, { elem });  // ★
                    if (isTruthy(y)) result.push_back(e);
                }
                return Value(result);
            }
            if (std::holds_alternative<RealMatrix>(args[1].data)) {
                auto flat = std::get<RealMatrix>(args[1].data).rawData();
                std::vector<double> result;
                for (double x : flat) {
                    Value y = callFunction(cl, { Value(x) });  // ★
                    if (isTruthy(y)) result.push_back(x);
                }
                int n = static_cast<int>(result.size());
                if (n == 0) return Value(RealMatrix(1, 0));
                return Value(RealMatrix(1, n, result));
            }
            if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
                auto flat = std::get<ComplexMatrix>(args[1].data).rawData();
                std::vector<Complex> result;
                for (const auto& x : flat) {
                    Value y = callFunction(cl, { Value(x) });  // ★
                    if (isTruthy(y)) result.push_back(x);
                }
                int n = static_cast<int>(result.size());
                if (n == 0) return Value(ComplexMatrix(1, 0));
                return Value(ComplexMatrix(1, n, result));
            }
            throw std::runtime_error("Type Error: filter() expects a vector/matrix.");
            });

        reg("reduce", { 2, 3 }, [this, anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() < 2 || args.size() > 3)
                throw std::runtime_error("Runtime Error: reduce(f, v [, init]) expects 2 or 3 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(2))
                throw std::runtime_error("Runtime Error: reduce() requires a two-parameter function f(acc, x).");
            // ★ List
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                Value acc;
                size_t startIdx = 0;
                if (args.size() == 3) { acc = args[2]; }
                else {
                    if (L.empty()) throw std::runtime_error("Runtime Error: reduce() on empty list without initial value.");
                    acc = anyToVal(L.raw()[0]);
                    startIdx = 1;
                }
                for (size_t ii = startIdx; ii < L.size(); ++ii) {
                    Value elem = anyToVal(L.raw()[ii]);
                    acc = callFunction(cl, { acc, elem });  // ★
                }
                return acc;
            }
            std::vector<double> flat;
            if (std::holds_alternative<RealMatrix>(args[1].data))
                flat = std::get<RealMatrix>(args[1].data).rawData();
            else if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
                auto cd = std::get<ComplexMatrix>(args[1].data).rawData();
                for (const auto& c : cd) {
                    if (!Tol::isEq(c.imag, 0.0))
                        throw std::runtime_error("reduce() requires real data.");
                    flat.push_back(c.real);
                }
            }
            else throw std::runtime_error("Type Error: reduce() expects a vector.");
            Value acc;
            size_t startIdx = 0;
            if (args.size() == 3) { acc = args[2]; }
            else {
                if (flat.empty()) throw std::runtime_error("Runtime Error: reduce() on empty vector without initial value.");
                acc = Value(flat[0]);
                startIdx = 1;
            }
            for (size_t ii = startIdx; ii < flat.size(); ++ii) {
                acc = callFunction(cl, { acc, Value(flat[ii]) });  // ★
            }
            return acc;
            });

        reg("any", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: any(f, v) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: any() requires a single-parameter function.");
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                for (const auto& e : L.raw()) {
                    Value elem = std::any_cast<Value>(e);
                    Value y = callFunction(cl, { elem });  // ★
                    if (isTruthy(y)) return Value(1.0);
                }
                return Value(0.0);
            }
            std::vector<double> flat;
            if (std::holds_alternative<RealMatrix>(args[1].data))
                flat = std::get<RealMatrix>(args[1].data).rawData();
            else throw std::runtime_error("Type Error: any() expects a real vector.");
            for (double x : flat) {
                Value y = callFunction(cl, { Value(x) });  // ★
                if (isTruthy(y)) return Value(1.0);
            }
            return Value(0.0);
            });

        reg("all", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: all(f, v) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: all() requires a single-parameter function.");
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                for (const auto& e : L.raw()) {
                    Value elem = std::any_cast<Value>(e);
                    Value y = callFunction(cl, { elem });  // ★
                    if (!isTruthy(y)) return Value(0.0);
                }
                return Value(1.0);
            }
            std::vector<double> flat;
            if (std::holds_alternative<RealMatrix>(args[1].data))
                flat = std::get<RealMatrix>(args[1].data).rawData();
            else throw std::runtime_error("Type Error: all() expects a real vector.");
            for (double x : flat) {
                Value y = callFunction(cl, { Value(x) });  // ★
                if (!isTruthy(y)) return Value(0.0);
            }
            return Value(1.0);
            });

        reg("countIf", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: countIf(f, v) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: countIf() requires a single-parameter function.");
            if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                int c = 0;
                for (const auto& e : L.raw()) {
                    Value elem = std::any_cast<Value>(e);
                    Value y = callFunction(cl, { elem });  // ★
                    if (isTruthy(y)) c++;
                }
                return Value(static_cast<double>(c));
            }
            std::vector<double> flat;
            if (std::holds_alternative<RealMatrix>(args[1].data))
                flat = std::get<RealMatrix>(args[1].data).rawData();
            else throw std::runtime_error("Type Error: countIf() expects a real vector.");
            int c = 0;
            for (double x : flat) {
                Value y = callFunction(cl, { Value(x) });  // ★
                if (isTruthy(y)) c++;
            }
            return Value(static_cast<double>(c));
            });

        // ── 字符串互操作 ──

        reg("join", { 2 }, [toVec, anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: join(v, delim) expects 2 arguments.");
            if (!std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: join() delimiter must be a string.");
            const std::string& delim = std::get<std::string>(args[1].data);

            // ★ List
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                std::ostringstream oss;
                for (size_t ii = 0; ii < L.size(); ++ii) {
                    if (ii > 0) oss << delim;
                    oss << anyToVal(L.raw()[ii]);
                }
                return Value(oss.str());
            }

            auto v = toVec(args[0], "join");
            std::ostringstream oss;
            for (size_t ii = 0; ii < v.size(); ++ii) {
                if (ii > 0) oss << delim;
                double val = v[ii];
                double rounded = std::round(val);
                if (Tol::isEq(val, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded))
                    oss << static_cast<int64_t>(rounded);
                else oss << val;
            }
            return Value(oss.str());
            });

        reg("zip", { 2 }, [anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: zip(a, b) expects 2 arguments.");
            // ★ List
            if (std::holds_alternative<List>(args[0].data) || std::holds_alternative<List>(args[1].data)) {
                // 两边都转成 List
                auto ensureList = [&](const Value& v) -> List {
                    if (std::holds_alternative<List>(v.data))
                        return std::get<List>(v.data);
                    // 将数组/标量转成 List
                    List L;
                    if (std::holds_alternative<RealMatrix>(v.data)) {
                        for (double d : std::get<RealMatrix>(v.data).rawData())
                            L.push_back(std::make_any<Value>(Value(d)));
                    }
                    else if (std::holds_alternative<ComplexMatrix>(v.data)) {
                        for (const auto& c : std::get<ComplexMatrix>(v.data).rawData())
                            L.push_back(std::make_any<Value>(Value(c)));
                    }
                    else {
                        L.push_back(std::make_any<Value>(v));
                    }
                    return L;
                    };
                List a = ensureList(args[0]), b = ensureList(args[1]);
                if (a.size() != b.size())
                    throw std::runtime_error("Math Error: zip() requires same length.");
                List result;
                for (size_t ii = 0; ii < a.size(); ++ii) {
                    List pair;
                    pair.push_back(a.raw()[ii]);
                    pair.push_back(b.raw()[ii]);
                    result.push_back(std::make_any<Value>(Value(pair)));
                }
                return Value(result);
            }
            // 原有矩阵版本
            if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data)) {
                const auto& a = std::get<RealMatrix>(args[0].data);
                const auto& b = std::get<RealMatrix>(args[1].data);
                auto fa = a.rawData(), fb = b.rawData();
                if (fa.size() != fb.size()) throw std::runtime_error("Math Error: zip() vectors must have same length.");
                int n = static_cast<int>(fa.size());
                std::vector<double> flat(n * 2);
                for (int ii = 0; ii < n; ++ii) { flat[ii * 2] = fa[ii]; flat[ii * 2 + 1] = fb[ii]; }
                return Value(RealMatrix(n, 2, flat));
            }
            ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix();
            auto fa = a.rawData(), fb = b.rawData();
            if (fa.size() != fb.size()) throw std::runtime_error("Math Error: zip() vectors must have same length.");
            int n = static_cast<int>(fa.size());
            std::vector<Complex> flat(n * 2);
            for (int ii = 0; ii < n; ++ii) { flat[ii * 2] = fa[ii]; flat[ii * 2 + 1] = fb[ii]; }
            return Value(ComplexMatrix(n, 2, flat));
            });

        reg("cat", {}, [anyToVal, valToAny](const std::vector<Value>& args) -> Value {
            if (args.size() < 1) throw std::runtime_error("Runtime Error: cat() expects at least 1 argument.");

            // 检查是否有 List 参数
            bool hasList = false;
            for (const auto& a : args)
                if (std::holds_alternative<List>(a.data)) { hasList = true; break; }

            if (hasList) {
                // ★ List 模式：所有参数展平为一个 List
                List result;
                for (const auto& a : args) {
                    if (std::holds_alternative<List>(a.data)) {
                        for (const auto& e : std::get<List>(a.data).raw())
                            result.push_back(e);
                    }
                    else {
                        result.push_back(valToAny(a));
                    }
                }
                return Value(result);
            }

            // ★ 原有行为：数值数组拼接
            std::vector<double> flat;
            for (const auto& a : args) {
                if (std::holds_alternative<RealMatrix>(a.data)) {
                    const auto& d = std::get<RealMatrix>(a.data).rawData();
                    flat.insert(flat.end(), d.begin(), d.end());
                }
                else if (std::holds_alternative<double>(a.data)) {
                    flat.push_back(std::get<double>(a.data));
                }
                else if (std::holds_alternative<BigInt>(a.data)) {
                    flat.push_back(std::get<BigInt>(a.data).toDouble());
                }
                else if (std::holds_alternative<Fraction>(a.data)) {
                    flat.push_back(std::get<Fraction>(a.data).toDouble());
                }
                else {
                    throw std::runtime_error("Type Error: cat() only accepts real scalars, real vectors, or Lists.");
                }
            }
            int n = static_cast<int>(flat.size());
            if (n == 0) return Value(RealMatrix(1, 0));
            return Value(RealMatrix(1, n, flat));
            });
// =================================================================
// [18] 字符串矩阵引擎 (StringMatrix Engine)
// =================================================================

// ── 创建 ──

        reg("strmat", {}, [](const std::vector<Value>& args) -> Value {
            // strmat(rows, cols, s1, s2, ...) 或 strmat(rows, cols) 创建空矩阵
            if (args.size() < 2) throw std::runtime_error("Runtime Error: strmat(rows, cols [, ...]) expects at least 2 arguments.");
            int r = static_cast<int>(std::round(args[0].asDouble()));
            int c = static_cast<int>(std::round(args[1].asDouble()));
            if (r < 0 || c < 0) throw std::runtime_error("Runtime Error: strmat() dimensions must be non-negative.");
            if (args.size() == 2) return Value(StringMatrix(r, c));
            if (static_cast<int>(args.size()) - 2 != r * c)
                throw std::runtime_error("Runtime Error: strmat() element count does not match dimensions.");
            std::vector<std::string> flat;
            flat.reserve(r * c);
            for (size_t i = 2; i < args.size(); ++i) {
                if (std::holds_alternative<std::string>(args[i].data))
                    flat.push_back(std::get<std::string>(args[i].data));
                else {
                    std::ostringstream oss;
                    oss << args[i];
                    flat.push_back(oss.str());
                }
            }
            return Value(StringMatrix(r, c, flat));
            });

        // ── 从向量/数组生成 ──

        reg("strvec", {}, [](const std::vector<Value>& args) -> Value {
            // strvec("a", "b", "c") → 列向量
            std::vector<std::string> flat;
            flat.reserve(args.size());
            for (const auto& a : args) {
                if (std::holds_alternative<std::string>(a.data))
                    flat.push_back(std::get<std::string>(a.data));
                else {
                    std::ostringstream oss;
                    oss << a;
                    flat.push_back(oss.str());
                }
            }
            return Value(StringMatrix(static_cast<int>(flat.size()), 1, flat));
            });

        reg("strrow", {}, [](const std::vector<Value>& args) -> Value {
            // strrow("a", "b", "c") → 行向量
            std::vector<std::string> flat;
            flat.reserve(args.size());
            for (const auto& a : args) {
                if (std::holds_alternative<std::string>(a.data))
                    flat.push_back(std::get<std::string>(a.data));
                else {
                    std::ostringstream oss;
                    oss << a;
                    flat.push_back(oss.str());
                }
            }
            return Value(StringMatrix(1, static_cast<int>(flat.size()), flat));
            });

        reg("strfill", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: strfill() expects a string and count.");
            int n = static_cast<int>(std::round(args[1].asDouble()));
            if (n < 0) throw std::runtime_error("Runtime Error: strfill() count must be non-negative.");
            return Value(StringMatrix(1, n, std::vector<std::string>(n, std::get<std::string>(args[0].data))));
            });

        // ── 属性 ──

        // len() 已经支持 RealMatrix/ComplexMatrix，在此扩展到 StringMatrix
        // 需要在现有 len 的基础上加分支，或者单独用 slen
        // 最简单的做法：在 visitCall 路由之前，len 已经注册了，这里覆盖注册
        // 但覆盖会丢失旧实现。所以用一个包装。

        // 改为：在现有 len 注册之后再来一次覆盖，保存旧版本
        {
            auto oldLen = builtins["len"];
            reg("len", { 1 }, [oldLen](const std::vector<Value>& args) -> Value {
                if (args.size() != 1) throw std::runtime_error("Runtime Error: len() expects 1 argument.");
                if (std::holds_alternative<StringMatrix>(args[0].data)) {
                    const auto& m = std::get<StringMatrix>(args[0].data);
                    return Value(static_cast<double>(m.getRows() * m.getCols()));
                }
                return oldLen(args);
                });
        }

        // ── 映射 ──

        reg("strmap", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: strmap(f, M) expects 2 arguments.");
            auto cl = args[0].asFunction();
            if (!cl->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: strmap() requires a single-parameter function.");
            if (!std::holds_alternative<StringMatrix>(args[1].data))
                throw std::runtime_error("Type Error: strmap() expects a StringMatrix.");
            const auto& m = std::get<StringMatrix>(args[1].data);
            std::vector<std::string> result;
            result.reserve(m.getRows() * m.getCols());
            for (int i = 0; i < m.getRows(); ++i) {
                for (int j = 0; j < m.getCols(); ++j) {
                    Value y = callFunction(cl, { Value(m(i, j)) });  // ★
                    if (std::holds_alternative<std::string>(y.data))
                        result.push_back(std::get<std::string>(y.data));
                    else {
                        std::ostringstream oss; oss << y;
                        result.push_back(oss.str());
                    }
                }
            }
            return Value(StringMatrix(m.getRows(), m.getCols(), result));
            });

        // ── 搜索与过滤 ──

        reg("strfind", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<StringMatrix>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: strfind(M, target) expects StringMatrix and string.");
            const auto& m = std::get<StringMatrix>(args[0].data);
            const std::string& target = std::get<std::string>(args[1].data);
            for (int i = 0; i < m.getRows(); ++i)
                for (int j = 0; j < m.getCols(); ++j)
                    if (m(i, j) == target)
                        return Value(RealMatrix(1, 2, { static_cast<double>(i), static_cast<double>(j) }));
            return Value(-1.0);
            });

        reg("strjoin", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<StringMatrix>(args[0].data) ||
                !std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: strjoin(M, delim) expects StringMatrix and string.");
            const auto& m = std::get<StringMatrix>(args[0].data);
            const std::string& delim = std::get<std::string>(args[1].data);
            std::string result;
            bool first = true;
            for (int i = 0; i < m.getRows(); ++i)
                for (int j = 0; j < m.getCols(); ++j) {
                    if (!first) result += delim;
                    result += m(i, j);
                    first = false;
                }
            return Value(result);
            });

        // ── 排序 ──

        reg("strsort", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<StringMatrix>(args[0].data))
                throw std::runtime_error("Type Error: strsort() expects a StringMatrix.");
            const auto& m = std::get<StringMatrix>(args[0].data);
            std::vector<std::string> flat = m.rawData();
            std::sort(flat.begin(), flat.end());
            return Value(StringMatrix(m.getRows(), m.getCols(), flat));
            });

        // ── 数值矩阵转 StringMatrix ──

        reg("toStrMat", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: toStrMat() expects 1 argument.");
            if (std::holds_alternative<StringMatrix>(args[0].data)) return args[0];
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                std::vector<std::string> flat;
                flat.reserve(m.getRows() * m.getCols());
                for (int i = 0; i < m.getRows(); ++i)
                    for (int j = 0; j < m.getCols(); ++j) {
                        std::ostringstream oss; oss << Value(m(i, j));
                        flat.push_back(oss.str());
                    }
                return Value(StringMatrix(m.getRows(), m.getCols(), flat));
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                std::vector<std::string> flat;
                flat.reserve(m.getRows() * m.getCols());
                for (int i = 0; i < m.getRows(); ++i)
                    for (int j = 0; j < m.getCols(); ++j) {
                        std::ostringstream oss; oss << Value(m(i, j));
                        flat.push_back(oss.str());
                    }
                return Value(StringMatrix(m.getRows(), m.getCols(), flat));
            }
            throw std::runtime_error("Type Error: toStrMat() expects a matrix.");
            });

// =================================================================
// [19] 字典引擎 (Dict Engine)
// =================================================================

// 辅助：将任意 Value 转为 dict key（字符串）
        auto toKey = [](const Value& v) -> std::string {
            if (std::holds_alternative<std::string>(v.data))
                return std::get<std::string>(v.data);
            std::ostringstream oss;
            oss << v;
            return oss.str();
            };

        reg("dict", {}, [](const std::vector<Value>& args) -> Value {
            Dict d;
            if (args.size() % 2 != 0)
                throw std::runtime_error("Runtime Error: dict() expects an even number of arguments (key-value pairs).");
            for (size_t ii = 0; ii < args.size(); ii += 2) {
                std::string key;
                if (std::holds_alternative<std::string>(args[ii].data))
                    key = std::get<std::string>(args[ii].data);
                else {
                    std::ostringstream oss; oss << args[ii];
                    key = oss.str();
                }
                d.set(key, std::make_any<Value>(args[ii + 1]));
            }
            return Value(d);
            });

        reg("keys", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: keys() expects a Dict.");
            auto ks = std::get<Dict>(args[0].data).getKeys();
            std::vector<std::string> flat(ks.begin(), ks.end());
            return Value(StringMatrix(1, static_cast<int>(flat.size()), flat));
            });

        reg("values", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: values() expects a Dict.");
            const auto& entries = std::get<Dict>(args[0].data).getEntries();
            // 返回行向量（如果全是数值）或打印（如果异构）
            std::vector<double> nums;
            bool allDouble = true;
            for (const auto& [k, v] : entries) {
                try {
                    const auto& val = std::any_cast<const Value&>(v);
                    if (allDouble) {
                        try { nums.push_back(val.asDouble()); }
                        catch (...) { allDouble = false; }
                    }
                }
                catch (...) { allDouble = false; }
            }
            if (allDouble && !nums.empty())
                return Value(RealMatrix(1, static_cast<int>(nums.size()), nums));

            // 异构值：返回 StringMatrix（每个值转为字符串）
            std::vector<std::string> strs;
            for (const auto& [k, v] : entries) {
                try {
                    const auto& val = std::any_cast<const Value&>(v);
                    std::ostringstream oss; oss << val;
                    strs.push_back(oss.str());
                }
                catch (...) { strs.push_back("?"); }
            }
            return Value(StringMatrix(1, static_cast<int>(strs.size()), strs));
            });

        reg("hasKey", { 2 }, [toKey](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: hasKey() expects a Dict.");
            return Value(std::get<Dict>(args[0].data).has(toKey(args[1])) ? 1.0 : 0.0);
            });

        reg("removeKey", { 2 }, [toKey](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: removeKey() expects a Dict.");
            Dict d = std::get<Dict>(args[0].data);
            std::string key = toKey(args[1]);
            if (!d.remove(key))
                throw std::runtime_error("Runtime Error: Key '" + key + "' not found in Dict.");
            return Value(d);
            });

        reg("dictSize", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: dictSize() expects a Dict.");
            return Value(static_cast<double>(std::get<Dict>(args[0].data).size()));
            });

        reg("dictMerge", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data) ||
                !std::holds_alternative<Dict>(args[1].data))
                throw std::runtime_error("Type Error: dictMerge() expects two Dicts.");
            Dict result = std::get<Dict>(args[0].data);
            const auto& other = std::get<Dict>(args[1].data);
            for (const auto& [k, v] : other.getEntries())
                result.set(k, v);
            return Value(result);
            });

        reg("dictPairs", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<Dict>(args[0].data))
                throw std::runtime_error("Type Error: dictPairs() expects a Dict.");
            const auto& entries = std::get<Dict>(args[0].data).getEntries();
            std::vector<std::string> flat;
            for (const auto& [k, v] : entries) {
                flat.push_back(k);
                try {
                    const auto& val = std::any_cast<const Value&>(v);
                    std::ostringstream oss; oss << val;
                    flat.push_back(oss.str());
                }
                catch (...) { flat.push_back("?"); }
            }
            return Value(StringMatrix(static_cast<int>(entries.size()), 2, flat));
            });

        // =================================================================
        // [20] List 引擎 (异构动态数组)
        // =================================================================

        
        reg("list", {}, [valToAny](const std::vector<Value>& args) -> Value {
            List L;
            for (const auto& a : args)
                L.push_back(valToAny(a));
            return Value(L);
            });

        reg("toList", { 1 }, [valToAny](const std::vector<Value>& args) -> Value {
            if (std::holds_alternative<List>(args[0].data))
                return args[0];

            // ★ RealMatrix → 向量: flat List, 矩阵: List of Lists
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const auto& m = std::get<RealMatrix>(args[0].data);
                if (m.getRows() == 1 || m.getCols() == 1) {
                    // 一维向量 → flat List
                    List L;
                    for (const auto& d : m.rawData())
                        L.push_back(std::make_any<Value>(Value(d)));
                    return Value(L);
                }
                // 二维矩阵 → List of Lists
                List rows;
                for (int i = 0; i < m.getRows(); ++i) {
                    List row;
                    for (int j = 0; j < m.getCols(); ++j)
                        row.push_back(std::make_any<Value>(Value(m(i, j))));
                    rows.push_back(std::make_any<Value>(Value(row)));
                }
                return Value(rows);
            }

            // ★ ComplexMatrix → 同理
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const auto& m = std::get<ComplexMatrix>(args[0].data);
                if (m.getRows() == 1 || m.getCols() == 1) {
                    List L;
                    for (const auto& c : m.rawData())
                        L.push_back(std::make_any<Value>(Value(c)));
                    return Value(L);
                }
                List rows;
                for (int i = 0; i < m.getRows(); ++i) {
                    List row;
                    for (int j = 0; j < m.getCols(); ++j)
                        row.push_back(std::make_any<Value>(Value(m(i, j))));
                    rows.push_back(std::make_any<Value>(Value(row)));
                }
                return Value(rows);
            }

            // ★ StringMatrix → List of Lists (strings)
            if (std::holds_alternative<StringMatrix>(args[0].data)) {
                const auto& m = std::get<StringMatrix>(args[0].data);
                if (m.getRows() == 1 || m.getCols() == 1) {
                    List L;
                    for (const auto& s : m.rawData())
                        L.push_back(std::make_any<Value>(Value(s)));
                    return Value(L);
                }
                List rows;
                for (int i = 0; i < m.getRows(); ++i) {
                    List row;
                    for (int j = 0; j < m.getCols(); ++j)
                        row.push_back(std::make_any<Value>(Value(m(i, j))));
                    rows.push_back(std::make_any<Value>(Value(row)));
                }
                return Value(rows);
            }

            // 字符串 → List of chars
            if (std::holds_alternative<std::string>(args[0].data)) {
                const auto& s = std::get<std::string>(args[0].data);
                List L;
                for (char c : s)
                    L.push_back(std::make_any<Value>(Value(std::string(1, c))));
                return Value(L);
            }

            // 标量 → 单元素 List
            List L;
            L.push_back(std::make_any<Value>(args[0]));
            return Value(L);
            });

        reg("toStrVec", { 1 }, [valToAny, anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: toStrVec() expects 1 argument.");
            // List → StringMatrix（一维）
            if (std::holds_alternative<List>(args[0].data)) {
                const auto& L = std::get<List>(args[0].data);
                std::vector<std::string> flat;
                for (const auto& e : L.raw()) {
                    Value v = anyToVal(e);
                    if (std::holds_alternative<std::string>(v.data))
                        flat.push_back(std::get<std::string>(v.data));
                    else {
                        std::ostringstream oss; oss << v;
                        flat.push_back(oss.str());
                    }
                }
                return Value(StringMatrix(static_cast<int>(flat.size()), 1, flat));
            }
            // 已经是 StringMatrix
            if (std::holds_alternative<StringMatrix>(args[0].data))
                return args[0];
            throw std::runtime_error("Type Error: toStrVec() expects a List or StringMatrix.");
            });

        reg("toArray", { 1 }, [anyToVal](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<List>(args[0].data))
                throw std::runtime_error("Type Error: toArray() expects a List.");
            const auto& L = std::get<List>(args[0].data);
            std::vector<double> flat;
            for (const auto& e : L.raw()) {
                Value v = anyToVal(e);
                flat.push_back(v.asDouble());
            }
            return Value(RealMatrix(1, static_cast<int>(flat.size()), flat));
            });

        reg("toMatrix", { 1 }, [anyToVal](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) throw std::runtime_error("Runtime Error: toMatrix() expects 1 argument.");

            if (std::holds_alternative<RealMatrix>(args[0].data) ||
                std::holds_alternative<ComplexMatrix>(args[0].data) ||
                std::holds_alternative<StringMatrix>(args[0].data))
                return args[0];

            if (!std::holds_alternative<List>(args[0].data))
                throw std::runtime_error("Type Error: toMatrix() expects a List or matrix.");

            const auto& L = std::get<List>(args[0].data);
            if (L.empty()) return Value(RealMatrix(0, 0));

            // 类型检测辅助
            auto isReal = [](const Value& v) {
                return std::holds_alternative<double>(v.data) ||
                    std::holds_alternative<BigInt>(v.data) ||
                    std::holds_alternative<Fraction>(v.data);
                };
            auto isNumeric = [](const Value& v) {
                return std::holds_alternative<double>(v.data) ||
                    std::holds_alternative<BigInt>(v.data) ||
                    std::holds_alternative<Fraction>(v.data) ||
                    std::holds_alternative<Complex>(v.data);
                };
            auto isStr = [](const Value& v) {
                return std::holds_alternative<std::string>(v.data);
                };
            auto valToStr = [](const Value& v) -> std::string {
                if (std::holds_alternative<std::string>(v.data))
                    return std::get<std::string>(v.data);
                std::ostringstream oss; oss << v;
                return oss.str();
                };

            // 检测结构
            Value first = anyToVal(L.raw()[0]);
            bool isNested = std::holds_alternative<List>(first.data);

            if (!isNested) {
                // ★ flat List → 行向量
                int n = static_cast<int>(L.size());
                bool allReal = true, allNum = true, allStr = true;
                for (const auto& e : L.raw()) {
                    Value v = anyToVal(e);
                    if (!isReal(v)) allReal = false;
                    if (!isNumeric(v)) allNum = false;
                    if (!isStr(v)) allStr = false;
                }
                if (allStr) {
                    std::vector<std::string> flat;
                    for (const auto& e : L.raw()) flat.push_back(std::get<std::string>(anyToVal(e).data));
                    return Value(StringMatrix(1, n, flat));
                }
                if (allReal) {
                    std::vector<double> flat;
                    for (const auto& e : L.raw()) flat.push_back(anyToVal(e).asDouble());
                    return Value(RealMatrix(1, n, flat));
                }
                if (allNum) {
                    std::vector<Complex> flat;
                    for (const auto& e : L.raw()) flat.push_back(anyToVal(e).asComplex());
                    return Value(ComplexMatrix(1, n, flat));
                }
                // ★ 混合类型 → StringMatrix
                std::vector<std::string> flat;
                for (const auto& e : L.raw()) flat.push_back(valToStr(anyToVal(e)));
                return Value(StringMatrix(1, n, flat));
            }

            // ★ List of Lists → 二维矩阵
            int rows = static_cast<int>(L.size());
            int cols = -1;
            bool allReal = true, allNum = true, allStr = true;

            for (const auto& rowAny : L.raw()) {
                Value rowVal = anyToVal(rowAny);
                if (!std::holds_alternative<List>(rowVal.data))
                    throw std::runtime_error("Type Error: toMatrix() expects uniform List of Lists.");
                const auto& rowList = std::get<List>(rowVal.data);
                if (cols == -1) cols = static_cast<int>(rowList.size());
                else if (static_cast<int>(rowList.size()) != cols)
                    throw std::runtime_error("Type Error: toMatrix() rows must have equal length.");
                for (const auto& e : rowList.raw()) {
                    Value v = anyToVal(e);
                    if (!isReal(v)) allReal = false;
                    if (!isNumeric(v)) allNum = false;
                    if (!isStr(v)) allStr = false;
                }
            }

            if (cols <= 0) return Value(RealMatrix(0, 0));
            // ★ 先提取所有元素到二维 Value 数组，避免临时对象悬垂
            std::vector<std::vector<Value>> grid;
            for (const auto& rowAny : L.raw()) {
                Value rowVal = anyToVal(rowAny);
                const auto& rowList = std::get<List>(rowVal.data);
                std::vector<Value> rowVec;
                for (const auto& e : rowList.raw())
                    rowVec.push_back(anyToVal(e));
                grid.push_back(std::move(rowVec));
            }
            if (allStr) {
                std::vector<std::string> flat;
                for (const auto& row : grid)
                    for (const auto& v : row)
                        flat.push_back(std::get<std::string>(v.data));
                return Value(StringMatrix(rows, cols, flat));
            }
            if (allReal) {
                std::vector<double> flat;
                for (const auto& row : grid)
                    for (const auto& v : row)
                        flat.push_back(v.asDouble());
                return Value(RealMatrix(rows, cols, flat));
            }
            if (allNum) {
                std::vector<Complex> flat;
                for (const auto& row : grid)
                    for (const auto& v : row)
                        flat.push_back(v.asComplex());
                return Value(ComplexMatrix(rows, cols, flat));
            }
            // 混合类型 → StringMatrix
            std::vector<std::string> flat;
            for (const auto& row : grid)
                for (const auto& v : row)
                    flat.push_back(valToStr(v));
            return Value(StringMatrix(rows, cols, flat));
            });

        // =================================================================
        // [21] 文件 I/O 引擎
        // =================================================================

        reg("readFile", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: readFile() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
            std::ostringstream oss;
            oss << file.rdbuf();
            file.close();
            return Value(oss.str());
        });

        reg("writeFile", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: writeFile() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string content;
            if (std::holds_alternative<std::string>(args[1].data))
                content = std::get<std::string>(args[1].data);
            else {
                std::ostringstream oss; oss << args[1];
                content = oss.str();
            }
            std::ofstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
            file << content;
            file.close();
            return Value::none();
            });

        reg("appendFile", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: appendFile() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string content;
            if (std::holds_alternative<std::string>(args[1].data))
                content = std::get<std::string>(args[1].data);
            else {
                std::ostringstream oss; oss << args[1];
                content = oss.str();
            }
            std::ofstream file(path, std::ios::app);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot append to file '" + path + "'.");
            file << content;
            file.close();
            return Value::none();
            });

        reg("readLines", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: readLines() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
            List L;
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                L.push_back(std::make_any<Value>(Value(line)));
            }
            file.close();
            return Value(L);
            });

        reg("writeLines", { 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: writeLines() expects a string path.");
            if (!std::holds_alternative<List>(args[1].data))
                throw std::runtime_error("Type Error: writeLines() expects a List.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            const auto& L = std::get<List>(args[1].data);
            std::ofstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
            for (const auto& e : L.raw()) {
                Value v = std::any_cast<Value>(e);
                if (std::holds_alternative<std::string>(v.data))
                    file << std::get<std::string>(v.data) << "\n";
                else
                    file << v << "\n";
            }
            file.close();
            return Value::none();
            });

        reg("fileExists", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: fileExists() expects a string path.");
            return Value(std::filesystem::exists(resolvePath(std::get<std::string>(args[0].data))) ? 1.0 : 0.0); // ★
            });

        reg("deleteFile", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: deleteFile() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            if (!std::filesystem::exists(path))
                throw std::runtime_error("IO Error: File '" + path + "' does not exist.");
            std::filesystem::remove(path);
            return Value::none();
            });

        reg("fileSize", { 1 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: fileSize() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            if (!std::filesystem::exists(path))
                throw std::runtime_error("IO Error: File '" + path + "' does not exist.");
            return Value(static_cast<double>(std::filesystem::file_size(path)));
            });

        reg("listDir", { 0, 1 }, [this](const std::vector<Value>& args) -> Value {
            std::string dir = cwd(); // ★ 默认使用当前脚本所在目录
            if (args.size() == 1) {
                if (!std::holds_alternative<std::string>(args[0].data))
                    throw std::runtime_error("Type Error: listDir() expects a string path.");
                dir = resolvePath(std::get<std::string>(args[0].data)); // ★
            }
            if (!std::filesystem::exists(dir))
                throw std::runtime_error("IO Error: Directory '" + dir + "' does not exist.");
            List L;
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                L.push_back(std::make_any<Value>(Value(entry.path().filename().string())));
            }
            return Value(L);
            });

        reg("readCSV", { 1, 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: readCSV() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string delim = ",";
            if (args.size() == 2) {
                if (!std::holds_alternative<std::string>(args[1].data))
                    throw std::runtime_error("Type Error: readCSV() delimiter must be a string.");
                delim = std::get<std::string>(args[1].data);
            }

            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");

            List rows;
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                List row;
                size_t pos = 0, found;
                while ((found = line.find(delim, pos)) != std::string::npos) {
                    row.push_back(std::make_any<Value>(Value(line.substr(pos, found - pos))));
                    pos = found + delim.size();
                }
                row.push_back(std::make_any<Value>(Value(line.substr(pos))));
                rows.push_back(std::make_any<Value>(Value(row)));
            }
            file.close();
            return Value(rows);
            });

        reg("readCSVMat", { 1, 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: readCSVMat() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string delim = ",";
            if (args.size() == 2) {
                if (!std::holds_alternative<std::string>(args[1].data))
                    throw std::runtime_error("Type Error: readCSVMat() delimiter must be a string.");
                delim = std::get<std::string>(args[1].data);
            }

            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");

            std::vector<std::vector<std::string>> rows;
            std::string line;
            size_t maxCols = 0;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                std::vector<std::string> row;
                size_t pos = 0, found;
                while ((found = line.find(delim, pos)) != std::string::npos) {
                    row.push_back(line.substr(pos, found - pos));
                    pos = found + delim.size();
                }
                row.push_back(line.substr(pos));
                if (row.size() > maxCols) maxCols = row.size();
                rows.push_back(row);
            }
            file.close();

            std::vector<std::string> flat;
            for (auto& row : rows) {
                row.resize(maxCols, "");
                flat.insert(flat.end(), row.begin(), row.end());
            }

            return Value(StringMatrix(static_cast<int>(rows.size()), static_cast<int>(maxCols), flat));
            });

        reg("writeCSV", { 2, 3 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: writeCSV() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string delim = ",";
            if (args.size() == 3) {
                if (!std::holds_alternative<std::string>(args[2].data))
                    throw std::runtime_error("Type Error: writeCSV() delimiter must be a string.");
                delim = std::get<std::string>(args[2].data);
            }

            std::ofstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");

            if (std::holds_alternative<RealMatrix>(args[1].data)) {
                const auto& m = std::get<RealMatrix>(args[1].data);
                for (int i = 0; i < m.getRows(); ++i) {
                    for (int j = 0; j < m.getCols(); ++j) {
                        if (j > 0) file << delim;
                        double val = m(i, j);
                        double rounded = std::round(val);
                        if (Tol::isEq(val, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded))
                            file << static_cast<int64_t>(rounded);
                        else
                            file << val;
                    }
                    file << "\n";
                }
            }
            else if (std::holds_alternative<StringMatrix>(args[1].data)) {
                const auto& m = std::get<StringMatrix>(args[1].data);
                for (int i = 0; i < m.getRows(); ++i) {
                    for (int j = 0; j < m.getCols(); ++j) {
                        if (j > 0) file << delim;
                        file << m(i, j);
                    }
                    file << "\n";
                }
            }
            else if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
                const auto& m = std::get<ComplexMatrix>(args[1].data);
                for (int i = 0; i < m.getRows(); ++i) {
                    for (int j = 0; j < m.getCols(); ++j) {
                        if (j > 0) file << delim;
                        file << m(i, j);
                    }
                    file << "\n";
                }
            }
            else if (std::holds_alternative<List>(args[1].data)) {
                const auto& L = std::get<List>(args[1].data);
                for (const auto& e : L.raw()) {
                    Value v = std::any_cast<Value>(e);
                    file << v << "\n";
                }
            }
            else {
                throw std::runtime_error("Type Error: writeCSV() expects a matrix or list.");
            }

            file.close();
            return Value::none();
            });

        reg("parseNum", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: parseNum() expects a string.");
            const std::string& s = std::get<std::string>(args[0].data);
            // 去除首尾空白
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) throw std::runtime_error("Math Error: Cannot parse empty string as number.");
            size_t b = s.find_last_not_of(" \t\r\n");
            std::string trimmed = s.substr(a, b - a + 1);
            try {
                if (trimmed.find('.') != std::string::npos || trimmed.find('e') != std::string::npos || trimmed.find('E') != std::string::npos)
                    return Value(std::stod(trimmed));
                return Value(BigInt(trimmed));
            }
            catch (...) {
                throw std::runtime_error("Math Error: Cannot parse '" + trimmed + "' as a number.");
            }
            });

        reg("parseCSVNum", { 1, 2 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: parseCSVNum() expects a string path.");
            std::string path = resolvePath(std::get<std::string>(args[0].data)); // ★
            std::string delim = ",";
            if (args.size() == 2) {
                if (!std::holds_alternative<std::string>(args[1].data))
                    throw std::runtime_error("Type Error: parseCSVNum() delimiter must be a string.");
                delim = std::get<std::string>(args[1].data);
            }

            std::ifstream file(path);
            if (!file.is_open())
                throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");

            std::vector<std::vector<double>> rows;
            std::string line;
            size_t maxCols = 0;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                std::vector<double> row;
                size_t pos = 0, found;
                while ((found = line.find(delim, pos)) != std::string::npos) {
                    std::string cell = line.substr(pos, found - pos);
                    try { row.push_back(std::stod(cell)); }
                    catch (...) { row.push_back(0.0); }
                    pos = found + delim.size();
                }
                std::string lastCell = line.substr(pos);
                try { row.push_back(std::stod(lastCell)); }
                catch (...) { row.push_back(0.0); }
                if (row.size() > maxCols) maxCols = row.size();
                rows.push_back(row);
            }
            file.close();

            if (rows.empty()) return Value(RealMatrix(0, 0));

            std::vector<double> flat;
            for (auto& row : rows) {
                row.resize(maxCols, 0.0);
                flat.insert(flat.end(), row.begin(), row.end());
            }

            return Value(RealMatrix(static_cast<int>(rows.size()), static_cast<int>(maxCols), flat));
            });


    // =================================================================
    // [24] Class / Instance 引擎
    // =================================================================

        reg("isinstance", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                return Value(0.0);
            if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(args[1].data))
                throw std::runtime_error("Type Error: isinstance() second argument must be a class.");
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto cls = std::get<std::shared_ptr<ClassDefinition>>(args[1].data);
            // ★ 沿继承链查找
            auto c = inst->classDef;
            while (c) {
                if (c.get() == cls.get()) return Value(1.0);
                c = c->parent;
            }
            return Value(0.0);
            });

        reg("hasField", { 2 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                return Value(0.0);
            if (!std::holds_alternative<std::string>(args[1].data))
                throw std::runtime_error("Type Error: hasField() field name must be a string.");
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            return Value(inst->fields.has(std::get<std::string>(args[1].data)) ? 1.0 : 0.0);
            });

        reg("getFields", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                throw std::runtime_error("Type Error: getFields() expects an instance.");
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto keys = inst->fields.getKeys();
            if (keys.empty()) return Value(StringMatrix(1, 0));
            std::vector<std::string> flat(keys.begin(), keys.end());
            return Value(StringMatrix(1, static_cast<int>(flat.size()), flat));
            });

        reg("getClass", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                throw std::runtime_error("Type Error: getClass() expects an instance.");
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            return Value(inst->classDef);
            });

        // ★ 新增
        reg("getParent", { 1 }, [](const std::vector<Value>& args) -> Value {
            std::shared_ptr<ClassDefinition> cls;
            if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(args[0].data))
                cls = std::get<std::shared_ptr<ClassDefinition>>(args[0].data);
            else if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                cls = std::get<std::shared_ptr<Instance>>(args[0].data)->classDef;
            else throw std::runtime_error("Type Error: getParent() expects a class or instance.");
            if (!cls->parent) return Value::none();
            return Value(cls->parent);
            });

        // =================================================================
        // [25] 运算符重载 Dunder 钩子 (Operator Overloading Hooks)
        // =================================================================

        // ★ str() — __str__
        {
            auto oldStr = builtins["str"];
            reg("str", { 1 }, [this, oldStr](const std::vector<Value>& args) -> Value {
                if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
                    auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
                    if (resolveMethod(inst->classDef, "__str__").first)
                        return callInstanceMethod(inst, "__str__", {});
                }
                return oldStr(args);
                });
        }

        // ★ print() / println() — __str__
        {
            reg("print", {}, [this](const std::vector<Value>& args) -> Value {
                for (size_t ii = 0; ii < args.size(); ++ii) {
                    if (ii > 0) std::cout << " ";
                    if (std::holds_alternative<std::shared_ptr<Instance>>(args[ii].data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(args[ii].data);
                        if (resolveMethod(inst->classDef, "__str__").first) {
                            Value s = callInstanceMethod(inst, "__str__", {});
                            std::cout << s;
                            continue;
                        }
                    }
                    std::cout << args[ii];
                }
                std::cout << std::endl;
                return Value::none();
                });
            reg("println", {}, builtins["print"]);
        }

        // ★ len() — __len__
        {
            auto oldLen = builtins["len"];
            reg("len", { 1 }, [this, oldLen](const std::vector<Value>& args) -> Value {
                if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
                    auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
                    if (resolveMethod(inst->classDef, "__len__").first)
                        return callInstanceMethod(inst, "__len__", {});
                }
                return oldLen(args);
                });
        }

        // ★ abs() — __abs__
        {
            auto oldAbs = builtins["abs"];
            reg("abs", { 1 }, [this, oldAbs](const std::vector<Value>& args) -> Value {
                if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
                    auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
                    if (resolveMethod(inst->classDef, "__abs__").first)
                        return callInstanceMethod(inst, "__abs__", {});
                }
                return oldAbs(args);
                });
        }

        // ★ bool() — __bool__
        {
            auto oldBool = builtins["bool"];
            reg("bool", { 1 }, [this, oldBool](const std::vector<Value>& args) -> Value {
                if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
                    auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
                    if (resolveMethod(inst->classDef, "__bool__").first)
                        return Value(isTruthy(callInstanceMethod(inst, "__bool__", {})) ? 1.0 : 0.0);
                }
                return oldBool(args);
                });
        }

        // ★ format() — __str__ 集成
        {
            auto oldFormat = builtins["format"];
            reg("format", {}, [this, oldFormat](const std::vector<Value>& args) -> Value {
                std::vector<Value> processed = args;
                for (size_t i = 1; i < processed.size(); ++i) {
                    if (std::holds_alternative<std::shared_ptr<Instance>>(processed[i].data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(processed[i].data);
                        if (resolveMethod(inst->classDef, "__str__").first)
                            processed[i] = callInstanceMethod(inst, "__str__", {});
                    }
                }
                return oldFormat(processed);
                });
        }

        // ★ imgPlot 需要 Evaluator 来调用用户函数，不能纯模块化
        reg("imgPlot", { 7, 8 }, [this](const std::vector<Value>& args) -> Value {
            if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data))
                throw std::runtime_error("Type Error: Expected an Image.");
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            if (!inst->nativeData.has_value())
                throw std::runtime_error("Type Error: Expected an Image.");
            auto& im = std::any_cast<std::shared_ptr<Image>&>(inst->nativeData);

            auto fn = args[1].asFunction();
            if (!fn->acceptsArgCount(1))
                throw std::runtime_error("Runtime Error: imgPlot() requires a single-parameter function.");
            double xMin = args[2].asDouble(), xMax = args[3].asDouble();
            double yMin = args[4].asDouble(), yMax = args[5].asDouble();
            Color c = Color::parse(std::get<std::string>(args[6].data));
            int thick = (args.size() == 8) ? static_cast<int>(std::round(args[7].asDouble())) : 2;
            int plotW = im->width() - 50;
            int prevPx = -1, prevPy = -1;
            bool isNativeFn = fn->isNative();
            for (int px = 0; px <= plotW; ++px) {
                double x = xMin + (static_cast<double>(px) / plotW) * (xMax - xMin);
                double y = 0;
                try {
                    if (isNativeFn) {
                        auto& nfn = std::any_cast<NativeCallable&>(fn->nativeFn);
                        y = nfn({ Value(x) }).asDouble();
                    }
                    else {
                        pushScope();
                        scopeStack.back().globalNames.insert(fn->paramNames[0]);
                        auto capSaved = injectCaptures(environment, fn->capturedEnv, fn->paramNames);
                        functionDepth++;
                        std::vector<Value> evalArgs = { Value(x) };
                        for (size_t _i = evalArgs.size(); _i < fn->paramNames.size(); ++_i)
                            if (_i < fn->defaultValues.size() && !fn->defaultValues[_i].isNone())
                                evalArgs.push_back(fn->defaultValues[_i]);
                        EnvGuard guard(environment, fn->paramNames, evalArgs);
                        try { y = evaluate(fn->body.get()).asDouble(); }
                        catch (ReturnSignal& sig) { y = sig.value.asDouble(); }
                        functionDepth--;
                        restoreCaptures(environment, capSaved);
                        popScope();
                    }
                }
                catch (...) {
                    if (!isNativeFn) { functionDepth--; popScope(); }
                    prevPx = -1; prevPy = -1; continue;
                }
                int screenX = im->mapPlotX(x, xMin, xMax);
                int screenY = im->mapPlotY(y, yMin, yMax);
                if (prevPx >= 0 && std::abs(screenY - prevPy) < im->height())
                    im->line(prevPx, prevPy, screenX, screenY, c, thick);
                prevPx = screenX; prevPy = screenY;
            }
            return args[0];
            });
    }

    // =================================================================
    // 作用域帧栈引擎
    // =================================================================

    void Evaluator::pushScope() {
        scopeStack.push_back({});
    }

    void Evaluator::popScope() {
        if (scopeStack.empty()) return;
        auto& frame = scopeStack.back();
        for (auto& [name, saved] : frame.savedValues) {
            if (saved.first) {
                environment[name] = saved.second;
            }
            else {
                environment.erase(name);
                constVars.erase(name);
            }
        }
        scopeStack.pop_back();
    }

    void Evaluator::declareLocal(const std::string& name) {
        if (scopeStack.empty()) return;
        auto& frame = scopeStack.back();
        if (frame.savedValues.find(name) != frame.savedValues.end()) return;
        auto it = environment.find(name);
        if (it != environment.end())
            frame.savedValues[name] = { true, it->second };
        else
            frame.savedValues[name] = { false, Value::none() };
    }

    // ★ 新增
    void Evaluator::declareGlobal(const std::string& name) {
        if (scopeStack.empty()) return; // 顶层无意义
        scopeStack.back().globalNames.insert(name);
    }

    // ★ 新增
    bool Evaluator::isGlobal(const std::string& name) const {
        if (scopeStack.empty()) return true; // 顶层一切都是全局
        return scopeStack.back().globalNames.count(name) > 0;
    }

    // =================================================================
    // 真值判定
    // =================================================================
    void Evaluator::assertNotConst(const std::string& name) const {
        if (constVars.count(name))
            throw std::runtime_error("Runtime Error: Cannot modify const variable '" + name + "'.");
    }

    Value Evaluator::callFunction(std::shared_ptr<FunctionClosure> closure,
        const std::vector<Value>& args) {
        if (closure->isNative()) {
            auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
            return fn(args);
        }

        std::vector<Value> finalArgs = fillDefaults(closure, args);

        if (++recursionDepth > MAX_RECURSION_DEPTH) {
            recursionDepth = 0;
            throw std::runtime_error("Runtime Error: Stack Overflow!");
        }

        pushScope();
        for (const auto& pName : closure->paramNames)
            scopeStack.back().globalNames.insert(pName);

        auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);

        functionDepth++;
        Value result;
        {
            EnvGuard guard(environment, closure->paramNames, finalArgs);
            try {
                try { result = evaluate(closure->body.get()); }
                catch (ReturnSignal& sig) { result = sig.value; }
            }
            catch (...) {
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                recursionDepth--;
                throw;
            }
        }
        functionDepth--;
        restoreCaptures(environment, capSaved);
        popScope();
        recursionDepth--;
        return result;
    }

    bool Evaluator::isTruthy(const Value& v) {
        if (std::holds_alternative<std::monostate>(v.data)) return false;
        if (std::holds_alternative<double>(v.data)) {
            double d = std::get<double>(v.data);
            return !Tol::isEq(d, 0.0) && !std::isnan(d);
        }
        if (std::holds_alternative<BigInt>(v.data))
            return !std::get<BigInt>(v.data).isZero();
        if (std::holds_alternative<Complex>(v.data))
            return !Tol::isEq(std::get<Complex>(v.data).modulus(), 0.0);
        if (std::holds_alternative<Fraction>(v.data))
            return !std::get<Fraction>(v.data).getNum().isZero();
        if (std::holds_alternative<BaseNum>(v.data))
            return !std::get<BaseNum>(v.data).getValue().isZero();
        if (std::holds_alternative<std::string>(v.data))              // ★ 新增
            return !std::get<std::string>(v.data).empty();            // "" → false
        if (std::holds_alternative<Dict>(v.data))
            return !std::get<Dict>(v.data).empty();
        if (std::holds_alternative<List>(v.data))
            return !std::get<List>(v.data).empty();
        if (std::holds_alternative<std::shared_ptr<Instance>>(v.data)) return true;
        if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(v.data)) return true;
        return true;
    }

    // =================================================================
    // 核心执行
    // =================================================================
    Value Evaluator::evaluate(Expr* expr) { return std::any_cast<Value>(expr->accept(*this)); }

    Value Evaluator::calculate(Expr* expr) {
        try {
            return evaluate(expr);
        }
        catch (BreakSignal&) { throw std::runtime_error("Syntax Error: 'break' used outside of a loop."); }
        catch (ContinueSignal&) { throw std::runtime_error("Syntax Error: 'continue' used outside of a loop."); }
        catch (ReturnSignal& sig) { return sig.value; }
        catch (ErrorSignal& sig) { throw std::runtime_error(sig.message); }   // ★ 未捕获的 throw 变成运行时错误
    }

    std::any Evaluator::visitLiteral(Literal* expr) {
        const std::string& s = expr->value;
        if (expr->isString) return Value(s);

        // ★ 虚数字面量: 3i → Complex(0, 3)
        if (expr->isImaginary) {
            return Value(Complex(0.0, std::stod(s)));
        }

        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
            try { return Value(BigInt(s)); }
            catch (...) { return Value(std::stod(s)); }
        }
        return Value(std::stod(s));
    }

    std::any Evaluator::visitVariable(Variable* expr) {
        auto it = environment.find(expr->name.lexeme);
        if (it != environment.end()) return it->second;

        // ★ 内建函数自动包装为一等公民
        auto bit = builtins.find(expr->name.lexeme);
        if (bit != builtins.end()) {
            // ★ 从 builtinArity 推断参数个数，生成占位参数名
            std::vector<std::string> dummyParams;
            std::vector<bool> dummyIsRef;
            auto arityIt = builtinArity.find(expr->name.lexeme);
            if (arityIt != builtinArity.end() && !arityIt->second.empty()) {
                int minArity = *arityIt->second.begin();
                for (int i = 0; i < minArity; ++i) {
                    dummyParams.push_back("_" + std::to_string(i));
                    dummyIsRef.push_back(false);
                }
            }

            auto wrapper = std::make_shared<FunctionClosure>(
                dummyParams,
                dummyIsRef,
                expr->name.lexeme,
                nullptr
            );
            wrapper->nativeFn = std::make_any<NativeCallable>(bit->second);
            return Value(wrapper);
        }

        throw std::runtime_error("Runtime Error: Undefined variable '" + expr->name.lexeme + "'.");
    }

    std::any Evaluator::visitAssign(Assign* expr) {
        assertNotConst(expr->name.lexeme);
        Value value = evaluate(expr->value.get());
        // ★ Python 风格：函数内，非 global 声明的新变量自动 local
        if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(expr->name.lexeme)) {
            declareLocal(expr->name.lexeme);
        }
        environment[expr->name.lexeme] = value;
        return value;
    }

    std::any Evaluator::visitUnary(Unary* expr) {
        Value right = evaluate(expr->right.get());
        // ★ 运算符重载：__neg__
        if (expr->op.type == TokenType::MINUS && hasDunder(right, "__neg__")) {
            auto inst = std::get<std::shared_ptr<Instance>>(right.data);
            return callInstanceMethod(inst, "__neg__", {});
        }
        switch (expr->op.type) {
        case TokenType::MINUS: return -right;
        case TokenType::PLUS:  return right;
        case TokenType::BANG:  return Value(isTruthy(right) ? 0.0 : 1.0);
        default: break;
        }
        return Value(0.0);
    }

    std::any Evaluator::visitBinary(Binary* expr) {
        // 短路逻辑 (&&, ||)
        if (expr->op.type == TokenType::AND_AND) {
            Value left = evaluate(expr->left.get());
            if (!isTruthy(left)) return Value(0.0);
            Value right = evaluate(expr->right.get());
            return Value(isTruthy(right) ? 1.0 : 0.0);
        }
        if (expr->op.type == TokenType::OR_OR) {
            Value left = evaluate(expr->left.get());
            if (isTruthy(left)) return Value(1.0);
            Value right = evaluate(expr->right.get());
            return Value(isTruthy(right) ? 1.0 : 0.0);
        }
        // ★ 新增：x in container
        if (expr->op.type == TokenType::IN) {
            Value needle = evaluate(expr->left.get());
            Value haystack = evaluate(expr->right.get());
            // 字符串 in 字符串 → 子串检查
            if (std::holds_alternative<std::string>(needle.data) &&
                std::holds_alternative<std::string>(haystack.data)) {
                return Value(std::get<std::string>(haystack.data).find(
                    std::get<std::string>(needle.data)) != std::string::npos ? 1.0 : 0.0);
            }
            // ★ 新增：非字符串 in 字符串 → 明确报错
            if (std::holds_alternative<std::string>(haystack.data)) {
                throw std::runtime_error("Type Error: 'in' on string requires a string on the left side.");
            }
            // x in RealMatrix → 元素查找
            if (std::holds_alternative<RealMatrix>(haystack.data)) {
                const auto& m = std::get<RealMatrix>(haystack.data);
                double target = needle.asDouble();
                for (const auto& v : m.rawData()) {
                    if (Tol::isEq(v, target, 1e4)) return Value(1.0);
                }
                return Value(0.0);
            }
            // x in ComplexMatrix
            if (std::holds_alternative<ComplexMatrix>(haystack.data)) {
                const auto& m = std::get<ComplexMatrix>(haystack.data);
                Complex target = needle.asComplex();
                for (const auto& v : m.rawData()) {
                    if (v == target) return Value(1.0);
                }
                return Value(0.0);
            }
            // x in StringMatrix
            if (std::holds_alternative<StringMatrix>(haystack.data)) {
                if (!std::holds_alternative<std::string>(needle.data))
                    throw std::runtime_error("Type Error: 'in' on StringMatrix requires a string needle.");
                const auto& m = std::get<StringMatrix>(haystack.data);
                const auto& target = std::get<std::string>(needle.data);
                for (const auto& v : m.rawData()) {
                    if (v == target) return Value(1.0);
                }
                return Value(0.0);
            }
            // x in List
            if (std::holds_alternative<List>(haystack.data)) {
                const auto& L = std::get<List>(haystack.data);
                for (const auto& e : L.raw()) {
                    try {
                        Value elem = std::any_cast<Value>(e);
                        if (valuesEqual(needle, elem)) return Value(1.0);
                    }
                    catch (...) {}
                }
                return Value(0.0);
            }
            // key in Dict → 检查 key 是否存在
            if (std::holds_alternative<Dict>(haystack.data)) {
                std::string key;
                if (std::holds_alternative<std::string>(needle.data))
                    key = std::get<std::string>(needle.data);
                else {
                    std::ostringstream oss; oss << needle;
                    key = oss.str();
                }
                return Value(std::get<Dict>(haystack.data).has(key) ? 1.0 : 0.0);
            }
            // ★ Instance __contains__
            if (hasDunder(haystack, "__contains__")) {
                auto inst = std::get<std::shared_ptr<Instance>>(haystack.data);
                Value result = callInstanceMethod(inst, "__contains__", { needle });
                return Value(isTruthy(result) ? 1.0 : 0.0);
            }
            throw std::runtime_error("Type Error: 'in' requires an array, vector, matrix, or string on the right side.");
        }

        // ★ 管道运算符: value |> function
        if (expr->op.type == TokenType::PIPE) {
            Value left = evaluate(expr->left.get());
            Value right = evaluate(expr->right.get());

            if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(right.data)) {
                auto closure = std::get<std::shared_ptr<FunctionClosure>>(right.data);
                // ★ Native 函数快速路径
                if (closure->isNative()) {
                    auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
                    return fn({ left });
                }
                // ★ AST 闭包：含默认参数填充
                std::vector<Value> args = { left };
                args = fillDefaults(closure, std::move(args));

                if (++recursionDepth > MAX_RECURSION_DEPTH) {
                    recursionDepth = 0;
                    throw std::runtime_error("Runtime Error: Stack Overflow!");
                }
                pushScope();
                for (const auto& pName : closure->paramNames)
                    scopeStack.back().globalNames.insert(pName);
                auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);
                functionDepth++;
                Value result;
                {
                    EnvGuard guard(environment, closure->paramNames, args);
                    try {
                        try { result = evaluate(closure->body.get()); }
                        catch (ReturnSignal& sig) { result = sig.value; }
                    }
                    catch (...) {
                        functionDepth--;
                        restoreCaptures(environment, capSaved);
                        popScope();
                        recursionDepth--;
                        throw;
                    }
                }
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                recursionDepth--;
                return result;
            }
            throw std::runtime_error("Type Error: Right side of |> must be a function.");
        }

        // 正常求值
        Value left = evaluate(expr->left.get());
        Value right = evaluate(expr->right.get());

        // ★ 运算符重载：Instance dunder methods
        if (std::holds_alternative<std::shared_ptr<Instance>>(left.data)) {
            // 算术 dunder
            static const std::map<TokenType, std::string> arithDunders = {
                {TokenType::PLUS, "__add__"}, {TokenType::MINUS, "__sub__"},
                {TokenType::STAR, "__mul__"}, {TokenType::SLASH, "__div__"},
                {TokenType::PERCENT, "__mod__"}, {TokenType::CARET, "__pow__"},
                {TokenType::BACKSLASH, "__ldiv__"},
            };
            auto ait = arithDunders.find(expr->op.type);
            if (ait != arithDunders.end() && hasDunder(left, ait->second)) {
                auto inst = std::get<std::shared_ptr<Instance>>(left.data);
                return callInstanceMethod(inst, ait->second, { right });
            }

            // 比较 dunder
            static const std::map<TokenType, std::string> cmpDunders = {
                {TokenType::EQUAL, "__eq__"}, {TokenType::BANG_EQUAL, "__neq__"},
                {TokenType::LESS, "__lt__"}, {TokenType::LESS_EQUAL, "__le__"},
                {TokenType::GREATER, "__gt__"}, {TokenType::GREATER_EQUAL, "__ge__"},
            };
            auto cit = cmpDunders.find(expr->op.type);
            if (cit != cmpDunders.end()) {
                auto inst = std::get<std::shared_ptr<Instance>>(left.data);
                if (hasDunder(left, cit->second)) {
                    Value result = callInstanceMethod(inst, cit->second, { right });
                    return Value(isTruthy(result) ? 1.0 : 0.0);
                }
                // != 降级：用 __eq__ 取反
                if (expr->op.type == TokenType::BANG_EQUAL && hasDunder(left, "__eq__")) {
                    Value result = callInstanceMethod(inst, "__eq__", { right });
                    return Value(isTruthy(result) ? 0.0 : 1.0);
                }
            }
        }

        // ★ 右侧反向 dunder（支持 3 + vec → vec.__radd__(3)）
        if (!std::holds_alternative<std::shared_ptr<Instance>>(left.data) &&
            std::holds_alternative<std::shared_ptr<Instance>>(right.data)) {
            static const std::map<TokenType, std::string> reverseDunders = {
                {TokenType::PLUS, "__radd__"}, {TokenType::MINUS, "__rsub__"},
                {TokenType::STAR, "__rmul__"}, {TokenType::SLASH, "__rdiv__"},
                {TokenType::PERCENT, "__rmod__"}, {TokenType::CARET, "__rpow__"},
            };
            auto rit = reverseDunders.find(expr->op.type);
            if (rit != reverseDunders.end() && hasDunder(right, rit->second)) {
                auto inst = std::get<std::shared_ptr<Instance>>(right.data);
                return callInstanceMethod(inst, rit->second, { left });
            }
        }

        switch (expr->op.type) {
        case TokenType::PLUS:    return left + right;
        case TokenType::MINUS:   return left - right;
        case TokenType::STAR:    return left * right;
        case TokenType::SLASH:   return left / right;
        case TokenType::PERCENT: return left % right;
        case TokenType::CARET:   return left ^ right;
        case TokenType::BACKSLASH: return jc::ldivide(left, right);
        case TokenType::EQUAL: case TokenType::BANG_EQUAL:
        case TokenType::LESS: case TokenType::LESS_EQUAL:
        case TokenType::GREATER: case TokenType::GREATER_EQUAL:
            return compareValues(left, right, expr->op.type);
        default: break;
        }
        return Value(0.0);
    }

    std::any Evaluator::visitCall(Call* expr) {
        std::string funcName = expr->callee.lexeme;

        // 1. 内建函数（按元数路由）
        auto builtinIt = builtins.find(funcName);
        if (builtinIt != builtins.end()) {
            auto arityIt = builtinArity.find(funcName);
            int numArgs = static_cast<int>(expr->arguments.size());
            bool builtinClaims = true;
            if (arityIt != builtinArity.end() && !arityIt->second.empty())
                builtinClaims = arityIt->second.count(numArgs) > 0;
            if (builtinClaims) {
                std::vector<Value> args;
                for (auto& argExpr : expr->arguments) args.push_back(evaluate(argExpr.get()));
                return builtinIt->second(args);
            }
        }

        // 2. 用户闭包 / 类构造函数
        auto envIt = environment.find(funcName);
        if (envIt != environment.end()) {

            // ★ 2a. 类构造函数调用: ClassName(args)
            if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(envIt->second.data)) {
                auto classDef = std::get<std::shared_ptr<ClassDefinition>>(envIt->second.data);

                auto instance = std::make_shared<Instance>();
                instance->classDef = classDef;

                // ★ 通过继承链解析 init
                auto [initClosure, initOwner] = resolveMethod(classDef, "init");
                if (initClosure) {
                    std::vector<Value> callArgs;
                    for (auto& a : expr->arguments) callArgs.push_back(evaluate(a.get()));
                    callInstanceMethod(instance, "init", callArgs);
                }
                else if (!expr->arguments.empty()) {
                    throw std::runtime_error("Runtime Error: Class '" + funcName +
                        "' has no init() and cannot accept constructor arguments.");
                }

                return Value(instance);
            }

            // ★ 2b. 用户函数闭包
            if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(envIt->second.data)) {
                if (++recursionDepth > MAX_RECURSION_DEPTH) {
                    recursionDepth = 0;
                    throw std::runtime_error("Runtime Error: Stack Overflow!");
                }

                auto closure = std::get<std::shared_ptr<FunctionClosure>>(envIt->second.data);

                // ★ Native 函数快速路径
                if (closure->isNative()) {
                    std::vector<Value> callArgs;
                    for (auto& argExpr : expr->arguments)
                        callArgs.push_back(evaluate(argExpr.get()));
                    auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
                    recursionDepth--;
                    return fn(callArgs);
                }

                // 求值参数
                std::vector<Value> callArgs;
                for (auto& argExpr : expr->arguments)
                    callArgs.push_back(evaluate(argExpr.get()));

                // ★ 填充默认参数（已含数量校验）
                try {
                    callArgs = fillDefaults(closure, std::move(callArgs), funcName);
                }
                catch (...) {
                    recursionDepth--;
                    throw;
                }

                // ref 参数识别
                std::vector<std::string> refCallerNames(closure->paramNames.size());
                for (size_t ii = 0; ii < closure->paramNames.size(); ++ii) {
                    if (closure->isRef[ii]) {
                        if (ii < expr->arguments.size()) {
                            auto* varExpr = dynamic_cast<Variable*>(expr->arguments[ii].get());
                            if (!varExpr) {
                                recursionDepth--;
                                throw std::runtime_error("Runtime Error: Argument " + std::to_string(ii + 1) +
                                    " of '" + funcName + "' is declared 'ref' and requires a variable.");
                            }
                            if (constVars.count(varExpr->name.lexeme)) {
                                recursionDepth--;
                                throw std::runtime_error("Runtime Error: Cannot pass const variable '" +
                                    varExpr->name.lexeme + "' as 'ref' parameter.");
                            }
                            refCallerNames[ii] = varExpr->name.lexeme;
                        }
                        // 如果 ii >= expr->arguments.size()，说明是默认值填充的 ref 参数
                        // 这种情况不允许（ref 参数不应该有默认值），但这里静默跳过
                    }
                }

                pushScope();
                for (const auto& pName : closure->paramNames)
                    scopeStack.back().globalNames.insert(pName);
                // ★ 注入闭包捕获
                auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);
                functionDepth++;
                Value result;
                std::vector<Value> refWriteBack(closure->paramNames.size());
                {
                    EnvGuard guard(environment, closure->paramNames, callArgs);
                    try {
                        try { result = evaluate(closure->body.get()); }
                        catch (ReturnSignal& sig) { result = sig.value; }
                        for (size_t ii = 0; ii < closure->paramNames.size(); ++ii)
                            if (!refCallerNames[ii].empty())
                                refWriteBack[ii] = environment[closure->paramNames[ii]];
                    }
                    catch (...) {
                        functionDepth--;
                        restoreCaptures(environment, capSaved);
                        popScope();
                        recursionDepth--;
                        throw;
                    }
                }
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                for (size_t ii = 0; ii < closure->paramNames.size(); ++ii)
                    if (!refCallerNames[ii].empty())
                        environment[refCallerNames[ii]] = refWriteBack[ii];
                recursionDepth--;
                return result;
            }
        }

        // ★ 3. 内建函数存在但元数不匹配 → 尝试用户同名闭包路由已在上方处理
        // 如果内建函数存在但元数不匹配，给出精确的参数提示
        auto builtinCheck = builtins.find(funcName);
        if (builtinCheck != builtins.end()) {
            auto arityCheck = builtinArity.find(funcName);
            if (arityCheck != builtinArity.end() && !arityCheck->second.empty()) {
                std::string expected;
                for (auto it = arityCheck->second.begin(); it != arityCheck->second.end(); ++it) {
                    if (it != arityCheck->second.begin()) expected += " or ";
                    expected += std::to_string(*it);
                }
                throw std::runtime_error("Runtime Error: " + funcName + "() expects " +
                    expected + " arguments, got " +
                    std::to_string(expr->arguments.size()) + ".");
            }
        }
        throw std::runtime_error("Runtime Error: Unknown function or not callable '" + funcName + "()'.");
    }

    std::any Evaluator::visitMatrixNode(MatrixNode* expr) {
        int rows = static_cast<int>(expr->elements.size());
        if (rows == 0) return Value(RealMatrix(0, 0));
        int cols = static_cast<int>(expr->elements[0].size());

        std::vector<Value> evaluatedElements;
        evaluatedElements.reserve(static_cast<size_t>(rows) * cols);
        bool hasComplex = false;
        bool hasString = false;
        bool hasOther = false;

        auto canBeMatrixElement = [](const Value& v) -> bool {
            return std::holds_alternative<double>(v.data) ||
                std::holds_alternative<BigInt>(v.data) ||
                std::holds_alternative<Fraction>(v.data) ||
                std::holds_alternative<BaseNum>(v.data) ||
                std::holds_alternative<Complex>(v.data) ||
                std::holds_alternative<std::string>(v.data) ||
                std::holds_alternative<RealMatrix>(v.data) ||    // ★ 现在将矩阵内部包含矩阵视为完全合法并可以被拆解
                std::holds_alternative<ComplexMatrix>(v.data) ||
                std::holds_alternative<StringMatrix>(v.data);
            };

        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < cols; ++j) {
                Value val = evaluate(expr->elements[i][j].get());
                if (std::holds_alternative<Complex>(val.data) || std::holds_alternative<ComplexMatrix>(val.data)) hasComplex = true;
                if (std::holds_alternative<std::string>(val.data) || std::holds_alternative<StringMatrix>(val.data)) hasString = true;
                if (!canBeMatrixElement(val)) hasOther = true;
                evaluatedElements.push_back(val);
            }
        }

        // 包含任意不可能是被块拼接的元素 (List, Dict, Function) => 保留旧有的 嵌套 List 解构逻辑
        if (hasOther) {
            if (rows == 1) {
                List L; for (auto& v : evaluatedElements) L.push_back(std::make_any<Value>(std::move(v)));
                return Value(L);
            }
            else {
                List outer; int idx = 0;
                for (int i = 0; i < rows; ++i) {
                    List inner;
                    for (int j = 0; j < cols; ++j) inner.push_back(std::make_any<Value>(std::move(evaluatedElements[idx++])));
                    outer.push_back(std::make_any<Value>(Value(inner)));
                }
                return Value(outer);
            }
        }

        // ★★★ 核弹级装配逻辑：能够容忍里面是嵌套并能够做矩阵块级别的融合！
        Value matResult = Value::none();
        int idx = 0;

        auto extractMatrixVal = [&](Value& cell) {
            // 已经是矩阵类型 → 可能需要统一升维
            if (std::holds_alternative<RealMatrix>(cell.data) ||
                std::holds_alternative<ComplexMatrix>(cell.data) ||
                std::holds_alternative<StringMatrix>(cell.data)) {

                if (hasString) {
                    // ★ 统一升维到 StringMatrix
                    if (std::holds_alternative<RealMatrix>(cell.data)) {
                        const auto& m = std::get<RealMatrix>(cell.data);
                        std::vector<std::string> flat;
                        flat.reserve(m.getRows() * m.getCols());
                        for (int i = 0; i < m.getRows(); ++i)
                            for (int j = 0; j < m.getCols(); ++j) {
                                std::ostringstream oss; oss << Value(m(i, j));
                                flat.push_back(oss.str());
                            }
                        cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                    }
                    else if (std::holds_alternative<ComplexMatrix>(cell.data)) {
                        const auto& m = std::get<ComplexMatrix>(cell.data);
                        std::vector<std::string> flat;
                        flat.reserve(m.getRows() * m.getCols());
                        for (int i = 0; i < m.getRows(); ++i)
                            for (int j = 0; j < m.getCols(); ++j) {
                                std::ostringstream oss; oss << Value(m(i, j));
                                flat.push_back(oss.str());
                            }
                        cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                    }
                    // 已经是 StringMatrix → 不需要转换
                }
                else if (hasComplex && std::holds_alternative<RealMatrix>(cell.data)) {
                    cell = Value(cell.asComplexMatrix());
                }
                return;
            }
            // 标量 → 包装成 1x1 矩阵
            if (hasString) {
                std::ostringstream ss; ss << cell;
                cell = Value(StringMatrix(1, 1, { ss.str() }));
            }
            else if (hasComplex) {
                cell = Value(ComplexMatrix(1, 1, { cell.asComplex() }));
            }
            else {
                cell = Value(RealMatrix(1, 1, { cell.asDouble() }));
            }
            };

        try {
            for (int i = 0; i < rows; ++i) {
                Value rowResult = Value::none();
                for (int j = 0; j < cols; ++j) {
                    Value cell = evaluatedElements[idx++];
                    extractMatrixVal(cell);

                    if (rowResult.isNone()) rowResult = cell;
                    else {
                        if (hasString) rowResult = Value(std::get<StringMatrix>(rowResult.data).integR(std::get<StringMatrix>(cell.data)));
                        else if (hasComplex) rowResult = Value(std::get<ComplexMatrix>(rowResult.data).integR(std::get<ComplexMatrix>(cell.data)));
                        else rowResult = Value(std::get<RealMatrix>(rowResult.data).integR(std::get<RealMatrix>(cell.data)));
                    }
                }
                if (matResult.isNone()) matResult = rowResult;
                else {
                    if (hasString) matResult = Value(std::get<StringMatrix>(matResult.data).integC(std::get<StringMatrix>(rowResult.data)));
                    else if (hasComplex) matResult = Value(std::get<ComplexMatrix>(matResult.data).integC(std::get<ComplexMatrix>(rowResult.data)));
                    else matResult = Value(std::get<RealMatrix>(matResult.data).integC(std::get<RealMatrix>(rowResult.data)));
                }
            }
        }
        catch (...) {
            throw std::runtime_error("Matrix Error: Dimension mismatch during block matrix concatenation.");
        }

        return matResult;
    }

    std::any Evaluator::visitDictLiteral(DictLiteral* expr) {
        Dict d;
        for (auto& [keyExpr, valExpr] : expr->entries) {
            // ★ key 求值并转换为字符串
            std::string key;
            Value keyVal = evaluate(keyExpr.get());
            if (std::holds_alternative<std::string>(keyVal.data)) {
                key = std::get<std::string>(keyVal.data);
            }
            else {
                std::ostringstream oss;
                oss << keyVal;
                key = oss.str();
            }
            // ★ value 求值
            Value val = evaluate(valExpr.get());
            d.set(key, std::make_any<Value>(val));
        }
        return Value(d);
    }

    std::any Evaluator::visitFunctionDef(FunctionDef* expr) {
        const std::string& funcName = expr->name.lexeme;
        int numParams = static_cast<int>(expr->params.size());
        assertNotConst(funcName);

        auto arityIt = builtinArity.find(funcName);
        if (arityIt != builtinArity.end() && !arityIt->second.empty()) {
            if (arityIt->second.count(numParams))
                throw std::runtime_error("Runtime Error: Cannot redefine built-in function '" +
                    funcName + "()' with " + std::to_string(numParams) +
                    " parameter(s). Use a different name, or a different number of parameters.");
        }

        std::vector<std::string> pNames;
        std::vector<bool> pIsRef;
        for (size_t ii = 0; ii < expr->params.size(); ++ii) {
            pNames.push_back(expr->params[ii].lexeme);
            pIsRef.push_back(expr->paramIsRef[ii]);
        }

        // ★ 函数内定义时捕获环境
        std::any capturedAny;
        if (functionDepth > 0) {
            CapturedEnv captured;
            for (const auto& [k, v] : environment) {
                bool isParam = false;
                for (const auto& p : pNames)
                    if (k == p) { isParam = true; break; }
                if (!isParam && k != funcName) captured[k] = v;
            }
            capturedAny = std::make_any<CapturedEnv>(std::move(captured));
        }

        auto closure = std::make_shared<FunctionClosure>(
            pNames, pIsRef, expr->rawBody, expr->body, std::move(capturedAny));

        // ★ 求值默认参数表达式（定义时求值，Python 语义）
        closure->defaultValues.resize(pNames.size(), Value::none());
        for (size_t ii = 0; ii < expr->defaultExprs.size(); ++ii) {
            if (expr->defaultExprs[ii]) {
                closure->defaultValues[ii] = evaluate(expr->defaultExprs[ii].get());
            }
        }
        validateDefaultOrder(pNames, closure->defaultValues, expr->name.lexeme);

        Value val(closure);

        // ★ 自引用
        if (closure->hasCaptures()) {
            auto& cap = std::any_cast<CapturedEnv&>(closure->capturedEnv);
            cap[funcName] = val;
        }

        environment[funcName] = val;
        return val;
    }

    // =================================================================
    // 控制流
    // =================================================================
    std::any Evaluator::visitBlock(Block* expr) {
        // ★ Python 风格：块不创建作用域，仅顺序执行
        Value result = Value(0.0);
        for (auto& stmt : expr->statements) {
            result = evaluate(stmt.get());
        }
        return result;
    }

    std::any Evaluator::visitIfExpr(IfExpr* expr) {
        Value cond = evaluate(expr->condition.get());
        if (isTruthy(cond)) return evaluate(expr->thenBranch.get());
        if (expr->elseBranch) return evaluate(expr->elseBranch.get());
        return Value(0.0);
    }

    std::any Evaluator::visitWhileExpr(WhileExpr* expr) {
        constexpr int MAX_ITERATIONS = 10000000;
        int count = 0;
        // ★ 不再 pushScope/popScope
        while (true) {
            Value cond = evaluate(expr->condition.get());
            if (!isTruthy(cond)) break;
            if (++count > MAX_ITERATIONS)
                throw std::runtime_error("Runtime Error: Loop exceeded " +
                    std::to_string(MAX_ITERATIONS) + " iterations.");
            try { evaluate(expr->body.get()); }
            catch (BreakSignal&) { break; }
            catch (ContinueSignal&) { continue; }
        }
        return Value::none();
    }

    std::any Evaluator::visitForExpr(ForExpr* expr) {
        constexpr int MAX_ITERATIONS = 10000000;
        int count = 0;
        // ★ 不再 pushScope/popScope
        evaluate(expr->initializer.get());
        while (true) {
            Value cond = evaluate(expr->condition.get());
            if (!isTruthy(cond)) break;
            if (++count > MAX_ITERATIONS)
                throw std::runtime_error("Runtime Error: Loop exceeded " +
                    std::to_string(MAX_ITERATIONS) + " iterations.");
            try { evaluate(expr->body.get()); }
            catch (BreakSignal&) { break; }
            catch (ContinueSignal&) { /* fall through */ }
            evaluate(expr->update.get());
        }
        return Value::none();
    }

    std::any Evaluator::visitBreakExpr(BreakExpr*) { throw BreakSignal{}; }
    std::any Evaluator::visitContinueExpr(ContinueExpr*) { throw ContinueSignal{}; }
    std::any Evaluator::visitReturnExpr(ReturnExpr* expr) {
        Value val = expr->value ? evaluate(expr->value.get()) : Value::none();
        throw ReturnSignal{ val };
    }

    std::any Evaluator::visitConstDecl(ConstDecl* expr) {
        const std::string& name = expr->name.lexeme;
        if (constVars.count(name))
            throw std::runtime_error("Runtime Error: '" + name + "' is already declared as const.");
        Value val = evaluate(expr->value.get());

        // ★ 函数内 auto-local（与 visitAssign 一致）
        if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name)) {
            declareLocal(name);
        }

        environment[name] = val;
        constVars.insert(name);
        return val;
    }

    std::any Evaluator::visitIndexAccess(IndexAccess* expr) {
        Value obj = evaluate(expr->object.get());
        // ★ Instance __getitem__
        if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
            if (inst->classDef->methods.count("__getitem__")) {
                std::vector<Value> idxArgs;
                for (auto& ie : expr->indices) idxArgs.push_back(evaluate(ie.get())); // 强行作为独立标量！
                return callInstanceMethod(inst, "__getitem__", idxArgs);
            }
        }
        return readSingleIndex(obj, expr->indices);
    }

    std::any Evaluator::visitIndexAssign(IndexAssign* expr) {
        Value val = evaluate(expr->value.get());
        int depth = static_cast<int>(expr->indexChain.size());

        if (expr->hasObjectExpr()) {
            Value container = evaluate(expr->objectExpr.get());
            if (depth == 1) {
                writeSingleIndex(container, expr->indexChain[0], val);
            }
            else {
                std::vector<Value> chain;
                chain.push_back(container);
                for (int i = 0; i < depth - 1; ++i)
                    chain.push_back(readSingleIndex(chain.back(), expr->indexChain[i]));
                writeSingleIndex(chain.back(), expr->indexChain[depth - 1], val);
                for (int i = depth - 2; i >= 0; --i)
                    writeSingleIndex(chain[i], expr->indexChain[i], chain[i + 1]);
                container = chain[0];
            }
            if (auto* dotExpr = dynamic_cast<DotAccess*>(expr->objectExpr.get())) {
                Value obj = evaluate(dotExpr->object.get());
                if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                    std::get<std::shared_ptr<Instance>>(obj.data)->fields.set(dotExpr->field.lexeme, std::make_any<Value>(container));
                }
            }
            if (auto* varExpr = dynamic_cast<Variable*>(expr->objectExpr.get())) {
                environment[varExpr->name.lexeme] = container;
            }
            return val;
        }

        assertNotConst(expr->name.lexeme);
        auto it = environment.find(expr->name.lexeme);
        if (it == environment.end()) throw std::runtime_error("Runtime Error: Undefined variable '" + expr->name.lexeme + "'.");

        if (depth == 1) {
            writeSingleIndex(it->second, expr->indexChain[0], val);
            return val;
        }

        std::vector<Value> chain;
        chain.push_back(it->second);
        for (int i = 0; i < depth - 1; ++i)
            chain.push_back(readSingleIndex(chain.back(), expr->indexChain[i]));

        writeSingleIndex(chain.back(), expr->indexChain[depth - 1], val);
        for (int i = depth - 2; i >= 0; --i)
            writeSingleIndex(chain[i], expr->indexChain[i], chain[i + 1]);
        it->second = chain[0];
        return val;
    }

    std::any Evaluator::visitDeleteExpr(DeleteExpr* expr) {
        // ★ 只有系统内置常量不可删除，用户自定义 const 可以删
        static const std::set<std::string> systemConsts = { "PI", "E", "i", "I" };

        for (const auto& tok : expr->names) {
            const std::string& name = tok.lexeme;

            if (systemConsts.count(name))
                throw std::runtime_error("Runtime Error: Cannot delete system constant '" + name + "'.");

            auto it = environment.find(name);
            if (it == environment.end())
                throw std::runtime_error("Runtime Error: Undefined variable '" + name + "'.");

            environment.erase(it);
            constVars.erase(name);   // ★ 同步清除 const 标记，防止残留
        }
        return Value::none();
    }

    std::any Evaluator::visitCompoundAssign(CompoundAssign* expr) {
        Value rhs = evaluate(expr->value.get());

        auto applyOp = [](const Value& lhs, const Value& rhs, TokenType op) -> Value {
            switch (op) {
            case TokenType::PLUS:    return lhs + rhs;
            case TokenType::MINUS:   return lhs - rhs;
            case TokenType::STAR:    return lhs * rhs;
            case TokenType::SLASH:   return lhs / rhs;
            case TokenType::PERCENT: return lhs % rhs;
            case TokenType::CARET:   return lhs ^ rhs;
            default: throw std::runtime_error("Runtime Error: Unknown compound operator.");
            }
            };

        // ── 情况 1：x += e ──
        if (auto* var = dynamic_cast<Variable*>(expr->target.get())) {
            const std::string& name = var->name.lexeme;
            assertNotConst(name);
            auto it = environment.find(name);
            if (it == environment.end())
                throw std::runtime_error("Runtime Error: Undefined variable '" + name + "'.");
            if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name)) {
                declareLocal(name);
            }
            Value result = applyOp(it->second, rhs, expr->op);
            it->second = result;
            return result;
        }

        // ── 情况 2：A[i,j] += e  或  d["key"] += e ──
        if (auto* idxNode = dynamic_cast<IndexAccess*>(expr->target.get())) {
            // 递归展开 IndexAccess 链
            std::vector<std::vector<std::unique_ptr<Expr>>> chain;

            // 直接利用已修改为支持表达式根的 IndexAssign 基础设施！
            // 把 x[i] += e 转换语义：x[i] = x[i] + e
            // 1. 我们先使用 visitIndexAccess 的逻辑读取当前值
            Value currentVal = std::any_cast<Value>(visitIndexAccess(idxNode));

            // 2. 计算新值
            Value computedVal = applyOp(currentVal, rhs, expr->op);

            // 3. 构建一个临时等效的 IndexAssign 节点并求值来写回！
            // 因为 AST 节点层层嵌套所有权复杂，我们直接手动复刻 IndexAssign 的核心回写逻辑。

            // 重新解析出所有裸指针链
            std::vector<std::vector<Expr*>> rawChain;
            Expr* currentObj = idxNode;
            while (auto* ia = dynamic_cast<IndexAccess*>(currentObj)) {
                std::vector<Expr*> level;
                for (auto& ie : ia->indices) level.push_back(ie.get());
                rawChain.push_back(level);
                currentObj = ia->object.get();
            }
            std::reverse(rawChain.begin(), rawChain.end());

            // 评估所有索引值（因为写回本质上也是单点索引）
            std::vector<std::vector<Value>> evalIndices;
            for (auto& group : rawChain) {
                std::vector<Value> idxGroup;
                for (auto* ie : group) idxGroup.push_back(evaluate(ie));
                evalIndices.push_back(std::move(idxGroup));
            }

            int depth = static_cast<int>(evalIndices.size());

            // 这里我们需要借助旧版基于 Value 的 read/writeSingleIndex 逻辑，
            // 既然我们之前把它改成了基于 Expr* 的切片引擎，对于赋值标量，
            // 最好的办法是我们在内部搞一个专用于标量回写的辅助 lambda。
            // 为了优雅解决，我们可以先求根。
            List tempContainer;
            // 重新获取根变量（变量或 DotAccess）
            Value rootValue = evaluate(currentObj);

            auto scalarWrite = [](Value& container, const std::vector<Value>& indices, const Value& val) {
                if (std::holds_alternative<Dict>(container.data)) {
                    std::string key = std::holds_alternative<std::string>(indices[0].data) ?
                        std::get<std::string>(indices[0].data) : indices[0].toJC2Expression();
                    std::get<Dict>(container.data).set(key, std::make_any<Value>(val));
                    return;
                }
                if (std::holds_alternative<List>(container.data)) {
                    int idx = static_cast<int>(std::round(indices[0].asDouble()));
                    std::get<List>(container.data).set(idx, std::make_any<Value>(val));
                    return;
                }
                auto processMatWrite = [&](auto& m) {
                    int r = -1, c = -1;
                    if (indices.size() == 2) {
                        r = static_cast<int>(std::round(indices[0].asDouble()));
                        c = static_cast<int>(std::round(indices[1].asDouble()));
                        if (r < 0) r = m.getRows() + r;
                        if (c < 0) c = m.getCols() + c;
                    }
                    else {
                        int idx = static_cast<int>(std::round(indices[0].asDouble()));
                        if (m.getCols() == 1) { r = (idx < 0 ? m.getRows() + idx : idx); c = 0; }
                        else if (m.getRows() == 1) { r = 0; c = (idx < 0 ? m.getCols() + idx : idx); }
                    }
                    if constexpr (std::is_same_v<std::decay_t<decltype(m)>, RealMatrix>) m(r, c) = val.asDouble();
                    else if constexpr (std::is_same_v<std::decay_t<decltype(m)>, ComplexMatrix>) m(r, c) = val.asComplex();
                    else m(r, c) = std::get<std::string>(val.data);
                    };
                if (std::holds_alternative<RealMatrix>(container.data)) {
                    if (std::holds_alternative<Complex>(val.data) || std::holds_alternative<ComplexMatrix>(val.data)) {
                        ComplexMatrix cm = std::get<RealMatrix>(container.data).toComplexMatrix();
                        processMatWrite(cm);
                        container = Value(cm);
                        return;
                    }
                    processMatWrite(std::get<RealMatrix>(container.data)); return;
                }
                if (std::holds_alternative<ComplexMatrix>(container.data)) { processMatWrite(std::get<ComplexMatrix>(container.data)); return; }
                if (std::holds_alternative<StringMatrix>(container.data)) { processMatWrite(std::get<StringMatrix>(container.data)); return; }
                };

            auto scalarRead = [](const Value& container, const std::vector<Value>& indices) -> Value {
                if (std::holds_alternative<Dict>(container.data)) {
                    std::string key = std::holds_alternative<std::string>(indices[0].data) ?
                        std::get<std::string>(indices[0].data) : indices[0].toJC2Expression();
                    return std::any_cast<Value>(*std::get<Dict>(container.data).get(key));
                }
                if (std::holds_alternative<List>(container.data)) {
                    int idx = static_cast<int>(std::round(indices[0].asDouble()));
                    return std::any_cast<Value>(std::get<List>(container.data).at(idx));
                }
                if (std::holds_alternative<std::string>(container.data)) {
                    int idx = static_cast<int>(std::round(indices[0].asDouble()));
                    const std::string& s = std::get<std::string>(container.data);
                    if (idx < 0) idx = static_cast<int>(s.size()) + idx;
                    return Value(std::string(1, s[idx]));
                }
                auto processMatRead = [&](const auto& m) -> Value {
                    int r = -1, c = -1;
                    if (indices.size() == 2) {
                        r = static_cast<int>(std::round(indices[0].asDouble()));
                        c = static_cast<int>(std::round(indices[1].asDouble()));
                        if (r < 0) r = m.getRows() + r;
                        if (c < 0) c = m.getCols() + c;
                    }
                    else {
                        int idx = static_cast<int>(std::round(indices[0].asDouble()));
                        if (static_cast<int>(m.getCols()) == 1) { r = (idx < 0 ? static_cast<int>(m.getRows()) + idx : idx); c = 0; }
                        else if (static_cast<int>(m.getRows()) == 1) { r = 0; c = (idx < 0 ? static_cast<int>(m.getCols()) + idx : idx); }
                        else { r = idx; c = 0; } // Fallback for rows
                    }
                    if (r >= 0 && c >= 0) return Value(m(r, c));
                    return Value(m.getRow(r));
                    };
                if (std::holds_alternative<RealMatrix>(container.data)) return processMatRead(std::get<RealMatrix>(container.data));
                if (std::holds_alternative<ComplexMatrix>(container.data)) return processMatRead(std::get<ComplexMatrix>(container.data));
                if (std::holds_alternative<StringMatrix>(container.data)) return processMatRead(std::get<StringMatrix>(container.data));
                return Value(0.0);
                };

            // Dunder Instance Check on root
            if (std::holds_alternative<std::shared_ptr<Instance>>(rootValue.data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(rootValue.data);
                if (inst->classDef->methods.count("__setitem__")) {
                    std::vector<Value> margs;
                    for (auto& group : evalIndices) for (auto& idx : group) margs.push_back(idx);
                    margs.push_back(computedVal);
                    callInstanceMethod(inst, "__setitem__", margs);
                    return computedVal;
                }
            }

            if (depth == 1) {
                scalarWrite(rootValue, evalIndices[0], computedVal);
            }
            else {
                std::vector<Value> tempChain;
                tempChain.push_back(rootValue);
                for (int i = 0; i < depth - 1; ++i)
                    tempChain.push_back(scalarRead(tempChain.back(), evalIndices[i]));
                scalarWrite(tempChain.back(), evalIndices[depth - 1], computedVal);
                for (int i = depth - 2; i >= 0; --i)
                    scalarWrite(tempChain[i], evalIndices[i], tempChain[i + 1]);
                rootValue = tempChain[0];
            }

            // 写回 root 变量
            if (auto* varExpr = dynamic_cast<Variable*>(currentObj)) {
                environment[varExpr->name.lexeme] = rootValue;
            }
            else if (auto* dotExpr = dynamic_cast<DotAccess*>(currentObj)) {
                Value dObj = evaluate(dotExpr->object.get());
                if (std::holds_alternative<std::shared_ptr<Instance>>(dObj.data)) {
                    std::get<std::shared_ptr<Instance>>(dObj.data)->fields.set(dotExpr->field.lexeme, std::make_any<Value>(rootValue));
                }
                else if (auto* varObj = dynamic_cast<Variable*>(dotExpr->object.get())) {
                    auto envIt = environment.find(varObj->name.lexeme);
                    if (envIt != environment.end() && std::holds_alternative<Dict>(envIt->second.data)) {
                        std::get<Dict>(envIt->second.data).set(dotExpr->field.lexeme, std::make_any<Value>(rootValue));
                    }
                }
            }
            return computedVal;
        }

        // ★ 情况 3：obj.field += e
        if (auto* dotNode = dynamic_cast<DotAccess*>(expr->target.get())) {
            Value obj = evaluate(dotNode->object.get());
            const std::string& field = dotNode->field.lexeme;

            if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
                auto* fval = inst->fields.get(field);
                if (!fval) throw std::runtime_error("Runtime Error: Field '" + field + "' not found.");
                Value curVal = std::any_cast<Value>(*fval);
                Value result = applyOp(curVal, rhs, expr->op);
                inst->fields.set(field, std::make_any<Value>(result));
                return result;
            }

            // Dict compound assign (variable root only)
            if (auto* varExpr = dynamic_cast<Variable*>(dotNode->object.get())) {
                auto envIt = environment.find(varExpr->name.lexeme);
                if (envIt != environment.end() && std::holds_alternative<Dict>(envIt->second.data)) {
                    auto& d = std::get<Dict>(envIt->second.data);
                    auto* fval = d.get(field);
                    if (!fval) throw std::runtime_error("Runtime Error: Key '" + field + "' not found.");
                    Value curVal = std::any_cast<Value>(*fval);
                    Value result = applyOp(curVal, rhs, expr->op);
                    d.set(field, std::make_any<Value>(result));
                    return result;
                }
            }
            throw std::runtime_error("Runtime Error: Cannot compound-assign field on this type.");
        }
        throw std::runtime_error("Runtime Error: Invalid compound assignment target.");
    }

    std::any Evaluator::visitLambdaExpr(LambdaExpr* expr) {
        std::vector<std::string> pNames;
        for (const auto& p : expr->params)
            pNames.push_back(p.lexeme);
        std::vector<bool> pIsRef(pNames.size(), false);

        // ★ 捕获当前环境
        CapturedEnv captured;
        for (const auto& [k, v] : environment) {
            bool isParam = false;
            for (const auto& p : pNames)
                if (k == p) { isParam = true; break; }
            if (!isParam) captured[k] = v;
        }

        auto closure = std::make_shared<FunctionClosure>(
            pNames, pIsRef, expr->rawBody, expr->body,
            std::make_any<CapturedEnv>(std::move(captured)));       // ★ 包装成 any

        // ★ 求值默认参数表达式
        closure->defaultValues.resize(pNames.size(), Value::none());
        for (size_t ii = 0; ii < expr->defaultExprs.size(); ++ii) {
            if (expr->defaultExprs[ii]) {
                closure->defaultValues[ii] = evaluate(expr->defaultExprs[ii].get());
            }
        }
        validateDefaultOrder(pNames, closure->defaultValues, "<lambda>");

        return Value(closure);
    }

    std::any Evaluator::visitInvokeExpr(InvokeExpr* expr) {
        Value calleeVal = evaluate(expr->callee.get());
        if (!std::holds_alternative<std::shared_ptr<FunctionClosure>>(calleeVal.data))
            throw std::runtime_error("Runtime Error: Expression is not callable.");

        auto closure = std::get<std::shared_ptr<FunctionClosure>>(calleeVal.data);
        if (closure->isNative()) {
            std::vector<Value> args;
            for (auto& argExpr : expr->arguments)
                args.push_back(evaluate(argExpr.get()));
            auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
            return fn(args);
        }
        std::vector<Value> args;
        for (auto& argExpr : expr->arguments)
            args.push_back(evaluate(argExpr.get()));

        try {
            args = fillDefaults(closure, std::move(args));
        }
        catch (...) {
            recursionDepth--;
            throw;
        }

        if (++recursionDepth > MAX_RECURSION_DEPTH) {
            recursionDepth = 0;
            throw std::runtime_error("Runtime Error: Stack Overflow!");
        }

        pushScope();
        for (const auto& pName : closure->paramNames)
            scopeStack.back().globalNames.insert(pName);

        auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);

        functionDepth++;
        Value result;
        {
            EnvGuard guard(environment, closure->paramNames, args);
            try {
                try { result = evaluate(closure->body.get()); }
                catch (ReturnSignal& sig) { result = sig.value; }
            }
            catch (...) {
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                recursionDepth--;
                throw;
            }
        }
        functionDepth--;
        restoreCaptures(environment, capSaved);
        popScope();
        recursionDepth--;
        return result;
    }

    std::any Evaluator::visitGlobalDecl(GlobalDecl* expr) {
        if (functionDepth == 0) {
            // 顶层 global 无意义，静默忽略
            return Value::none();
        }
        for (const auto& tok : expr->names) {
            declareGlobal(tok.lexeme);
        }
        return Value::none();
    }

    std::any Evaluator::visitForInExpr(ForInExpr* expr) {
        constexpr int MAX_ITERATIONS = 10000000;
        Value iterVal = evaluate(expr->iterable.get());

        // ★ 统一的变量设置器：支持单变量和解构两种模式
        auto setupVars = [&](const Value& elemVal) {
            if (expr->isDestruct()) {
                auto elems = extractElements(elemVal);
                if (elems.size() != expr->destructNames.size())
                    throw std::runtime_error("Runtime Error: Destructuring size mismatch in for-in: expected " +
                        std::to_string(expr->destructNames.size()) + " elements, got " +
                        std::to_string(elems.size()) + ".");
                for (size_t i = 0; i < elems.size(); ++i) {
                    const std::string& name = expr->destructNames[i].lexeme;
                    if (name == "_") continue;
                    if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name))
                        declareLocal(name);
                    environment[name] = elems[i];
                }
            }
            else {
                const std::string& varName = expr->varName.lexeme;
                if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(varName))
                    declareLocal(varName);
                environment[varName] = elemVal;
            }
            };

        int count = 0;

        // ── RealMatrix ──
        if (std::holds_alternative<RealMatrix>(iterVal.data)) {
            const auto& m = std::get<RealMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(0, j)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else if (m.getCols() == 1) {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(ii, 0)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m.getRow(ii)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
        }
        // ── ComplexMatrix ──
        else if (std::holds_alternative<ComplexMatrix>(iterVal.data)) {
            const auto& m = std::get<ComplexMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(0, j)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else if (m.getCols() == 1) {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(ii, 0)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m.getRow(ii)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
        }
        // ── String ──
        else if (std::holds_alternative<std::string>(iterVal.data)) {
            const auto& s = std::get<std::string>(iterVal.data);
            for (size_t ii = 0; ii < s.size(); ++ii) {
                if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                setupVars(Value(std::string(1, s[ii])));
                try { evaluate(expr->body.get()); }
                catch (BreakSignal&) { goto done; }
                catch (ContinueSignal&) { continue; }
            }
        }
        // ── StringMatrix ──
        else if (std::holds_alternative<StringMatrix>(iterVal.data)) {
            const auto& m = std::get<StringMatrix>(iterVal.data);
            if (m.getRows() == 1) {
                for (int j = 0; j < m.getCols(); ++j) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(0, j)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else if (m.getCols() == 1) {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m(ii, 0)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
            else {
                for (int ii = 0; ii < m.getRows(); ++ii) {
                    if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                    setupVars(Value(m.getRow(ii)));
                    try { evaluate(expr->body.get()); }
                    catch (BreakSignal&) { goto done; }
                    catch (ContinueSignal&) { continue; }
                }
            }
        }
        // ── List ──
        else if (std::holds_alternative<List>(iterVal.data)) {
            const auto& L = std::get<List>(iterVal.data);
            for (size_t ii = 0; ii < L.size(); ++ii) {
                if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                setupVars(std::any_cast<Value>(L.raw()[ii]));
                try { evaluate(expr->body.get()); }
                catch (BreakSignal&) { goto done; }
                catch (ContinueSignal&) { continue; }
            }
        }
        // ── Dict ──
        else if (std::holds_alternative<Dict>(iterVal.data)) {
            const auto& d = std::get<Dict>(iterVal.data);
            for (const auto& [key, val] : d.getEntries()) {
                if (++count > MAX_ITERATIONS) throw std::runtime_error("Runtime Error: for-in loop exceeded iteration limit.");
                // ★ 解构模式下，把 key 和 value 打包成 List
                if (expr->isDestruct()) {
                    List pair;
                    pair.push_back(std::make_any<Value>(Value(key)));
                    pair.push_back(std::make_any<Value>(std::any_cast<Value>(val)));
                    setupVars(Value(pair));
                }
                else {
                    setupVars(Value(key));
                }
                try { evaluate(expr->body.get()); }
                catch (BreakSignal&) { goto done; }
                catch (ContinueSignal&) { continue; }
            }
        }
        else {
            throw std::runtime_error("Type Error: for-in requires an iterable (array, vector, matrix, string, list, or dict).");
        }

    done:
        return Value::none();
    }

    std::any Evaluator::visitThrowExpr(ThrowExpr* expr) {
        Value val = evaluate(expr->value.get());
        std::string msg;
        if (std::holds_alternative<std::string>(val.data)) {
            msg = std::get<std::string>(val.data);
        }
        else {
            std::ostringstream oss;
            oss << val;
            msg = oss.str();
        }
        throw ErrorSignal{ msg };
    }

    std::any Evaluator::visitTryCatchExpr(TryCatchExpr* expr) {
        try {
            return evaluate(expr->tryBody.get());
        }
        // ★ 控制流信号必须穿透，不能被 catch 拦截
        catch (BreakSignal&) { throw; }
        catch (ContinueSignal&) { throw; }
        catch (ReturnSignal&) { throw; }
        // ★ 用户 throw
        catch (ErrorSignal& sig) {
            if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(expr->catchName.lexeme))
                declareLocal(expr->catchName.lexeme);
            environment[expr->catchName.lexeme] = Value(sig.message);
            return evaluate(expr->catchBody.get());
        }
        // ★ 系统运行时错误（除零、越界、类型错误等）
        catch (const std::exception& ex) {
            if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(expr->catchName.lexeme))
                declareLocal(expr->catchName.lexeme);
            environment[expr->catchName.lexeme] = Value(std::string(ex.what()));
            return evaluate(expr->catchBody.get());
        }
    }

    std::any Evaluator::visitImportExpr(ImportExpr* expr) {
        Value pathVal = evaluate(expr->path.get());
        if (!std::holds_alternative<std::string>(pathVal.data))
            throw std::runtime_error("Runtime Error: import expects a string path.");
        std::string filepath = std::get<std::string>(pathVal.data);
        // ★ 优先检查 C++ 原生模块
        if (loadNativeModule(filepath)) {
            return Value::none();
        }
        // ★ 原有的 .jc2 文件搜索逻辑继续...
        namespace fs = std::filesystem;

        std::string baseDir = cwd();
        std::string wsDir = getWorkspacePath();
        std::string exDir = getExeDir();
        std::string resolved = filepath;

        if (!fs::exists(resolved) && !fs::path(filepath).is_absolute()) {
            std::string withExt = filepath + ".jc2";
            std::vector<std::string> candidates = {
                // 1. 脚本所在目录
                (fs::path(baseDir) / filepath).string(),
                (fs::path(baseDir) / withExt).string(),
                (fs::path(baseDir) / "data" / filepath).string(),
                (fs::path(baseDir) / "data" / withExt).string(),
                (fs::path(baseDir) / "lib" / filepath).string(),
                (fs::path(baseDir) / "lib" / withExt).string(),
                // 2. 工作区目录
                (fs::path(wsDir) / filepath).string(),
                (fs::path(wsDir) / withExt).string(),
                // 3. exe 所在目录（全局兜底）
                (fs::path(exDir) / filepath).string(),
                (fs::path(exDir) / withExt).string(),
                (fs::path(exDir) / "data" / filepath).string(),
                (fs::path(exDir) / "data" / withExt).string(),
                (fs::path(exDir) / "lib" / filepath).string(),
                (fs::path(exDir) / "lib" / withExt).string(),
            };

            resolved.clear();
            for (const auto& c : candidates) {
                if (fs::exists(c)) { resolved = c; break; }
            }

            if (resolved.empty())
                throw std::runtime_error("Import Error: Cannot find '" + filepath + "'.");
        }

        std::string canonical = fs::weakly_canonical(resolved).string();
        if (importedFiles.count(canonical))
            return Value::none();
        importedFiles.insert(canonical);

        std::ifstream file(resolved);
        if (!file.is_open())
            throw std::runtime_error("Import Error: Cannot open '" + filepath + "'.");

        std::string importDir = fs::path(canonical).parent_path().string();
        pushScriptDir(importDir);

        std::string rawLine;
        while (std::getline(file, rawLine)) {
            size_t s = rawLine.find_first_not_of(" \t");
            size_t e = rawLine.find_last_not_of(" \t\r\n");
            if (s == std::string::npos) continue;
            std::string line = rawLine.substr(s, e - s + 1);
            if (line.empty()) continue;
            if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

            {
                size_t cp = line.find("//");
                if (cp != std::string::npos) line = line.substr(0, cp);
                size_t ss = line.find_first_not_of(" \t");
                if (ss == std::string::npos) continue;
                line = line.substr(ss);
                size_t ee = line.find_last_not_of(" \t");
                line = line.substr(0, ee + 1);
                if (line.empty()) continue;
            }

            {
                int braces = 0, parens = 0, brackets = 0;
                for (char c : line) {
                    if (c == '{') braces++; else if (c == '}') braces--;
                    if (c == '(') parens++; else if (c == ')') parens--;
                    if (c == '[') brackets++; else if (c == ']') brackets--;
                }
                while ((braces > 0 || parens > 0 || brackets > 0 || endsWithContinuation(line)) && std::getline(file, rawLine)) {
                    size_t cp = rawLine.find("//");
                    std::string stripped = (cp != std::string::npos) ? rawLine.substr(0, cp) : rawLine;
                    line += " " + stripped;
                    for (char c : stripped) {
                        if (c == '{') braces++; else if (c == '}') braces--;
                        if (c == '(') parens++; else if (c == ')') parens--;
                        if (c == '[') brackets++; else if (c == ']') brackets--;
                    }
                }
            }

            Lexer lexer(line);
            auto tokens = lexer.tokenize();
            Parser parser(tokens);
            auto ast = parser.parse();
            evaluate(ast.get());
        }

        file.close();
        popScriptDir();
        return Value::none();
    }

    std::any Evaluator::visitSwitchExpr(SwitchExpr* expr) {
        Value subject = evaluate(expr->subject.get());

        for (auto& [values, body] : expr->cases) {
            for (auto& valExpr : values) {
                Value caseVal = evaluate(valExpr.get());
                if (valuesEqual(subject, caseVal)) {
                    return evaluate(body.get());
                }
            }
        }

        if (expr->defaultBody) {
            return evaluate(expr->defaultBody.get());
        }

        return Value(0.0);
    }

    std::any Evaluator::visitClassDefExpr(ClassDefExpr* expr) {
        auto classDef = std::make_shared<ClassDefinition>();
        classDef->name = expr->name.lexeme;

        // ★ 解析父类
        if (!expr->superClassName.empty()) {
            auto parentIt = environment.find(expr->superClassName);
            if (parentIt == environment.end() ||
                !std::holds_alternative<std::shared_ptr<ClassDefinition>>(parentIt->second.data))
                throw std::runtime_error("Runtime Error: Parent class '" +
                    expr->superClassName + "' is not defined.");
            classDef->parent = std::get<std::shared_ptr<ClassDefinition>>(parentIt->second.data);
        }

        for (auto& md : expr->methods) {
            std::vector<std::string> pNames;
            std::vector<bool> pIsRef;
            for (size_t i = 0; i < md.params.size(); ++i) {
                pNames.push_back(md.params[i].lexeme);
                pIsRef.push_back(md.paramIsRef[i]);
            }
            auto closure = std::make_shared<FunctionClosure>(
                pNames, pIsRef, md.rawBody, md.body);

            closure->defaultValues.resize(pNames.size(), Value::none());
            for (size_t ii = 0; ii < md.defaultExprs.size(); ++ii) {
                if (md.defaultExprs[ii]) {
                    closure->defaultValues[ii] = evaluate(md.defaultExprs[ii].get());
                }
            }
            validateDefaultOrder(pNames, closure->defaultValues, md.name.lexeme);

            classDef->methods[md.name.lexeme] = closure;
        }

        Value val(classDef);
        environment[expr->name.lexeme] = val;
        return val;
    }

    std::any Evaluator::visitDotAccess(DotAccess* expr) {
        Value obj = evaluate(expr->object.get());
        const std::string& field = expr->field.lexeme;

        // ═══ SuperProxy: super.field / super.method ═══
        if (obj.isSuperProxy()) {
            auto proxy = obj.asSuperProxy();
            auto [closure, owningClass] = resolveMethod(proxy.parentClass, field);
            if (closure) {
                // 返回绑定方法
                auto bound = std::make_shared<FunctionClosure>(*closure);
                using CapturedEnv = std::map<std::string, Value>;
                CapturedEnv cap;
                if (bound->hasCaptures()) {
                    try { cap = std::any_cast<CapturedEnv>(bound->capturedEnv); }
                    catch (...) {}
                }
                cap["self"] = Value(proxy.instance);
                cap["__class__"] = Value(owningClass);
                bound->capturedEnv = std::make_any<CapturedEnv>(std::move(cap));
                return Value(bound);
            }
            throw std::runtime_error("Runtime Error: Parent class has no method '" +
                field + "'.");
        }

        // ═══ Instance 字段 / 绑定方法 ═══
        if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);

            // 1) 查字段
            auto* fval = inst->fields.get(field);
            if (fval) return std::any_cast<Value>(*fval);

            // 2) 查类方法（含继承链）→ 返回绑定方法
            auto [closure, owningClass] = resolveMethod(inst->classDef, field);
            if (closure) {
                auto bound = std::make_shared<FunctionClosure>(*closure);
                using CapturedEnv = std::map<std::string, Value>;
                CapturedEnv cap;
                if (bound->hasCaptures()) {
                    try { cap = std::any_cast<CapturedEnv>(bound->capturedEnv); }
                    catch (...) {}
                }
                cap["self"] = Value(inst);
                cap["__class__"] = Value(owningClass);
                bound->capturedEnv = std::make_any<CapturedEnv>(std::move(cap));
                return Value(bound);
            }

            throw std::runtime_error("Runtime Error: '" + inst->classDef->name +
                "' instance has no field or method '" + field + "'.");
        }

        // ═══ Dict ═══
        if (std::holds_alternative<Dict>(obj.data)) {
            const auto& d = std::get<Dict>(obj.data);
            const auto* val = d.get(field);
            if (!val)
                throw std::runtime_error("Runtime Error: Key '" + field + "' not found in Dict.");
            return std::any_cast<Value>(*val);
        }

        // ═══ ClassDefinition 静态方法 ═══
        if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(obj.data)) {
            auto cls = std::get<std::shared_ptr<ClassDefinition>>(obj.data);
            auto [closure, _] = resolveMethod(cls, field);
            if (closure) return Value(closure);
            throw std::runtime_error("Runtime Error: Class '" + cls->name +
                "' has no method '" + field + "'.");
        }

        throw std::runtime_error("Type Error: '.' operator requires an instance, dict, or class.");
    }

    std::any Evaluator::visitDotAssign(DotAssign* expr) {
        Value val = evaluate(expr->value.get());
        const std::string& field = expr->field.lexeme;

        // 先求值 object
        Value obj = evaluate(expr->object.get());

        // ═══ Instance (shared_ptr 引用语义，直接修改) ═══
        if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
            inst->fields.set(field, std::make_any<Value>(val));
            return val;
        }

        // ═══ Dict (值语义，需要回写到变量) ═══
        if (auto* varExpr = dynamic_cast<Variable*>(expr->object.get())) {
            auto envIt = environment.find(varExpr->name.lexeme);
            if (envIt != environment.end()) {
                assertNotConst(varExpr->name.lexeme);
                if (std::holds_alternative<Dict>(envIt->second.data)) {
                    if (functionDepth > 0 && !scopeStack.empty() &&
                        !isGlobal(varExpr->name.lexeme))
                        declareLocal(varExpr->name.lexeme);
                    std::get<Dict>(envIt->second.data).set(field, std::make_any<Value>(val));
                    return val;
                }
            }
        }

        throw std::runtime_error("Type Error: Cannot assign field on this type.");
    }

    std::any Evaluator::visitMethodCallExpr(MethodCallExpr* expr) {
        Value obj = evaluate(expr->object.get());
        const std::string& methodName = expr->method.lexeme;

        // ═══ SuperProxy 方法调用 ═══
        if (obj.isSuperProxy()) {
            auto proxy = obj.asSuperProxy();
            std::vector<Value> args;
            for (auto& a : expr->arguments) args.push_back(evaluate(a.get()));
            return callInstanceMethod(proxy.instance, methodName, args, proxy.parentClass);
        }

        // ═══ Instance 方法调用 ═══
        if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);

            // 1) 优先检查实例字段中的可调用对象
            auto* fieldVal = inst->fields.get(methodName);
            if (fieldVal) {
                Value fv = std::any_cast<Value>(*fieldVal);
                if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(fv.data)) {
                    auto closure = std::get<std::shared_ptr<FunctionClosure>>(fv.data);
                    std::vector<Value> args;
                    for (auto& a : expr->arguments) args.push_back(evaluate(a.get()));
                    return callFunction(closure, args);
                }
            }

            // 2) 通过继承链解析，委托 callInstanceMethod
            std::vector<Value> args;
            for (auto& a : expr->arguments) args.push_back(evaluate(a.get()));
            return callInstanceMethod(inst, methodName, args);
        }

        // ═══ Dict 方法调用 ═══
        if (std::holds_alternative<Dict>(obj.data)) {
            const auto& d = std::get<Dict>(obj.data);
            const auto* fval = d.get(methodName);
            if (!fval)
                throw std::runtime_error("Runtime Error: Key '" + methodName +
                    "' not found in Dict.");
            Value fv = std::any_cast<Value>(*fval);
            if (!std::holds_alternative<std::shared_ptr<FunctionClosure>>(fv.data))
                throw std::runtime_error("Type Error: '" + methodName + "' is not callable.");
            auto closure = std::get<std::shared_ptr<FunctionClosure>>(fv.data);
            std::vector<Value> args;
            for (auto& a : expr->arguments) args.push_back(evaluate(a.get()));
            return callFunction(closure, args);
        }

        throw std::runtime_error("Type Error: Cannot call method on this type.");
    }

    // =================================================================
    // 工具方法
    // =================================================================
    void Evaluator::showVariables() const {
        if (environment.empty()) { std::cout << "No variables defined." << std::endl; return; }
        std::cout << "\n--- Defined Variables ---" << std::endl;
        for (const auto& [name, value] : environment) {
            if (name == "PI" || name == "E" || name == "i" || name == "I" ||
                name == "true" || name == "false" || name == "ANS") continue;
            std::cout << "  ";
            if (constVars.count(name)) std::cout << "(const) ";

            if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(value.data)) {
                auto cl = std::get<std::shared_ptr<FunctionClosure>>(value.data);
                std::string params;
                for (size_t ii = 0; ii < cl->paramNames.size(); ++ii) {
                    if (cl->isRef[ii]) params += "ref ";
                    params += cl->paramNames[ii];
                    if (ii < cl->paramNames.size() - 1) params += ", ";
                }
                std::string body = cl->rawBody;
                if (body.size() > 50) body = body.substr(0, 47) + "...";
                std::cout << name << "(" << params << ") = " << body << std::endl;
            }
            else {
                std::cout << name << " = " << value << std::endl;
            }
        }
        std::cout << "-------------------------" << std::endl;
    }

    void Evaluator::clearVariables() {
        environment.clear();
        constVars.clear();
        importedFiles.clear();                                       // ★
        environment["PI"] = Value(3.14159265358979323846);
        environment["E"] = Value(2.71828182845904523536);
        environment["i"] = Value(Complex(0.0, 1.0));
        environment["I"] = Value(Complex(0.0, 1.0));
        environment["true"] = Value(1.0);
        environment["false"] = Value(0.0);
    }

    void Evaluator::setVariable(const std::string& name, const Value& val) {
        environment[name] = val;
    }

    bool Evaluator::hasDunder(const Value& obj, const std::string& name) const {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) return false;
        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
        return resolveMethod(inst->classDef, name).first != nullptr;
    }

    Value Evaluator::callInstanceMethod(std::shared_ptr<Instance> inst,
        const std::string& methodName, const std::vector<Value>& args,
        std::shared_ptr<ClassDefinition> startClass)
    {
        if (!startClass) startClass = inst->classDef;
        auto [closure, owningClass] = resolveMethod(startClass, methodName);
        if (!closure)
            throw std::runtime_error("Runtime Error: No method '" + methodName +
                "' on '" + inst->classDef->name + "' instance.");

        std::vector<Value> finalArgs = fillDefaults(closure, args, methodName);

        if (++recursionDepth > MAX_RECURSION_DEPTH) {
            recursionDepth = 0;
            throw std::runtime_error("Runtime Error: Stack Overflow!");
        }

        pushScope();
        declareLocal("self");
        declareLocal("__class__");
        environment["self"] = Value(inst);
        environment["__class__"] = Value(owningClass);
        scopeStack.back().globalNames.insert("self");
        scopeStack.back().globalNames.insert("__class__");
        for (const auto& pName : closure->paramNames)
            scopeStack.back().globalNames.insert(pName);

        auto capSaved = injectCaptures(environment, closure->capturedEnv, closure->paramNames);

        functionDepth++;
        Value result;
        {
            EnvGuard guard(environment, closure->paramNames, finalArgs);
            try {
                try { result = evaluate(closure->body.get()); }
                catch (ReturnSignal& sig) { result = sig.value; }
            }
            catch (...) {
                functionDepth--;
                restoreCaptures(environment, capSaved);
                popScope();
                recursionDepth--;
                throw;
            }
        }
        functionDepth--;
        restoreCaptures(environment, capSaved);
        popScope();
        recursionDepth--;
        return result;
    }

    std::any Evaluator::visitSuperExpr(SuperExpr*) {
        auto selfIt = environment.find("self");
        if (selfIt == environment.end() ||
            !std::holds_alternative<std::shared_ptr<Instance>>(selfIt->second.data))
            throw std::runtime_error("Runtime Error: 'super' can only be used inside a method.");
        auto inst = std::get<std::shared_ptr<Instance>>(selfIt->second.data);

        auto classIt = environment.find("__class__");
        if (classIt == environment.end() ||
            !std::holds_alternative<std::shared_ptr<ClassDefinition>>(classIt->second.data))
            throw std::runtime_error("Runtime Error: 'super' requires class context.");
        auto currentClass = std::get<std::shared_ptr<ClassDefinition>>(classIt->second.data);

        if (!currentClass->parent)
            throw std::runtime_error("Runtime Error: Class '" + currentClass->name +
                "' has no parent class.");

        return Value::makeSuperProxy(inst, currentClass->parent);
    }

    std::any Evaluator::visitDestructAssign(DestructAssign* expr) {
        Value rhs = evaluate(expr->value.get());
        auto elements = extractElements(rhs);
        int n = static_cast<int>(expr->names.size());

        if (static_cast<int>(elements.size()) != n)
            throw std::runtime_error("Runtime Error: Destructuring size mismatch: expected " +
                std::to_string(n) + " elements, got " +
                std::to_string(elements.size()) + ".");

        // ★ 先提取所有值（保证 [a, b] = [b, a] 交换语义正确）
        // RHS 已经完整求值，元素已经是值拷贝，无需额外处理

        for (int i = 0; i < n; ++i) {
            const std::string& name = expr->names[i].lexeme;
            if (name == "_") continue;  // ★ 丢弃占位符
            assertNotConst(name);
            if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name))
                declareLocal(name);
            environment[name] = elements[i];
        }
        return rhs;
    }

    std::pair<std::shared_ptr<FunctionClosure>, std::shared_ptr<ClassDefinition>>
        Evaluator::resolveMethod(std::shared_ptr<ClassDefinition> cls, const std::string& name) {
        auto c = cls;
        while (c) {
            auto it = c->methods.find(name);
            if (it != c->methods.end()) return { it->second, c };
            c = c->parent;
        }
        return { nullptr, nullptr };
    }

    std::any Evaluator::visitFStringExpr(FStringExpr* expr) {
        std::ostringstream result;

        for (size_t i = 0; i < expr->exprs.size(); ++i) {
            // ★ 输出表达式前的文本段
            result << expr->literals[i];

            // ★ 求值表达式
            Value val = evaluate(expr->exprs[i].get());

            // ★ Instance __str__ 钩子
            if (std::holds_alternative<std::shared_ptr<Instance>>(val.data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(val.data);
                if (resolveMethod(inst->classDef, "__str__").first)
                    val = callInstanceMethod(inst, "__str__", {});
            }

            const std::string& spec = expr->formatSpecs[i];

            if (spec.empty()) {
                // ★ 无格式说明符：直接转字符串
                result << val;
            }
            else {
                // ★ 解析格式说明符：[对齐][宽度][.精度][类型]
                char align = '\0';
                int width = 0;
                int precision = -1;
                char type = '\0';

                size_t si = 0;
                if (si < spec.size() && (spec[si] == '<' || spec[si] == '>' || spec[si] == '^'))
                    align = spec[si++];
                while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9')
                    width = width * 10 + (spec[si++] - '0');
                if (si < spec.size() && spec[si] == '.') {
                    si++;
                    precision = 0;
                    while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9')
                        precision = precision * 10 + (spec[si++] - '0');
                }
                if (si < spec.size())
                    type = spec[si++];

                // ★ 按类型格式化
                std::ostringstream valOss;
                if (type == 'f' || type == 'e') {
                    double v = val.asDouble();
                    if (precision >= 0) valOss << std::fixed << std::setprecision(precision);
                    if (type == 'e') valOss << std::scientific;
                    valOss << v;
                }
                else if (type == 'd') {
                    valOss << static_cast<int64_t>(std::round(val.asDouble()));
                }
                else if (type == 'x') {
                    valOss << std::hex << static_cast<int64_t>(std::round(val.asDouble()));
                }
                else {
                    if (precision >= 0) valOss << std::fixed << std::setprecision(precision);
                    valOss << val;
                }

                std::string valStr = valOss.str();

                // ★ 对齐填充
                if (width > 0 && static_cast<int>(valStr.size()) < width) {
                    int pad = width - static_cast<int>(valStr.size());
                    if (align == '<')
                        valStr = valStr + std::string(pad, ' ');
                    else if (align == '^') {
                        int left = pad / 2, right = pad - left;
                        valStr = std::string(left, ' ') + valStr + std::string(right, ' ');
                    }
                    else // 默认右对齐
                        valStr = std::string(pad, ' ') + valStr;
                }

                result << valStr;
            }
        }
        // ★ 输出尾部文本段
        result << expr->literals.back();

        return Value(result.str());
    }

    std::any Evaluator::visitListCompExpr(ListCompExpr* expr) {
        std::vector<Value> results;

        // ★ 递归迭代引擎：处理嵌套 for 子句
        std::function<void(size_t)> iterate;
        iterate = [&](size_t idx) {
            if (idx >= expr->clauses.size()) {
                // 所有 for 子句耗尽 → 检查条件并收集
                if (expr->condition) {
                    Value cond = evaluate(expr->condition.get());
                    if (!isTruthy(cond)) return;
                }
                results.push_back(evaluate(expr->valueExpr.get()));
                return;
            }

            auto& clause = expr->clauses[idx];
            Value iterVal = evaluate(clause.iterable.get());

            // ★ Dict 解构特殊处理：自动生成 [key, val] 对
            if (clause.isDestruct() && std::holds_alternative<Dict>(iterVal.data)) {
                const auto& d = std::get<Dict>(iterVal.data);
                for (const auto& [key, val] : d.getEntries()) {
                    List pair;
                    pair.push_back(std::make_any<Value>(Value(key)));
                    pair.push_back(std::make_any<Value>(std::any_cast<Value>(val)));
                    auto elems = extractElements(Value(pair));
                    if (elems.size() != clause.destructNames.size())
                        throw std::runtime_error("Runtime Error: Destructuring size mismatch in comprehension.");
                    for (size_t i = 0; i < elems.size(); ++i) {
                        const std::string& name = clause.destructNames[i].lexeme;
                        if (name == "_") continue;
                        if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name))
                            declareLocal(name);
                        environment[name] = elems[i];
                    }
                    iterate(idx + 1);
                }
                return;
            }

            auto elements = getIterableElements(iterVal);

            for (auto& elem : elements) {
                if (clause.isDestruct()) {
                    auto elems = extractElements(elem);
                    if (elems.size() != clause.destructNames.size())
                        throw std::runtime_error("Runtime Error: Destructuring size mismatch in comprehension.");
                    for (size_t i = 0; i < elems.size(); ++i) {
                        const std::string& name = clause.destructNames[i].lexeme;
                        if (name == "_") continue;
                        if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name))
                            declareLocal(name);
                        environment[name] = elems[i];
                    }
                }
                else {
                    const std::string& name = clause.varName.lexeme;
                    if (functionDepth > 0 && !scopeStack.empty() && !isGlobal(name))
                        declareLocal(name);
                    environment[name] = elem;
                }
                iterate(idx + 1);
            }
            };

        iterate(0);

        // ★ 自动类型推断：尝试最紧凑的返回类型
        if (results.empty()) return Value(RealMatrix(1, 0));

        // 尝试 RealMatrix（行向量）
        {
            std::vector<double> flat;
            bool ok = true;
            for (auto& v : results) {
                try { flat.push_back(v.asDouble()); }
                catch (...) { ok = false; break; }
            }
            if (ok) return Value(RealMatrix(1, static_cast<int>(flat.size()), flat));
        }

        // 尝试 ComplexMatrix
        {
            std::vector<Complex> flat;
            bool ok = true;
            for (auto& v : results) {
                try { flat.push_back(v.asComplex()); }
                catch (...) { ok = false; break; }
            }
            if (ok) return Value(ComplexMatrix(1, static_cast<int>(flat.size()), flat));
        }

        // 异构类型 → List
        List L;
        for (auto& v : results) L.push_back(std::make_any<Value>(std::move(v)));
        return Value(L);
    }

} // namespace jc

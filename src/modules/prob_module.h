#ifndef JC2_MODULE_PROB_H
#define JC2_MODULE_PROB_H

#include "Module.h"
#include "../vm/BuiltinRegistry.h"
#include "../vm/VM.h"
#include "Probability.h"

namespace jc_prob {
    using namespace jc;

    inline ObjClass* distClass = nullptr;

    inline std::shared_ptr<Distribution>& getDist(const Value& v) {
        if (!v.isInstance())
            throw std::runtime_error("Type Error: Expected a Distribution instance.");
        auto inst = v.asInstance();
        if (!inst->nativeData.has_value())
            throw std::runtime_error("Type Error: Expected a Distribution native object.");
        return std::any_cast<std::shared_ptr<Distribution>&>(inst->nativeData);
    }

    inline Value makeDist(Distribution d) {
        auto inst = GcHeap::get().allocate<ObjInstance>();
        inst->classDef = distClass;
        inst->nativeData = std::make_shared<Distribution>(std::move(d));
        return Value(inst);
    }
}

JC2_MODULE(prob) {
    using namespace jc_prob;
    jc::ModuleReg R(env, builtins, arity);
    distClass = GcHeap::get().allocate<ObjClass>();
    jc::Value distClassVal(distClass);

    distClass->name = "Distribution";
    R.set("Distribution", distClassVal);

    // ── 原生全局纯函数 (Global Math Functions) ──
    R.reg("gamma", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::tgamma(a[0].asDouble())); });
    R.reg("lgamma", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::lngamma(a[0].asDouble())); });
    R.reg("betaFn", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::betafn(a[0].asDouble(), a[1].asDouble())); });
    R.reg("erf", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::erf_impl(a[0].asDouble())); });
    R.reg("erfc", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::erfc_impl(a[0].asDouble())); });

    // ── 分布构造器 (Distribution Constructors) ──
    R.reg("Normal", { 0, 1, 2 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        double mu = a.size() >= 1 ? a[0].asDouble() : 0;
        double sigma = a.size() >= 2 ? a[1].asDouble() : 1;
        return makeDist(Distribution::normal(mu, sigma));
        });

    R.reg("TDist", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::studentT(a[0].asDouble())); });
    R.reg("Chi2", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::chiSquared(a[0].asDouble())); });
    R.reg("FDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::fDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("ExpDist", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::exponential(a[0].asDouble())); });
    R.reg("GammaDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::gammaDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("BetaDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::betaDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("Uniform", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::uniformDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("Binom", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::binomial(static_cast<int>(std::round(a[0].asDouble())), a[1].asDouble())); });
    R.reg("Poisson", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::poisson(a[0].asDouble())); });
    R.reg("Geom", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::geometric(a[0].asDouble())); });

    // =========================================================================
    // ★ 追加 OOP 成员方法绑定支持 (Method Bindings for Distribution Class)
    // =========================================================================
    auto addDistMethod = [&](const std::string& name, jc::NativeCallable fn) {
        auto fc = GcHeap::get().allocate<ObjClosure>(
            std::vector<std::string>{}, std::vector<bool>{}, name, nullptr);
        fc->nativeFn = jc::VM::makeNativeFn(fn);
        distClass->methods[name] = fc;
        };

    addDistMethod("pdf", [](const std::vector<jc::Value>& a) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->pdf(a[0].asDouble()));
        });
    addDistMethod("pmf", [](const std::vector<jc::Value>& a) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->pdf(a[0].asDouble()));
        });
    addDistMethod("cdf", [](const std::vector<jc::Value>& a) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->cdf(a[0].asDouble()));
        });
    addDistMethod("quantile", [](const std::vector<jc::Value>& a) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->quantile(a[0].asDouble()));
        });
    addDistMethod("mean", [](const std::vector<jc::Value>&) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->distMean());
        });
    addDistMethod("var", [](const std::vector<jc::Value>&) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(getDist(selfVal)->distVar());
        });
    addDistMethod("std", [](const std::vector<jc::Value>&) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        return jc::Value(std::sqrt(getDist(selfVal)->distVar()));
        });
    addDistMethod("sample", [](const std::vector<jc::Value>& a) -> jc::Value {
        int n = static_cast<int>(std::round(a[0].asDouble()));
        if (n <= 0) throw std::runtime_error("Runtime Error: sample() count must be positive.");
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        auto data = getDist(selfVal)->sample(n);
        return jc::Value(jc::RealMatrix(1, n, data));
        });

    // ── 保留兼容全局泛型操作 (Backward Compatibility API) ──
    R.reg("pdf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->pdf(a[1].asDouble())); });
    R.reg("pmf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->pdf(a[1].asDouble())); });
    R.reg("cdf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->cdf(a[1].asDouble())); });
    R.reg("quantile", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->quantile(a[1].asDouble())); });
    R.reg("dmean", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->distMean()); });
    R.reg("dvar", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->distVar()); });
    R.reg("dstd", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(std::sqrt(getDist(a[0])->distVar())); });

    R.reg("sample", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        int n = static_cast<int>(std::round(a[1].asDouble()));
        if (n <= 0) throw std::runtime_error("Runtime Error: sample() count must be positive.");
        auto data = getDist(a[0])->sample(n);
        return jc::Value(jc::RealMatrix(1, n, data));
        });

    R.reg("distInfo", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        auto d = getDist(a[0]);
        std::cout << "  Distribution: " << d->toString() << std::endl;
        std::cout << "  Type:         " << (d->isDiscrete() ? "Discrete" : "Continuous") << std::endl;
        try { std::cout << "  Mean:         " << d->distMean() << std::endl; }
        catch (...) {}
        try { std::cout << "  Variance:     " << d->distVar() << std::endl; }
        catch (...) {}
        try { std::cout << "  Std Dev:      " << std::sqrt(d->distVar()) << std::endl; }
        catch (...) {}
        return Value::none();
        });

    // ── 假设检验数据预处理 ──
    auto extractDS = [](const Value& v, const std::string& f) -> std::vector<double> {
        if (v.isObjType(ObjType::REAL_MATRIX)) return static_cast<ObjRealMatrix*>(v.asObj())->mat.rawData();
        if (v.isObjType(ObjType::COMPLEX_MATRIX)) {
            const auto& cd = static_cast<ObjComplexMatrix*>(v.asObj())->mat.rawData();
            std::vector<double> r(cd.size());
            for (size_t i = 0; i < cd.size(); ++i) {
                if (std::abs(cd[i].imag) > 1e-15) throw std::runtime_error(f + "() requires real data.");
                r[i] = cd[i].real;
            }
            return r;
        }
        throw std::runtime_error(f + "() requires a matrix/vector.");
        };

    auto printTest = [](const TestResult& r) {
        std::cout << "  " << r.name << std::endl;
        std::cout << "  statistic = " << r.statistic << ",  df = " << r.df
            << ",  p-value = " << r.pValue << std::endl;
        if (r.pValue < 0.001)      std::cout << "  Significance: *** (p < 0.001)" << std::endl;
        else if (r.pValue < 0.01)  std::cout << "  Significance: **  (p < 0.01)" << std::endl;
        else if (r.pValue < 0.05)  std::cout << "  Significance: *   (p < 0.05)" << std::endl;
        else                       std::cout << "  Significance: n.s. (p >= 0.05)" << std::endl;
        };

    // ── 假设检验接口 ──
    R.reg("ttest", { 1, 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto data = extractDS(a[0], "ttest");
        double mu0 = a.size() >= 2 ? a[1].asDouble() : 0.0;
        auto r = ttest1(data, mu0);
        printTest(r);
        return Value(RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("ttest2", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto d1 = extractDS(a[0], "ttest2"), d2 = extractDS(a[1], "ttest2");
        auto r = ttest2ind(d1, d2);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("ttestP", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto d1 = extractDS(a[0], "ttestP"), d2 = extractDS(a[1], "ttestP");
        auto r = ttestPaired(d1, d2);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("chi2test", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto obs = extractDS(a[0], "chi2test"), exp = extractDS(a[1], "chi2test");
        auto r = chi2test(obs, exp);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    // ★ 覆盖 mean/var/std 支持泛型多态调用
    {
        auto it = builtins.find("mean");
        auto oldMean = (it != builtins.end()) ? it->second : nullptr;
        R.reg("mean", { 1 }, [oldMean](const std::vector<jc::Value>& args) -> jc::Value {
            if (args[0].isInstance()) {
                auto inst = args[0].asInstance();
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(getDist(args[0])->distMean());
                }
            }
            if (oldMean) return oldMean(args);
            throw std::runtime_error("Type Error: mean() expects a Distribution.");
            });
    }
    {
        auto it = builtins.find("var");
        auto oldVar = (it != builtins.end()) ? it->second : nullptr;
        R.reg("var", { 1 }, [oldVar](const std::vector<jc::Value>& args) -> jc::Value {
            if (args[0].isInstance()) {
                auto inst = args[0].asInstance();
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(getDist(args[0])->distVar());
                }
            }
            if (oldVar) return oldVar(args);
            throw std::runtime_error("Type Error: var() expects a Distribution.");
            });
    }
    {
        auto it = builtins.find("std");
        auto oldStd = (it != builtins.end()) ? it->second : nullptr;
        R.reg("std", { 1 }, [oldStd](const std::vector<jc::Value>& args) -> jc::Value {
            if (args[0].isInstance()) {
                auto inst = args[0].asInstance();
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(std::sqrt(getDist(args[0])->distVar()));
                }
            }
            if (oldStd) return oldStd(args);
            throw std::runtime_error("Type Error: std() expects a Distribution.");
            });
    }
}

#endif

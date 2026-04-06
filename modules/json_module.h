#ifndef JC2_MODULE_JSON_H
#define JC2_MODULE_JSON_H

#include "../Module.h"
#include <sstream>

// ═══ JSON 序列化/反序列化 ═══
JC2_MODULE(json) {
    jc::ModuleReg R(env, builtins, arity);
    // ── json_encode(value) → JSON string ──
    R.reg("json_encode", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        std::function<std::string(const jc::Value&)> encode;
        encode = [&encode](const jc::Value& val) -> std::string {
            if (val.isNone()) return "null";

            if (std::holds_alternative<double>(val.data)) {
                double d = std::get<double>(val.data);
                double rounded = std::round(d);
                if (jc::Tol::isEq(d, rounded, 1e5) && std::abs(rounded) < 1e15 &&
                    rounded == std::trunc(rounded)) {
                    return std::to_string(static_cast<int64_t>(rounded));
                }
                std::ostringstream oss;
                oss << d;
                return oss.str();
            }
            if (std::holds_alternative<jc::BigInt>(val.data)) {
                return std::get<jc::BigInt>(val.data).toString();
            }
            if (std::holds_alternative<jc::Fraction>(val.data)) {
                std::ostringstream oss;
                oss << std::get<jc::Fraction>(val.data).toDouble();
                return oss.str();
            }
            if (std::holds_alternative<std::string>(val.data)) {
                std::string s = std::get<std::string>(val.data);
                std::string escaped = "\"";
                for (char c : s) {
                    if (c == '"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else if (c == '\n') escaped += "\\n";
                    else if (c == '\t') escaped += "\\t";
                    else if (c == '\r') escaped += "\\r";
                    else escaped += c;
                }
                escaped += "\"";
                return escaped;
            }
            if (std::holds_alternative<jc::Complex>(val.data)) {
                auto c = std::get<jc::Complex>(val.data);
                return "{\"real\":" + std::to_string(c.real) +
                    ",\"imag\":" + std::to_string(c.imag) + "}";
            }
            if (std::holds_alternative<jc::List>(val.data)) {
                const auto& L = std::get<jc::List>(val.data);
                std::string r = "[";
                for (size_t i = 0; i < L.size(); ++i) {
                    if (i > 0) r += ",";
                    r += encode(std::any_cast<jc::Value>(L.raw()[i]));
                }
                return r + "]";
            }
            if (std::holds_alternative<jc::Dict>(val.data)) {
                const auto& d = std::get<jc::Dict>(val.data);
                std::string r = "{";
                bool first = true;
                for (const auto& [k, v] : d.getEntries()) {
                    if (!first) r += ",";
                    r += "\"" + k + "\":";
                    try { r += encode(std::any_cast<jc::Value>(v)); }
                    catch (...) { r += "null"; }
                    first = false;
                }
                return r + "}";
            }
            if (std::holds_alternative<jc::RealMatrix>(val.data)) {
                const auto& m = std::get<jc::RealMatrix>(val.data);
                std::string r = "[";
                for (int i = 0; i < m.getRows(); ++i) {
                    if (i > 0) r += ",";
                    if (m.getRows() > 1) r += "[";
                    for (int j = 0; j < m.getCols(); ++j) {
                        if (j > 0) r += ",";
                        r += encode(jc::Value(m(i, j)));
                    }
                    if (m.getRows() > 1) r += "]";
                }
                return r + "]";
            }

            // Fallback
            std::ostringstream oss;
            oss << val;
            return "\"" + oss.str() + "\"";
            };

        return jc::Value(encode(args[0]));
        });

    // ── json_decode(string) → value ──
    R.reg("json_decode", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: json_decode() expects a string.");

        const std::string& s = std::get<std::string>(args[0].data);
        size_t pos = 0;

        std::function<void()> skipWS;
        std::function<jc::Value()> parseValue;
        std::function<std::string()> parseString;
        std::function<jc::Value()> parseNumber;
        std::function<jc::Value()> parseArray;
        std::function<jc::Value()> parseObject;

        skipWS = [&]() {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
                pos++;
            };

        parseString = [&]() -> std::string {
            if (pos >= s.size() || s[pos] != '"')
                throw std::runtime_error("JSON Error: Expected '\"'.");
            pos++;
            std::string result;
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\\') {
                    pos++;
                    if (pos >= s.size()) throw std::runtime_error("JSON Error: Unexpected end.");
                    if (s[pos] == '"') result += '"';
                    else if (s[pos] == '\\') result += '\\';
                    else if (s[pos] == 'n') result += '\n';
                    else if (s[pos] == 't') result += '\t';
                    else if (s[pos] == 'r') result += '\r';
                    else { result += '\\'; result += s[pos]; }
                }
                else {
                    result += s[pos];
                }
                pos++;
            }
            if (pos >= s.size()) throw std::runtime_error("JSON Error: Unterminated string.");
            pos++; // closing "
            return result;
            };

        parseNumber = [&]() -> jc::Value {
            size_t start = pos;
            if (pos < s.size() && s[pos] == '-') pos++;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            bool isFloat = false;
            if (pos < s.size() && s[pos] == '.') { isFloat = true; pos++; }
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                isFloat = true; pos++;
                if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
                while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
            }
            std::string numStr = s.substr(start, pos - start);
            if (isFloat) return jc::Value(std::stod(numStr));
            try { return jc::Value(jc::BigInt(numStr)); }
            catch (...) { return jc::Value(std::stod(numStr)); }
            };

        parseArray = [&]() -> jc::Value {
            pos++; // [
            skipWS();
            jc::List L;
            if (pos < s.size() && s[pos] == ']') { pos++; return jc::Value(L); }
            while (true) {
                L.push_back(std::make_any<jc::Value>(parseValue()));
                skipWS();
                if (pos < s.size() && s[pos] == ',') { pos++; skipWS(); }
                else break;
            }
            if (pos >= s.size() || s[pos] != ']')
                throw std::runtime_error("JSON Error: Expected ']'.");
            pos++;
            return jc::Value(L);
            };

        parseObject = [&]() -> jc::Value {
            pos++; // {
            skipWS();
            jc::Dict d;
            if (pos < s.size() && s[pos] == '}') { pos++; return jc::Value(d); }
            while (true) {
                skipWS();
                std::string key = parseString();
                skipWS();
                if (pos >= s.size() || s[pos] != ':')
                    throw std::runtime_error("JSON Error: Expected ':'.");
                pos++; skipWS();
                d.set(key, std::make_any<jc::Value>(parseValue()));
                skipWS();
                if (pos < s.size() && s[pos] == ',') { pos++; }
                else break;
            }
            if (pos >= s.size() || s[pos] != '}')
                throw std::runtime_error("JSON Error: Expected '}'.");
            pos++;
            return jc::Value(d);
            };

        parseValue = [&]() -> jc::Value {
            skipWS();
            if (pos >= s.size()) throw std::runtime_error("JSON Error: Unexpected end.");
            if (s[pos] == '"') return jc::Value(parseString());
            if (s[pos] == '[') return parseArray();
            if (s[pos] == '{') return parseObject();
            if (s[pos] == 't') { pos += 4; return jc::Value(1.0); }
            if (s[pos] == 'f') { pos += 5; return jc::Value(0.0); }
            if (s[pos] == 'n') { pos += 4; return jc::Value::none(); }
            return parseNumber();
            };

        return parseValue();
        });

    // ── json_pretty(value, indent) → formatted JSON string ──
    R.reg("json_pretty", { 1, 2 }, [&builtins](const std::vector<jc::Value>& args) -> jc::Value {
        int indent = (args.size() >= 2) ? static_cast<int>(std::round(args[1].asDouble())) : 2;

        // 先用 json_encode 获取紧凑 JSON，然后格式化
        std::string json = std::get<std::string>(builtins["json_encode"]({ args[0] }).data);

        // 格式化
        std::string result;
        int level = 0;
        bool inStr = false;
        std::string pad;
        auto makeIndent = [&]() { return std::string(level * indent, ' '); };

        for (size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (inStr) {
                result += c;
                if (c == '"' && (i == 0 || json[i - 1] != '\\')) inStr = false;
                continue;
            }
            if (c == '"') { inStr = true; result += c; continue; }
            if (c == '{' || c == '[') {
                result += c;
                if (i + 1 < json.size() && (json[i + 1] == '}' || json[i + 1] == ']')) {
                    // empty container
                }
                else {
                    level++;
                    result += "\n" + makeIndent();
                }
            }
            else if (c == '}' || c == ']') {
                if (i > 0 && json[i - 1] != '{' && json[i - 1] != '[') {
                    level--;
                    result += "\n" + makeIndent();
                }
                result += c;
            }
            else if (c == ',') {
                result += ",\n" + makeIndent();
            }
            else if (c == ':') {
                result += ": ";
            }
            else {
                result += c;
            }
        }
        return jc::Value(result);
        });
}

#endif // JC2_MODULE_JSON_H
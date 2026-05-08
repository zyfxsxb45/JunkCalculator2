#ifndef JC2_HELP_ROUTER_H
#define JC2_HELP_ROUTER_H

#include <string>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <map>
#include <any>
#include "GeneratedHelpText.h"
#include "../modules/json_module.h"
#include "../memory/Value.h"
#include "../frontend/Highlight.h"

namespace jc {
    inline std::map<std::string, std::string> DynamicHelp;

    class HelpRouter {
    private:
        static inline Value helpAst = Value::none();
        static inline bool initialized = false;

        static void init() {
            if (initialized) return;
            // 1. 将 CMake 生成的字节数组转为标准 C++ 字符串
            // 使用 sizeof 确保即使中间有 \0 也不会被截断
            std::string jsonStr(reinterpret_cast<const char*>(RAW_HELP_JSON), sizeof(RAW_HELP_JSON) - 1);
            
            // 2. 擦除所有的 UTF-8 BOM 头（如果存在）
            size_t pos = 0;
            while ((pos = jsonStr.find("\xEF\xBB\xBF", pos)) != std::string::npos) {
                jsonStr.erase(pos, 3);
            }
            
            // 3. 净化所有的控制字符 (解决纯手写 JSON 解析器对不可见字符处理不严格的 Bug)
            // JSON 规范允许在内部任意添加空白，将 \r, \t, \0 等替换为空格绝对安全
            for (char& c : jsonStr) {
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 32 && uc != '\n') {
                    c = ' ';
                }
            }
            try {
                // 将极其干净的字符串喂给你的 Parser
                JsonParser parser(jsonStr);
                helpAst = parser.parseValue();
            }
            catch (const std::exception& e) {
                std::cerr << "\n[HelpRouter] Failed to parse documentation.json: " << e.what() << "\n";
            }
            initialized = true;
        }

        static Value extractAny(const std::any& a) {
            try { return std::any_cast<Value>(a); }
            catch (...) { return Value::none(); }
        }

        static int levenshtein(const std::string& s1, const std::string& s2) {
            int len1 = static_cast<int>(s1.size()), len2 = static_cast<int>(s2.size());
            std::vector<int> col(len2 + 1), prevCol(len2 + 1);
            for (int i = 0; i <= len2; i++) prevCol[i] = i;
            for (int i = 0; i < len1; i++) {
                col[0] = i + 1;
                for (int j = 0; j < len2; j++) {
                    col[j + 1] = std::min({ prevCol[1 + j] + 1, col[j] + 1, prevCol[j] + (s1[i] == s2[j] ? 0 : 1) });
                }
                prevCol = col;
            }
            return prevCol[len2];
        }

        static bool printEntry(const Value& entryVal) {
            if (!std::holds_alternative<Dict>(entryVal.data)) return false;
            Dict entry = std::get<Dict>(entryVal.data);
            std::cout << "\n";
            if (entry.has(Value("signature"))) {
                std::cout << col(Ansi::BRIGHT_GREEN) << std::get<std::string>(entry.get(Value("signature"))->data) << col(Ansi::RESET) << "\n";
            }
            if (entry.has(Value("desc"))) {
                Value descVal = *entry.get(Value("desc"));
                if (std::holds_alternative<std::string>(descVal.data)) {
                    std::cout << "  " << std::get<std::string>(descVal.data) << "\n";
                } else if (std::holds_alternative<List>(descVal.data)) {
                    for (const auto& lineAny : std::get<List>(descVal.data).raw()) {
                        Value lineVal = extractAny(lineAny);
                        if (std::holds_alternative<std::string>(lineVal.data)) {
                            std::cout << "  " << std::get<std::string>(lineVal.data) << "\n";
                        }
                    }
                }
            }
            if (entry.has(Value("examples"))) {
                Value exVal = *entry.get(Value("examples"));
                if (std::holds_alternative<List>(exVal.data)) {
                    std::cout << "\n  Examples:\n";
                    for (const auto& exAny : std::get<List>(exVal.data).raw()) {
                        Value ex = extractAny(exAny);
                        if (std::holds_alternative<std::string>(ex.data)) {
                            std::cout << col(Ansi::BRIGHT_YELLOW) << "    " << std::get<std::string>(ex.data) << col(Ansi::RESET) << "\n";
                        }
                    }
                }
            }
            std::cout << std::endl;
            return true;
        }

    public:
        static void printHelpTopic(const std::string& topic) {
            init();

            std::string key = topic;
            size_t s = key.find_first_not_of(" \t");
            size_t e = key.find_last_not_of(" \t");
            if (s != std::string::npos) key = key.substr(s, e - s + 1);
            else key.clear();

            bool forceFunction = false;
            if (key.length() > 2 && key.substr(key.length() - 2) == "()") {
                forceFunction = true;
                key = key.substr(0, key.length() - 2);
            }

            // 1. Dynamic Help (Scripts)
            auto itDyn = DynamicHelp.find(key);
            if (itDyn != DynamicHelp.end()) {
                std::cout << "\n" << itDyn->second << std::endl;
                return;
            }

            if (helpAst.isNone() || !std::holds_alternative<Dict>(helpAst.data)) {
                std::cout << "\n  [System] Documentation data is unavailable.\n";
                return;
            }

            Dict root = std::get<Dict>(helpAst.data);

            std::string lowerKey = key;
            std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

            // 2. Search in "topics" (Case-insensitive)
            if (!forceFunction && root.has(Value("topics"))) {
                Value topicsVal = *root.get(Value("topics"));
                if (std::holds_alternative<Dict>(topicsVal.data)) {
                    Dict topics = std::get<Dict>(topicsVal.data);
                    if (topics.has(Value(lowerKey))) {
                        Value topicVal = *topics.get(Value(lowerKey));
                        if (std::holds_alternative<std::string>(topicVal.data)) {
                            std::cout << "\n" << std::get<std::string>(topicVal.data) << std::endl;
                        } else if (std::holds_alternative<List>(topicVal.data)) {
                            std::cout << "\n";
                            for (const auto& lineAny : std::get<List>(topicVal.data).raw()) {
                                Value lineVal = extractAny(lineAny);
                                if (std::holds_alternative<std::string>(lineVal.data)) {
                                    std::cout << std::get<std::string>(lineVal.data) << "\n";
                                }
                            }
                            std::cout << std::endl;
                        }
                        
                        // Check if a function with the same name exists to provide a hint
                        if (root.has(Value("functions"))) {
                            Value funcsVal = *root.get(Value("functions"));
                            if (std::holds_alternative<Dict>(funcsVal.data)) {
                                Dict funcs = std::get<Dict>(funcsVal.data);
                                if (funcs.has(Value(key))) {
                                    std::cout << col(Ansi::BRIGHT_YELLOW) << "  Tip: '" << key << "' is also a built-in function. Type '/help " << key << "()' to see its documentation.\n" << col(Ansi::RESET) << std::endl;
                                }
                            }
                        }
                        return;
                    }
                }
            }

            // 3. Search in "functions" (Exact match or alias)
            if (root.has(Value("functions"))) {
                Value funcsVal = *root.get(Value("functions"));
                if (std::holds_alternative<Dict>(funcsVal.data)) {
                    Dict funcs = std::get<Dict>(funcsVal.data);
                    
                    Value targetFn = Value::none();
                    if (funcs.has(Value(key))) {
                        targetFn = *funcs.get(Value(key));
                    } else {
                        for (const auto& [k, v] : funcs.getEntries()) {
                            if (std::holds_alternative<Dict>(v.data)) {
                                Dict fnDict = std::get<Dict>(v.data);
                                if (fnDict.has(Value("aliases"))) {
                                    Value aliasesVal = *fnDict.get(Value("aliases"));
                                    if (std::holds_alternative<List>(aliasesVal.data)) {
                                        for (const auto& aliasAny : std::get<List>(aliasesVal.data).raw()) {
                                            Value aliasVal = extractAny(aliasAny);
                                            if (std::holds_alternative<std::string>(aliasVal.data) && 
                                                std::get<std::string>(aliasVal.data) == key) {
                                                targetFn = v;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            if (!targetFn.isNone()) break;
                        }
                    }

                    if (!targetFn.isNone() && printEntry(targetFn)) return;
                }
            }

            // 3.5 Search in "keywords" (case-insensitive)
            if (root.has(Value("keywords"))) {
                Value kwsVal = *root.get(Value("keywords"));
                if (std::holds_alternative<Dict>(kwsVal.data)) {
                    Dict kws = std::get<Dict>(kwsVal.data);
                    if (kws.has(Value(lowerKey))) {
                        if (printEntry(*kws.get(Value(lowerKey)))) return;
                    }
                }
            }

            // 4. Did you mean? (Fuzzy matching)
            std::vector<std::pair<std::string, std::string>> candidates;
            if (root.has(Value("functions"))) {
                Value funcsVal = *root.get(Value("functions"));
                if (std::holds_alternative<Dict>(funcsVal.data)) {
                    Dict funcs = std::get<Dict>(funcsVal.data);
                    for (const auto& [k, v] : funcs.getEntries()) {
                        if (std::holds_alternative<std::string>(k.data) && std::holds_alternative<Dict>(v.data)) {
                            std::string funcName = std::get<std::string>(k.data);
                            Dict fnDict = std::get<Dict>(v.data);
                            std::string desc = "";
                            if (fnDict.has(Value("desc"))) {
                                Value descVal = *fnDict.get(Value("desc"));
                                if (std::holds_alternative<std::string>(descVal.data)) {
                                    desc = std::get<std::string>(descVal.data);
                                } else if (std::holds_alternative<List>(descVal.data)) {
                                    auto raw = std::get<List>(descVal.data).raw();
                                    if (!raw.empty()) {
                                        Value firstLine = extractAny(raw[0]);
                                        if (std::holds_alternative<std::string>(firstLine.data)) {
                                            desc = std::get<std::string>(firstLine.data);
                                        }
                                    }
                                }
                            }
                            
                            std::string lowerFuncName = funcName;
                            std::transform(lowerFuncName.begin(), lowerFuncName.end(), lowerFuncName.begin(),
                                [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
                            
                            bool isPrefix = lowerFuncName.find(lowerKey) == 0;
                            bool isSubstr = lowerFuncName.find(lowerKey) != std::string::npos;
                            int dist = levenshtein(lowerKey, lowerFuncName);
                            
                            if (isPrefix || isSubstr || dist <= 2) {
                                candidates.push_back({funcName, desc});
                            }
                        }
                    }
                }
            }

            if (root.has(Value("keywords"))) {
                Value kwsVal = *root.get(Value("keywords"));
                if (std::holds_alternative<Dict>(kwsVal.data)) {
                    Dict kws = std::get<Dict>(kwsVal.data);
                    for (const auto& [k, v] : kws.getEntries()) {
                        if (std::holds_alternative<std::string>(k.data) && std::holds_alternative<Dict>(v.data)) {
                            std::string kwName = std::get<std::string>(k.data);
                            Dict kwDict = std::get<Dict>(v.data);
                            std::string desc = "";
                            if (kwDict.has(Value("desc"))) {
                                Value descVal = *kwDict.get(Value("desc"));
                                if (std::holds_alternative<std::string>(descVal.data)) {
                                    desc = std::get<std::string>(descVal.data);
                                } else if (std::holds_alternative<List>(descVal.data)) {
                                    auto raw = std::get<List>(descVal.data).raw();
                                    if (!raw.empty()) {
                                        Value firstLine = extractAny(raw[0]);
                                        if (std::holds_alternative<std::string>(firstLine.data))
                                            desc = std::get<std::string>(firstLine.data);
                                    }
                                }
                            }
                            std::string lowerKwName = kwName;
                            std::transform(lowerKwName.begin(), lowerKwName.end(), lowerKwName.begin(),
                                [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
                            bool isPrefix = lowerKwName.find(lowerKey) == 0;
                            bool isSubstr = lowerKwName.find(lowerKey) != std::string::npos;
                            int dist = levenshtein(lowerKey, lowerKwName);
                            if (isPrefix || isSubstr || dist <= 2) {
                                candidates.push_back({kwName + " (keyword)", desc});
                            }
                        }
                    }
                }
            }

            if (!candidates.empty()) {
                std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
                    std::string la = a.first, lb = b.first;
                    std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
                    std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });
                    
                    bool aPref = la.find(lowerKey) == 0;
                    bool bPref = lb.find(lowerKey) == 0;
                    if (aPref != bPref) return aPref;
                    
                    bool aSub = la.find(lowerKey) != std::string::npos;
                    bool bSub = lb.find(lowerKey) != std::string::npos;
                    if (aSub != bSub) return aSub;
                    
                    return levenshtein(lowerKey, la) < levenshtein(lowerKey, lb);
                });
                
                std::cout << "\n  No exact match found for '" << topic << "'.\n\n";
                std::cout << "  Did you mean?\n";
                int count = 0;
                for (const auto& cand : candidates) {
                    if (count++ >= 5) break;
                    std::cout << col(Ansi::BRIGHT_GREEN) << "    > " << cand.first << col(Ansi::RESET);
                    if (!cand.second.empty()) {
                        std::string d = cand.second;
                        if (d.length() > 60) {
                            size_t cutPos = 57;
                            size_t lastSpace = d.find_last_of(" \t", 57);
                            if (lastSpace != std::string::npos && lastSpace > 30) {
                                cutPos = lastSpace;
                            }
                            d = d.substr(0, cutPos) + "...";
                        }
                        int padding = std::max(1, 15 - static_cast<int>(cand.first.length()));
                        std::cout << std::string(padding, ' ') << "(" << d << ")";
                    }
                    std::cout << "\n";
                }
                std::cout << std::endl;
                return;
            }

            std::cout << "\n  No detailed help available for topic: '" << topic << "'\n";
        }

        static void printMainHelp() {
            printHelpTopic("main");
        }
    };
} // namespace jc

#endif // JC2_HELP_ROUTER_H

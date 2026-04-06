#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include "Lexer.h"
#include "Parser.h"
#include "Evaluator.h"
#include "Value.h"
#include "HelpText.h"
#include "Highlight.h"  // ★
#include "Module.h"
#include "modules/json_module.h"
#include "modules/image_module.h"    // ★
#include "modules/prob_module.h"     // ★

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

// =================================================================
// 帮助系统 (Static Embedded Version)
// =================================================================

void printHelp() {
    auto it = jc::BuiltinHelp.find("main");
    if (it != jc::BuiltinHelp.end()) {
        std::cout << "\n" << it->second << std::endl;
    }
}

void printHelpTopic(const std::string& topic) {
    std::string key = topic;

    // 清洗首尾空白并转小写
    size_t s = key.find_first_not_of(" \t");
    size_t e = key.find_last_not_of(" \t");
    if (s != std::string::npos) key = key.substr(s, e - s + 1);
    else key.clear();

    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

    auto it = jc::BuiltinHelp.find(key);
    if (it != jc::BuiltinHelp.end()) {
        std::cout << "\n" << it->second << std::endl;
    }
    else {
        std::cout << "\n  No detailed help available for topic: '" << topic << "'\n"
            << "  Please type 'help' to see available topics.\n" << std::endl;
    }
}

// =================================================================
// 终极持久化引擎：Workspace I/O
// =================================================================
void saveWorkspace(const std::string& filename, jc::Evaluator& evaluator) {
    namespace fs = std::filesystem;
    if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
        std::cerr << "   IO Error: Invalid characters in workspace name." << std::endl;
        return;
    }
    fs::path dir = evaluator.getWorkspacePath();  // ★
    if (!fs::exists(dir)) fs::create_directories(dir);

    std::string path = (dir / (filename + ".jc2")).string();
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "   IO Error: Failed to save workspace to " << path << std::endl;
        return;
    }

    int count = 0;

    std::string currentPrimePath = jc::BigInt::getPrimeFilePath();
    for (char& c : currentPrimePath) if (c == '\\') c = '/';
    if (std::filesystem::exists(currentPrimePath)) {
        out << "mountPrimes(\"" << currentPrimePath << "\")\n";
    }

    for (const auto& [name, value] : evaluator.getEnvironment()) {
        if (name == "PI" || name == "E" || name == "i" || name == "I" ||
            name == "ANS" || name == "true" || name == "false") continue;

        if (value.isFunctionClosure()) {
            auto closure = std::get<std::shared_ptr<jc::FunctionClosure>>(value.data);

            std::string pStr;
            for (size_t i = 0; i < closure->paramNames.size(); ++i) {
                if (closure->isRef[i]) pStr += "ref ";
                pStr += closure->paramNames[i];
                if (i < closure->paramNames.size() - 1) pStr += ", ";
            }

            if (closure->hasCaptures()) {
                try {
                    const auto& captured = std::any_cast<
                        const std::map<std::string, jc::Value>&>(closure->capturedEnv);

                    auto isReferenced = [](const std::string& word, const std::string& body) -> bool {
                        size_t pos = 0;
                        while ((pos = body.find(word, pos)) != std::string::npos) {
                            bool leftOk = (pos == 0 ||
                                (!std::isalnum(static_cast<unsigned char>(body[pos - 1])) && body[pos - 1] != '_'));
                            size_t end = pos + word.size();
                            bool rightOk = (end >= body.size() ||
                                (!std::isalnum(static_cast<unsigned char>(body[end])) && body[end] != '_'));
                            if (leftOk && rightOk) return true;
                            pos += word.size();
                        }
                        return false;
                        };

                    std::vector<std::pair<std::string, jc::Value>> essentialCaptures;
                    for (const auto& [ck, cv] : captured) {
                        if (ck == "PI" || ck == "E" || ck == "i" || ck == "I" || ck == "ANS") continue;
                        if (cv.isFunctionClosure() || cv.isNone()) continue;
                        bool isParam = false;
                        for (const auto& p : closure->paramNames)
                            if (ck == p) { isParam = true; break; }
                        if (isParam) continue;
                        if (!isReferenced(ck, closure->rawBody)) continue;
                        essentialCaptures.push_back({ ck, cv });
                    }

                    if (!essentialCaptures.empty()) {
                        out << name << " = (() => { ";
                        for (const auto& [ck, cv] : essentialCaptures)
                            out << ck << " = " << cv.toJC2Expression() << "; ";
                        out << "return (" << pStr << ") => " << closure->rawBody;
                        out << " })()\n";
                    }
                    else { out << name << "(" << pStr << ") = " << closure->rawBody << "\n";}
                }
                catch (...) {out << name << "(" << pStr << ") = " << closure->rawBody << "\n";}
            }
            else {out << name << "(" << pStr << ") = " << closure->rawBody << "\n";}
        }
        else {out << name << " = " << value.toJC2Expression() << "\n";}
        count++;
    }
    out.close();
    std::cout << "   Saved " << count << " variables to " << path << std::endl;
}

void loadWorkspace(const std::string& filename, jc::Evaluator& evaluator) {
    namespace fs = std::filesystem;
    std::string path = (fs::path(evaluator.getWorkspacePath()) / (filename + ".jc2")).string();  // ★

    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "   IO Error: Workspace '" << filename << "' not found at "
            << evaluator.getWorkspacePath() << std::endl;
        return;
    }

    evaluator.clearVariables();

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    in.close();

    std::vector<bool> pending(lines.size(), true);
    int totalRestored = 0;
    constexpr int MAX_PASSES = 10;

    for (int pass = 0; pass < MAX_PASSES; ++pass) {
        bool anyResolved = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (!pending[i]) continue;
            try {
                jc::Lexer lexer(lines[i]);
                auto tokens = lexer.tokenize();
                jc::Parser parser(tokens);
                auto ast = parser.parse();
                evaluator.calculate(ast.get());
                pending[i] = false;
                anyResolved = true;
                if (lines[i].find('=') != std::string::npos) totalRestored++;
            }
            catch (...) {}
        }
        if (!anyResolved) break;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        if (pending[i]) {
            try {
                jc::Lexer lexer(lines[i]);
                auto tokens = lexer.tokenize();
                jc::Parser parser(tokens);
                auto ast = parser.parse();
                evaluator.calculate(ast.get());
                if (lines[i].find('=') != std::string::npos) totalRestored++;
            }
            catch (const std::exception& e) {
                std::cerr << "   Warning: Failed to restore -> " << lines[i]
                    << "\n   Error: " << e.what() << std::endl;
            }
        }
    }

    std::cout << "   Loaded " << totalRestored << " variables from " << path << std::endl;
}

// =================================================================
// 操作系统文件级管理 (Workspace File Management)
// =================================================================

void listWorkspaces(const std::string& dirPath, const std::string& filter = "") {
	namespace fs = std::filesystem;
	fs::path dir = dirPath;

	if (!fs::exists(dir)) {
		std::cout << "   Directory not found: " << dirPath << std::endl;
		return;
	}

	std::set<std::string> extensions;
	if (filter == "*" || filter == "all") {
		// 不过滤
	}
	else if (filter.empty()) {
		extensions = { ".jc2", ".txt", ".csv" };
	}
	else {
		std::string ext = filter;
		if (ext[0] != '.') ext = "." + ext;
		extensions.insert(ext);
	}

    std::cout << jc::col(jc::Ansi::BOLD) << "--- " << fs::weakly_canonical(dir).string()
        << " ---" << jc::col(jc::Ansi::RESET) << std::endl;

	// ★ 分别收集文件夹和文件
	std::vector<std::string> dirs;
	std::vector<std::pair<std::string, uintmax_t>> files;

	for (const auto& entry : fs::directory_iterator(dir)) {
		if (entry.is_directory()) {
			dirs.push_back(entry.path().filename().string());
		}
		else if (entry.is_regular_file()) {
			std::string ext = entry.path().extension().string();
			if (!extensions.empty() && extensions.find(ext) == extensions.end()) continue;
			files.push_back({ entry.path().filename().string(), fs::file_size(entry.path()) });
		}
	}

	std::sort(dirs.begin(), dirs.end());
	std::sort(files.begin(), files.end());

	// ★ 先打印文件夹
	for (const auto& name : dirs) {
        std::cout << "  " << jc::col(jc::Ansi::BRIGHT_BLUE) << "[DIR]  " << name << "/"
            << jc::col(jc::Ansi::RESET) << "\n";
	}

	// ★ 再打印文件
	for (const auto& [name, size] : files) {
        // 从文件名中提取扩展名着色
        std::string displayName = name;
        size_t dotPos = name.rfind('.');
        if (dotPos != std::string::npos) {
            std::string base = name.substr(0, dotPos);
            std::string ext = name.substr(dotPos);
            displayName = base + jc::col(jc::Ansi::DIM) + ext + jc::col(jc::Ansi::RESET);
        }
        std::cout << "         " << displayName;
		for (size_t j = name.length(); j < 28; ++j) std::cout << " ";
		if (size < 1024)
			std::cout << size << " B";
		else if (size < 1024 * 1024)
			std::cout << std::fixed << std::setprecision(1) << (size / 1024.0) << " KB";
		else
			std::cout << std::fixed << std::setprecision(1) << (size / (1024.0 * 1024.0)) << " MB";
		std::cout << "\n";
	}

	int total = static_cast<int>(dirs.size() + files.size());
	if (total == 0) {
		std::cout << "   (empty)" << std::endl;
	}
	std::cout << "   " << dirs.size() << " folder(s), " << files.size() << " file(s)" << std::endl;
	std::cout << "--------------------------------------------" << std::endl;
}

void removeWorkspace(const std::string& filename, jc::Evaluator& evaluator) {
    namespace fs = std::filesystem;
    std::string path = (fs::path(evaluator.getWorkspacePath()) / (filename + ".jc2")).string();  // ★

    if (!fs::exists(path)) {
        std::cerr << "   FS Error: Workspace '" << filename << "' does not exist." << std::endl;
        return;
    }

    try {
        if (fs::remove(path))
            std::cout << "   Deleted workspace -> " << filename << ".jc2" << std::endl;
        else
            std::cerr << "   FS Error: Failed to delete '" << filename << "'." << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "   FS Error: " << e.what() << std::endl;
    }
}

void runScript(const std::string& filepath, jc::Evaluator& evaluator) {
	namespace fs = std::filesystem;

	std::string resolvedPath = filepath;

	if (!fs::exists(resolvedPath) && !fs::path(filepath).is_absolute()) {
		std::string withExt = filepath + ".jc2";

		// ★ 搜索顺序：CWD → CWD+.jc2 → workspace → workspace+.jc2 → data/ → data/+.jc2
		std::vector<std::string> candidates = {
			filepath,
			withExt,
			(fs::path(evaluator.getWorkspacePath()) / filepath).string(),
			(fs::path(evaluator.getWorkspacePath()) / withExt).string(),
			(fs::current_path() / "data" / filepath).string(),
			(fs::current_path() / "data" / withExt).string(),
		};

        // ★ 在 candidates 列表末尾追加 exe 目录兜底
        std::string exDir = evaluator.getExeDir();
        candidates.push_back((fs::path(exDir) / filepath).string());
        candidates.push_back((fs::path(exDir) / (filepath + ".jc2")).string());
        candidates.push_back((fs::path(exDir) / "data" / filepath).string());
        candidates.push_back((fs::path(exDir) / "data" / (filepath + ".jc2")).string());
        candidates.push_back((fs::path(exDir) / "lib" / filepath).string());
        candidates.push_back((fs::path(exDir) / "lib" / (filepath + ".jc2")).string());

		resolvedPath.clear();
		for (const auto& c : candidates) {
			if (fs::exists(c)) { resolvedPath = c; break; }
		}

		if (resolvedPath.empty()) {
			std::cerr << "   IO Error: Cannot open script '" << filepath << "'." << std::endl;
			return;
		}
	}

	std::ifstream file(resolvedPath);
	if (!file.is_open()) {
		std::cerr << "   IO Error: Cannot open script '" << filepath << "'." << std::endl;
		return;
	}

	std::string scriptDir = fs::weakly_canonical(resolvedPath).parent_path().string();
	evaluator.pushScriptDir(scriptDir);

	std::string rawLine;
	int lineNum = 0;
	int executed = 0;

	while (std::getline(file, rawLine)) {
		lineNum++;

		size_t s = rawLine.find_first_not_of(" \t");
		size_t e = rawLine.find_last_not_of(" \t\r\n");
		if (s == std::string::npos) continue;
		std::string line = rawLine.substr(s, e - s + 1);

		if (line.empty()) continue;
		if (line.size() >= 2 && line[0] == '/' && line[1] == '/') continue;

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

		{
			int braces = 0, parens = 0, brackets = 0;
			for (char c : line) {
				if (c == '{') braces++; else if (c == '}') braces--;
				if (c == '(') parens++; else if (c == ')') parens--;
                if (c == '[') brackets++; else if (c == ']') brackets--;
			}
			while ((braces > 0 || parens > 0 || brackets > 0 || endsWithContinuation(line)) && std::getline(file, rawLine)) {
				lineNum++;
				size_t commentPos = rawLine.find("//");
				std::string stripped = (commentPos != std::string::npos)
					? rawLine.substr(0, commentPos) : rawLine;
				line += " " + stripped;
				for (char c : stripped) {
					if (c == '{') braces++; else if (c == '}') braces--;
					if (c == '(') parens++; else if (c == ')') parens--;
                    if (c == '[') brackets++; else if (c == ']') brackets--;
				}
			}
		}

		try {
			jc::Lexer lexer(line);
			auto tokens = lexer.tokenize();
			jc::Parser parser(tokens);
			auto ast = parser.parse();
			jc::Value result = evaluator.calculate(ast.get());

			if (!result.isNone()) {
				evaluator.setVariable("ANS", result);
			}
			executed++;
		}
		catch (const std::exception& ex) {
            std::cerr << jc::col(jc::Ansi::BRIGHT_RED) << "   Error at line " << lineNum
                << ": " << ex.what() << jc::col(jc::Ansi::RESET) << std::endl;
            std::cerr << jc::col(jc::Ansi::DIM) << "   >>> " << line
                << jc::col(jc::Ansi::RESET) << std::endl;
			file.close();
			evaluator.popScriptDir();
			std::cout << "   Script '" << resolvedPath << "' aborted at line " << lineNum
				<< ": " << executed << " statements executed." << std::endl;
			return;
		}
	}

	file.close();
	evaluator.popScriptDir();
	std::cout << "   Script '" << resolvedPath << "' finished: "
		<< executed << " statements executed." << std::endl;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    std::system("chcp 65001 > nul");
#endif
    jc::enableAnsiColors();
    jc::Evaluator evaluator;
    // ★ 记录 exe 所在目录
    {
        namespace fs = std::filesystem;
#ifdef _WIN32
        char buf[2048];
        if (GetModuleFileNameA(nullptr, buf, sizeof(buf))) {
            evaluator.setExeDir(fs::path(buf).parent_path().string());
        }
        else {
            evaluator.setExeDir(fs::current_path().string());
        }
#else
        evaluator.setExeDir(fs::current_path().string());
#endif
    }
    // ★ 命令行模式：JunkCalculator2 --run script.jc2
    if (argc >= 3 && std::string(argv[1]) == "--run") {
        std::string scriptPath = argv[2];
        runScript(scriptPath, evaluator);
        return 0;
    }
    // ★ 单参数模式：JunkCalculator2 script.jc2
    if (argc == 2) {
        std::string arg = argv[1];
        if (arg != "--help" && arg != "-h") {
            runScript(arg, evaluator);
            return 0;
        }
    }
    std::cout << jc::col(jc::Ansi::BRIGHT_CYAN)
        << "=================================================" << std::endl
        << "   Junk Calculator 2.0 [Build 2026.04]" << std::endl
        << "   Developed by Yu Liangyang, Tsinghua University" << std::endl
        << "   Powered by AST Engine & C++20" << std::endl
        << "=================================================" << jc::col(jc::Ansi::RESET) << std::endl
        << "Type " << jc::col(jc::Ansi::BRIGHT_YELLOW) << "'help'"
        << jc::col(jc::Ansi::RESET) << " for a list of commands and functions." << std::endl;

    while (true) {
        std::string input;
        std::cout << "\n" << jc::col(jc::Ansi::BOLD) << jc::col(jc::Ansi::BRIGHT_CYAN)
            << "JC2> " << jc::col(jc::Ansi::RESET);
        std::getline(std::cin, input);

        // 去除首尾空格
        size_t start = input.find_first_not_of(" \t");
        size_t end = input.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        input = input.substr(start, end - start + 1);

        if (input.empty()) continue;

        // --- ★ 新增：多行输入续读（检测未闭合的括号和花括号）---
        {
            int braces = 0, parens = 0, brackets = 0;
            for (char c : input) {
                if (c == '{') braces++;
                else if (c == '}') braces--;
                else if (c == '(') parens++;
                else if (c == ')') parens--;
                else if (c == '[') brackets++;
                else if (c == ']') brackets--;
            }
            while (braces > 0 || parens > 0 || brackets > 0 || endsWithContinuation(input)) {
                std::string line;
                std::cout << jc::col(jc::Ansi::BRIGHT_CYAN) << "...  " << jc::col(jc::Ansi::RESET);
                if (!std::getline(std::cin, line)) break; // EOF
                input += " " + line;
                for (char c : line) {
                    if (c == '{') braces++;
                    else if (c == '}') braces--;
                    else if (c == '(') parens++;
                    else if (c == ')') parens--;
                    else if (c == '[') brackets++;
                    else if (c == ']') brackets--;
                }
            }
        }

        if (input == "color on") {
            jc::colorsEnabled = true;
            std::cout << "   Colors enabled." << std::endl;
            continue;
        }
        if (input == "color off") {
            jc::colorsEnabled = false;
            std::cout << "   Colors disabled." << std::endl;
            continue;
        }

        // --- 特殊命令拦截 ---
        if (input == "exit" || input == "quit") break;

        if (input == "help") { printHelp(); continue; }
        if (input.substr(0, 5) == "help ") { printHelpTopic(input.substr(5)); continue; }

        if (input == "vars") {
            evaluator.showVariables();
            continue;
        }

        if (input == "clear") {
            evaluator.clearVariables();
            std::cout << "All user variables cleared." << std::endl;
            continue;
        }

        if (input.substr(0, 9) == "workspace") {
            std::string arg = input.substr(9);
            size_t ws = arg.find_first_not_of(" \t");
            if (ws != std::string::npos) arg = arg.substr(ws);
            else arg.clear();

            if (arg.empty()) {
                std::cout << "   Current workspace: " << evaluator.getWorkspacePath() << std::endl;
            }
            else {
                if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"')
                    arg = arg.substr(1, arg.size() - 2);
                evaluator.setWorkspacePath(arg);
                std::cout << "   Workspace changed to: " << evaluator.getWorkspacePath() << std::endl;
            }
            continue;
        }

        if (input == "ls" || input.substr(0, 3) == "ls " ||
            input == "list" ||
            (input.substr(0, 5) == "list " && input.size() > 5 && input[4] != '(')) {
            std::string args;
            if (input.substr(0, 4) == "list") args = input.substr(4);
            else args = input.substr(2);

            // 去首尾空格
            size_t as = args.find_first_not_of(" \t");
            size_t ae = args.find_last_not_of(" \t");
            if (as != std::string::npos) args = args.substr(as, ae - as + 1);
            else args.clear();

            // 去除可能的引号
            if (args.size() >= 2 && args.front() == '"' && args.back() == '"')
                args = args.substr(1, args.size() - 2);

            // 解析：ls [path] [filter]
            std::string lsPath = evaluator.getWorkspacePath();
            std::string lsFilter;

            if (!args.empty()) {
                // 检查是否有空格分隔的 filter（如 "ls data *"）
                size_t sp = args.rfind(' ');
                if (sp != std::string::npos) {
                    std::string lastToken = args.substr(sp + 1);
                    // 如果最后一个 token 像 filter（* 或 .ext）
                    if (lastToken == "*" || lastToken == "all" ||
                        (lastToken.size() >= 2 && lastToken[0] == '.')) {
                        lsFilter = lastToken;
                        lsPath = args.substr(0, sp);
                        size_t ps = lsPath.find_last_not_of(" \t");
                        if (ps != std::string::npos) lsPath = lsPath.substr(0, ps + 1);
                    }
                    else {
                        lsPath = args;
                    }
                }
                else {
                    // 单个参数：如果是 * 或 .ext 则当 filter，否则当路径
                    if (args == "*" || args == "all" ||
                        (args.size() >= 2 && args[0] == '.')) {
                        lsFilter = args;
                    }
                    else {
                        lsPath = args;
                    }
                }

                // 相对路径解析
                namespace fs = std::filesystem;
                if (!fs::path(lsPath).is_absolute())
                    lsPath = (fs::current_path() / lsPath).string();
            }

            listWorkspaces(lsPath, lsFilter);
            continue;
        }
        if (input.substr(0, 3) == "rm " || input.substr(0, 7) == "remove ") {
            std::string filename = (input.substr(0, 3) == "rm ") ? input.substr(3) : input.substr(7);
            filename.erase(0, filename.find_first_not_of(" \t"));
            filename.erase(filename.find_last_not_of(" \t") + 1);
            if (filename.empty()) {
                std::cerr << "   FS Error: Please specify a workspace name to remove (e.g. rm my_work)." << std::endl;
            }
            else {
                removeWorkspace(filename, evaluator);
            }
            continue;
        }
        if (input.substr(0, 5) == "save ") {
            std::string filename = input.substr(5);
            filename.erase(0, filename.find_first_not_of(" \t"));
            filename.erase(filename.find_last_not_of(" \t") + 1);
            if (filename.empty()) {
                std::cerr << "   IO Error: Please specify a workspace name to save (e.g. save my_work)." << std::endl;
            }
            else {
                saveWorkspace(filename, evaluator);
            }
            continue;
        }
        if (input.substr(0, 4) == "run ") {
            std::string filepath = input.substr(4);
            filepath.erase(0, filepath.find_first_not_of(" \t"));
            filepath.erase(filepath.find_last_not_of(" \t") + 1);
            // 去除可能的引号包裹
            if (filepath.size() >= 2 && filepath.front() == '"' && filepath.back() == '"')
                filepath = filepath.substr(1, filepath.size() - 2);
            if (filepath.empty()) {
                std::cerr << "   IO Error: Please specify a script path (e.g. run my_script.jc2)." << std::endl;
            }
            else {
                runScript(filepath, evaluator);
            }
            continue;
        }
        if (input.substr(0, 5) == "load ") {
            std::string filename = input.substr(5);
            filename.erase(0, filename.find_first_not_of(" \t"));
            filename.erase(filename.find_last_not_of(" \t") + 1);
            if (filename.empty()) {
                std::cerr << "   IO Error: Please specify a workspace name to load (e.g. load my_work)." << std::endl;
            }
            else {
                loadWorkspace(filename, evaluator);
            }
            continue;
        }

        // --- 正常表达式求值 ---
        try {
            jc::Lexer lexer(input);
            auto tokens = lexer.tokenize();

            jc::Parser parser(tokens);
            auto ast = parser.parse();

            jc::Value result = evaluator.calculate(ast.get());

            if (!result.isNone()) {
                evaluator.setVariable("ANS", result);
                // ★ 类型着色输出
                std::string typeColor;
                std::visit([&typeColor](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, double> || std::is_same_v<T, jc::BigInt> || std::is_same_v<T, jc::Fraction>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_YELLOW);
                    else if constexpr (std::is_same_v<T, jc::Complex>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_MAGENTA);
                    else if constexpr (std::is_same_v<T, std::string>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_GREEN);
                    else if constexpr (std::is_same_v<T, jc::RealMatrix> || std::is_same_v<T, jc::ComplexMatrix>)
                        typeColor = jc::col(jc::Ansi::WHITE);
                    else if constexpr (std::is_same_v<T, jc::StringMatrix>)
                        typeColor = jc::col(jc::Ansi::GREEN);
                    else if constexpr (std::is_same_v<T, jc::BaseNum>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_CYAN);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::FunctionClosure>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_BLUE);
                    else if constexpr (std::is_same_v<T, jc::Dict> || std::is_same_v<T, jc::List>)
                        typeColor = jc::col(jc::Ansi::CYAN);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::ClassDefinition>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_BLUE);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::Instance>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_CYAN);
                    }, result.data);
                std::cout << typeColor << result << jc::col(jc::Ansi::RESET) << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << jc::col(jc::Ansi::BRIGHT_RED) << "   " << e.what()
                << jc::col(jc::Ansi::RESET) << std::endl;
        }
    }

    std::cout << "\nGoodbye! Thanks for using Junk Calculator 2.0" << std::endl;
    return 0;
}


"""
patch_chart_load.py — fix chart loading for easy-phi.

Problem: The format detection only checks for "Title"/"name" substrings
in the first 600 bytes. Charts that fail the DevJson load (e.g., missing
meta.Title) don't get retried with the in-game -> DevJson converter.

Fix: Wrap loadChart() in a try/catch. On failure, fall back to running
milchart_convert.py. Also expand the header read to 4KB so we don't miss
"Title" or "name" markers that appear past byte 600.

Apply to the .patched.cpp file (after patch_gcc13.py has run).
"""

import sys
import re
from pathlib import Path


OLD_BLOCK = r'''        auto loadChart = [&]() -> bool {
            try {
                // Auto-detect chart format and convert if needed
                {
                    std::ifstream f(chartPath, std::ios::binary);
                    if (f) {
                        std::string header(600, '\0');
                        f.read(&header[0], 600);
                        f.close();
                        // DevJson has "Title", in-game has "name" in meta
                        bool isDevJson = header.find("\"Title\"") != std::string::npos;
                        bool isIngame = header.find("\"name\"") != std::string::npos && !isDevJson;
                        if (isIngame) {
                            std::string convertedPath = chartPath + ".dev.json";
                            // Find converter script next to exe
                            char exePath[MAX_PATH];
                            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
                            std::string exeDir = std::filesystem::path(exePath).parent_path().string();
                            std::string converter = exeDir + "\\milchart_convert.py";
                            // Try multiple Python paths
                            const char* pyPaths[] = {
                                "C:\\Users\\Lenovo\\.workbuddy\\binaries\\python\\envs\\default\\Scripts\\python.exe",
                                "python", "python3", nullptr
                            };
                            bool converted = false;
                            for (int i = 0; pyPaths[i] && !converted; i++) {
                                std::string cmd = std::string("\"") + pyPaths[i] + "\" \"" + converter + "\" \"" + chartPath + "\" \"" + convertedPath + "\" 2>nul";
                                if (std::system(cmd.c_str()) == 0 && std::filesystem::exists(convertedPath)) {
                                    converted = true;
                                }
                            }
                            if (converted) {
                                std::cout << "已自动转换谱面格式 (官谱 -> DevJson)" << std::endl;
                                chartPath = convertedPath;
                            } else {
                                std::cout << "警告: 谱面为官谱格式但转换失败，将尝试直接加载" << std::endl;
                            }
                        }
                    }
                }
                window.loadChart(chartPath, chartDir);
                if (!imagePath.empty()) window.renderer->loadIllustion(imagePath);
                if (!audioPath.empty()) window.renderer->audioManager.load(audioPath);
                return true;
            } catch (const std::exception& e) {
                std::cout << "加载失败: " << e.what() << std::endl;
                return false;
            }
        };'''

NEW_BLOCK = r'''        // Helper: run milchart_convert.py to convert in-game chart -> DevJson
        auto tryConvertChart = [&](std::string& targetPath) -> bool {
            std::string convertedPath = targetPath + ".dev.json";
            char exePath[MAX_PATH];
            GetModuleFileNameA(nullptr, exePath, MAX_PATH);
            std::string exeDir = std::filesystem::path(exePath).parent_path().string();
            std::string converter = exeDir + "\\milchart_convert.py";
            const wchar_t* pyPaths[] = {
                L"C:\\Users\\Lenovo\\.workbuddy\\binaries\\python\\envs\\default\\Scripts\\python.exe",
                L"python", L"python3", nullptr
            };
            for (int i = 0; pyPaths[i]; i++) {
                std::wstring wpython = pyPaths[i];
                std::wstring wconverter = Win32Utils::stringToWstring(converter);
                std::wstring winput = Win32Utils::stringToWstring(targetPath);
                std::wstring woutput = Win32Utils::stringToWstring(convertedPath);
                // Use CreateProcessW to bypass cmd.exe's Unicode-mangling
                std::wstring wcmd = L"\"" + wpython + L"\" \"" + wconverter + L"\" \"" + winput + L"\" \"" + woutput + L"\"";
                std::wcout << L"[CONVERT] running: " << wpython << L"..." << std::endl;
                std::vector<wchar_t> cmdBuf(wcmd.begin(), wcmd.end());
                cmdBuf.push_back(L'\0');
                STARTUPINFOW si = {0};
                si.cb = sizeof(si);
                si.dwFlags = STARTF_USESTDHANDLES;
                si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
                si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                PROCESS_INFORMATION pi = {0};
                int rc = -1;
                if (CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, INFINITE);
                    DWORD exitCode = 0;
                    GetExitCodeProcess(pi.hProcess, &exitCode);
                    rc = (int)exitCode;
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                } else {
                    std::wcerr << L"[CONVERT] CreateProcessW failed: " << GetLastError() << std::endl;
                }
                std::wcout << L"[CONVERT] python rc=" << rc << L", checking: " << woutput << std::endl;
                if (rc == 0 && std::filesystem::exists(convertedPath)) {
                    std::cout << "已自动转换谱面格式 (官谱 -> DevJson)" << std::endl;
                    targetPath = convertedPath;
                    return true;
                }
            }
            return false;
        };

        auto loadChart = [&]() -> bool {
            try {
                // 1. Pre-detect format: read first 4KB
                bool isIngame = false;
                bool isDevJson = false;
                {
                    // IMPORTANT: use std::filesystem::path to support CJK paths on MinGW
                    std::ifstream f(std::filesystem::path(chartPath), std::ios::binary);
                    if (f) {
                        std::string header(4096, '\0');
                        f.read(&header[0], 4096);
                        f.close();
                        isDevJson = header.find("\"Title\"") != std::string::npos;
                        bool hasName = header.find("\"name\"") != std::string::npos;
                        isIngame = hasName && !isDevJson;
                    } else {
                        std::cout << "[DETECT] FAILED to open: " << chartPath << std::endl;
                    }
                }
                std::cout << "[DETECT] DevJson=" << (isDevJson ? "yes" : "no")
                          << ", in-game=" << (isIngame ? "yes" : "no")
                          << " (path=" << chartPath << ")" << std::endl;

                // 2. If in-game, pre-convert
                if (isIngame) {
                    if (!tryConvertChart(chartPath)) {
                        std::cout << "警告: 谱面为官谱格式但转换失败，仍尝试直接加载..." << std::endl;
                    }
                }

                // 3. Try DevJson load
                try {
                    window.loadChart(chartPath, chartDir);
                } catch (const std::exception& loadErr) {
                    // 4. If not pre-converted, try conversion as fallback
                    if (!isIngame) {
                        std::cout << "DevJson 加载失败 (\"" << loadErr.what() << "\")，尝试官谱转换..." << std::endl;
                        if (tryConvertChart(chartPath)) {
                            window.loadChart(chartPath, chartDir);
                        } else {
                            std::cout << "提示: 请确认谱面是合法 DevJson (含 meta.Title) 或官谱 (含 meta.name)" << std::endl;
                            std::cout << "      转换脚本需位于 milthm-player.exe 同目录 (milchart_convert.py)" << std::endl;
                            throw;
                        }
                    } else {
                        throw;
                    }
                }

                if (!imagePath.empty()) window.renderer->loadIllustion(imagePath);
                if (!audioPath.empty()) window.renderer->audioManager.load(audioPath);
                return true;
            } catch (const std::exception& e) {
                std::cout << "加载失败: " << e.what() << std::endl;
                return false;
            }
        };'''


def main():
    if len(sys.argv) < 2:
        print("Usage: python patch_chart_load.py <source.cpp.patched.cpp>")
        sys.exit(1)

    src_path = Path(sys.argv[1])
    if not src_path.exists():
        print(f"[ERROR] file not found: {src_path}")
        sys.exit(1)

    content = src_path.read_text(encoding="utf-8")

    if NEW_BLOCK in content:
        print("[SKIP] patch already applied")
        return

    if OLD_BLOCK not in content:
        print("[ERROR] old loadChart block not found in file.")
        print("Possible causes:")
        print("  - File was regenerated by grain but not yet patched")
        print("  - The 8-space indentation differs")

        for m in re.finditer(r"auto loadChart = ", content):
            start = max(0, m.start() - 50)
            print(f"\n  Found at offset {m.start()}:")
            print(f"  >>>{content[start:m.start() + 30]!r}<<<")
        sys.exit(2)

    new_content = content.replace(OLD_BLOCK, NEW_BLOCK, 1)
    if new_content == content:
        print("[ERROR] no change after replace — something is off")
        sys.exit(3)

    src_path.write_text(new_content, encoding="utf-8")
    print(f"[OK] patched: {src_path}")
    print(f"     before: {len(content)} bytes")
    print(f"     after:  {len(new_content)} bytes")


if __name__ == "__main__":
    main()

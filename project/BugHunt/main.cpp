/** @file main.cpp
 * @brief バグ探しの起動役（Engine.exe を --bug-hunt で立ち上げ、終わるまで待って、見つけたおかしな所を一覧にして出す）
 * @note 遊んでいる間の見張りはゲームの中身を直接見る必要があるので、ゲーム側（GamePlaySceneBugChecks.cpp）にある。
 *       このプロジェクトは起動と結果の読み取りだけを受け持つ。手順の表は同じフォルダの bug_hunt.json
 *       使い方: BugHunt をスタートアッププロジェクトにして実行する（先に CG2-1 をビルドしておく）。
 *       引数に --realtime を付けると、人が見られるふだんの速さで遊ぶ。--runs N で N 回続けて回す（付けなければ起動した時に聞く）
 */
#include "TestReport.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using engine::tests::testReport::Print;

constexpr const wchar_t* kEngineExeName = L"Engine.exe";
constexpr const wchar_t* kBugHuntFlag = L"--bug-hunt";
constexpr const wchar_t* kRealtimeFlag = L"--realtime";
constexpr const wchar_t* kRunsFlag = L"--runs"; // 続けて回す回数（例: --runs 5）
constexpr int kMaxRuns = 50; // 回す回数の上限（1回で十数分かかるので、うっかり大きな数を入れても止まるように）
constexpr int kDecimalBase = 10; // 回数を読む時の基数
constexpr int kInputBufferSize = 32; // 回数を入力してもらう時の1行の長さ
constexpr const char* kReportDirectory = "smoke_test";
constexpr const char* kReportPrefix = "result_";
constexpr const char* kIssueSectionHeader = "--- 見つけたおかしな所";
constexpr const char* kStepSectionHeader = "--- 手順の記録";
constexpr const char* kCrashDirectory = "crash";
constexpr DWORD kMaxWaitMilliseconds = 60u * 60u * 1000u; // 1時間たっても終わらなければ止まったとみなす
constexpr int kExitPassed = 0; // おかしな所なしで最後まで回った
constexpr int kExitIssuesFound = 1; // 最後まで回ったが、おかしな所が見つかった
constexpr int kExitFailed = 2; // 途中で止まった・落ちた・起動できなかった
constexpr int kOutputDirectoryDepth = 3; // 出力先（Generated/Outputs/構成）からリポジトリの一番上までの深さ
constexpr const char* kProjectFolderName = "project";

/** @brief 場所を表示用の UTF-8 の文字にする（Print は UTF-8 で受け取る） */
std::string ToUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

/** @brief この起動役の exe が置いてあるフォルダ */
fs::path ExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

/** @brief ゲームの作業フォルダ（Resources がある project フォルダ）。出力先から3つ上がってリポジトリの一番上へ行き、project へ入る */
fs::path FindProjectDirectory(const fs::path& exeDirectory) {
    fs::path root = exeDirectory;
    for (int i = 0; i < kOutputDirectoryDepth; ++i) {
        root = root.parent_path();
    }
    return root / kProjectFolderName;
}

/** @brief コマンド引数に flag が含まれているか */
bool HasFlag(const wchar_t* flag) {
    return std::wstring(GetCommandLineW()).find(flag) != std::wstring::npos;
}

/** @brief 報告のフォルダにある結果ファイルのうち、since より後に書かれた一番新しいもの（無ければ空） */
fs::path FindNewestReport(const fs::path& projectDirectory, fs::file_time_type since) {
    const fs::path directory = projectDirectory / kReportDirectory;
    std::error_code error;
    if (!fs::exists(directory, error)) {
        return fs::path();
    }
    fs::path newest;
    fs::file_time_type newestTime = since;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_regular_file() || name.rfind(kReportPrefix, 0) != 0) {
            continue;
        }
        const fs::file_time_type written = entry.last_write_time(error);
        if (written > newestTime) {
            newestTime = written;
            newest = entry.path();
        }
    }
    return newest;
}

/** @brief 落ちた時の報告が since より後に書かれていれば、その場所を返す（無ければ空） */
fs::path FindNewCrashReport(const fs::path& projectDirectory, fs::file_time_type since) {
    const fs::path directory = projectDirectory / kCrashDirectory;
    std::error_code error;
    if (!fs::exists(directory, error)) {
        return fs::path();
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (entry.is_regular_file() && entry.last_write_time(error) > since) {
            return entry.path();
        }
    }
    return fs::path();
}

/** @brief 結果ファイルを読み、見つけたおかしな所と最初の数行を出す（見つけた件数を返す。読めなければ -1） */
int PrintReport(const fs::path& reportPath) {
    std::ifstream file(reportPath);
    if (!file) {
        return -1;
    }
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    int issueCount = 0;
    bool inIssues = false;
    Print("\n==== バグ探しの結果 ====\n");
    for (const std::string& line : lines) {
        if (line.rfind(kIssueSectionHeader, 0) == 0) {
            inIssues = true;
            Print("\n[見つけたおかしな所]\n");
            continue;
        }
        if (line.rfind(kStepSectionHeader, 0) == 0) {
            break;
        }
        if (inIssues) {
            // 1件は [種類] の行で始まり、続く字下げの行に様子とその時の手順が書いてある
            if (!line.empty() && line.front() == '[') {
                ++issueCount;
            }
            Print("%s\n", line.c_str());
        } else {
            Print("%s\n", line.c_str());
        }
    }
    Print("\n結果のファイル: %s\n", ToUtf8(reportPath).c_str());
    return issueCount;
}

/** @brief Engine.exe を --bug-hunt で起動し、終わるまで待つ（起動できなければ false） */
bool RunGame(const fs::path& engineExe, const fs::path& projectDirectory, bool realtime, DWORD& exitCode) {
    std::wstring commandLine = L"\"" + engineExe.wstring() + L"\" " + kBugHuntFlag;
    if (realtime) {
        commandLine += std::wstring(L" ") + kRealtimeFlag;
    }
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
            projectDirectory.wstring().c_str(), &startup, &process)) {
        return false;
    }
    const DWORD waited = WaitForSingleObject(process.hProcess, kMaxWaitMilliseconds);
    if (waited == WAIT_TIMEOUT) {
        Print("1時間たっても終わらないので、ゲームを止めました（どこかで固まっています）\n");
        TerminateProcess(process.hProcess, static_cast<UINT>(kExitFailed));
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

/** @brief コンソールから実行した時だけ、結果を読めるように Enter まで待つ */
void WaitForEnterIfInteractive() {
    DWORD mode = 0;
    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        Print("\nEnter で閉じます\n");
        std::getchar();
    }
}
/** @brief 1回分の結果 */
struct RunResult {
    bool passed = false; ///< 最後まで回り、落ちなかったか
    int issueCount = 0; ///< 見つけたおかしな所の件数
    fs::path report; ///< 結果のファイル（書かれなかった時は空）
};

/** @brief コマンド引数の --runs N を読む（無いか読めなければ 0） */
int ReadRunsArgument() {
    const std::wstring commandLine = GetCommandLineW();
    const size_t found = commandLine.find(kRunsFlag);
    if (found == std::wstring::npos) {
        return 0;
    }
    const wchar_t* number = commandLine.c_str() + found + std::wcslen(kRunsFlag);
    return static_cast<int>(std::wcstol(number, nullptr, kDecimalBase));
}

/** @brief 何回回すかを決める（--runs があればそれ、コンソールから実行した時は聞く、どちらでもなければ1回） */
int DecideRunCount() {
    int runs = ReadRunsArgument();
    DWORD mode = 0;
    if (runs <= 0 && GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        Print("何回回しますか？（1〜%d。Enter だけなら1回。毎回ちがう遊び方にするには bug_hunt.json の seed を 0 にしておく）: ", kMaxRuns);
        char line[kInputBufferSize] = {};
        if (std::fgets(line, sizeof(line), stdin) != nullptr) {
            runs = std::atoi(line);
        }
    }
    return std::clamp(runs <= 0 ? 1 : runs, 1, kMaxRuns);
}

/** @brief バグ探しを1回回して、結果を一覧にして出す */
RunResult RunOnce(const fs::path& engineExe, const fs::path& projectDirectory, bool realtime) {
    RunResult result;
    // 結果ファイルは時刻で見分けるので、始める前の時刻を覚えておく（ファイルの時刻と同じ時計で測る）
    const fs::file_time_type startTime = fs::file_time_type::clock::now();
    DWORD exitCode = 0;
    if (!RunGame(engineExe, projectDirectory, realtime, exitCode)) {
        Print("Engine.exe を起動できませんでした（エラー %lu）\n", GetLastError());
        return result;
    }

    const fs::path crashReport = FindNewCrashReport(projectDirectory, startTime);
    if (!crashReport.empty()) {
        Print("\nゲームが落ちました。落ちた時の報告: %s\n", ToUtf8(crashReport).c_str());
    }
    result.report = FindNewestReport(projectDirectory, startTime);
    if (result.report.empty()) {
        Print("\n結果のファイルが書かれていません（途中で落ちたか、閉じられました。終了コード %lu）\n", exitCode);
        return result;
    }
    result.issueCount = (std::max)(0, PrintReport(result.report));
    std::ifstream firstLine(result.report);
    std::string verdict;
    std::getline(firstLine, verdict);
    result.passed = verdict == "成功" && crashReport.empty();
    if (!result.passed) {
        Print("\n途中で止まりました（上の「理由」を見てください）\n");
    } else if (result.issueCount > 0) {
        Print("\n最後まで回りました。おかしな所が %d 件見つかりました\n", result.issueCount);
    } else {
        Print("\n最後まで回りました。おかしな所は見つかりませんでした\n");
    }
    return result;
}
} // namespace

/** @brief バグ探しを決めた回数だけ回し、回ごとの結果と合計を出す */
int main() {
    const fs::path exeDirectory = ExecutableDirectory();
    const fs::path engineExe = exeDirectory / kEngineExeName;
    const fs::path projectDirectory = FindProjectDirectory(exeDirectory);
    std::error_code error;
    if (!fs::exists(engineExe, error)) {
        Print("Engine.exe が見つかりません: %s（先に CG2-1 をビルドしてください）\n", ToUtf8(engineExe).c_str());
        WaitForEnterIfInteractive();
        return kExitFailed;
    }
    if (!fs::exists(projectDirectory / "Resources", error)) {
        Print("ゲームの作業フォルダが見つかりません: %s\n", ToUtf8(projectDirectory).c_str());
        WaitForEnterIfInteractive();
        return kExitFailed;
    }

    const bool realtime = HasFlag(kRealtimeFlag);
    const int runCount = DecideRunCount();
    Print("バグ探しを %d 回回します（%s）。1回で全ステージとラスボス戦を回します（ふだんの速さだと十数分。速さは bug_hunt.json の speed）\n",
        runCount, realtime ? "ふだんの速さ" : "速回し");
    Print("ゲーム: %s\n作業フォルダ: %s\n", ToUtf8(engineExe).c_str(), ToUtf8(projectDirectory).c_str());

    std::vector<RunResult> results;
    for (int run = 1; run <= runCount; ++run) {
        Print("\n######## %d / %d 回目 ########\n", run, runCount);
        results.push_back(RunOnce(engineExe, projectDirectory, realtime));
    }

    // 回ごとの結果と合計（1回だけの時は上の結果がそのまま全部なので出さない）
    int totalIssues = 0;
    bool allPassed = true;
    for (const RunResult& result : results) {
        totalIssues += result.issueCount;
        allPassed = allPassed && result.passed;
    }
    if (runCount > 1) {
        Print("\n==== %d 回のまとめ ====\n", runCount);
        for (size_t i = 0; i < results.size(); ++i) {
            const RunResult& result = results[i];
            Print("%zu 回目: %s・おかしな所 %d 件  %s\n", i + 1, result.passed ? "最後まで回った" : "途中で止まった",
                result.issueCount, result.report.empty() ? "（結果のファイル無し）" : ToUtf8(result.report).c_str());
        }
        Print("合計: おかしな所 %d 件\n", totalIssues);
    }
    WaitForEnterIfInteractive();
    if (!allPassed) {
        return kExitFailed;
    }
    return totalIssues > 0 ? kExitIssuesFound : kExitPassed;
}

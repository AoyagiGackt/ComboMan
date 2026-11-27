/**
 * @file SmokeTestRunnerReport.cpp
 * @brief 自動テストプレイの結果の報告・網羅のまとめ・再生ファイルの書き出しと、再生の進め方を実装するファイル
 * @note SmokeTestRunner の報告まわりだけを SmokeTestRunner.cpp から分けて置いている（クラスは SmokeTestRunner のまま）
 */
#include "SmokeTestRunner.h"
#include "CrashBreadcrumbs.h"
#include "EngineConstants.h"
#include "Input.h"
#include "Logger.h"
#include "TestCoverage.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace engine {

namespace {
constexpr const char* kReportDirectory = "smoke_test"; // 報告を書くフォルダ
constexpr size_t kReportPathCapacity = 256; // 報告の場所の入れ物の大きさ
constexpr size_t kMaxUntriedNames = 40; // 網羅のまとめに出す、まだ試していない物の名前の数の上限
constexpr const char* kReplayPrefix = "replay"; // 再生ファイルの名前の頭
constexpr const char* kReplayExtension = "json"; // 再生ファイルの拡張子
} // namespace

/** @brief 今の時刻を入れた、報告のフォルダの中の場所を作る */
std::string SmokeTestRunner::ReportPath(const char* prefix, const char* extension) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[kReportPathCapacity];
    std::snprintf(path, sizeof(path), "%s/%s_%04d%02d%02d_%02d%02d%02d.%s", kReportDirectory, prefix,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, extension);
    return path;
}

/** @brief 見張りをやめて、終わったことにする */
void SmokeTestRunner::Finish() {
    monitors_.Stop();
    sMonitors = nullptr;
    finished_ = true;
    // 閉じる前に、本物の入力を戻しておく
    if (input_ != nullptr) {
        input_->SetPhysicalInputEnabled(true);
    }
}

/** @brief 再生の時、見せたいフレームに近づいたらふつうの速さにし、見せ終えたら終える */
bool SmokeTestRunner::UpdateReplay() {
    if (!replay_.IsPlaying() || replayTargetFrame_ == 0) {
        return false;
    }
    // 見せたいフレームの少し手前から、人が見られるふつうの速さにする
    if (!replayRealtime_ && totalFrames_ + replayLeadFrames_ >= replayTargetFrame_) {
        replayRealtime_ = true;
        log_.push_back("    frame " + std::to_string(totalFrames_) + " からふつうの速さで見せる");
        Logger::LogInfo("[SmokeTest] 再生: 見つけた所が近いので、ふつうの速さにします");
    }
    // 見せたいフレームに来たら、何を見つけた所かを出し、画面も残す
    if (!replayReachedTarget_ && totalFrames_ >= replayTargetFrame_) {
        replayReachedTarget_ = true;
        std::string label = replay_.Data().failReason;
        for (const ReplayIssue& issue : replay_.Data().issues) {
            if (issue.frame == replayTargetFrame_) {
                label = issue.kind + " " + issue.subject;
                break;
            }
        }
        log_.push_back("    frame " + std::to_string(totalFrames_) + ": 記録した時はここで見つかった（" + label + "）");
        Logger::LogWarning("[SmokeTest] 再生: 記録した時はここで見つかった（" + label + "）");
        monitors_.RequestScreenshot(0);
    }
    // 見せたいフレームの後、少し見せてから終える
    if (replayReachedTarget_ && totalFrames_ >= replayTargetFrame_ + replayAfterFrames_) {
        ReleaseKeys();
        sExpectProgress = false;
        log_.push_back("再生を終えた");
        Finish();
        WriteReport(true, "");
        return true;
    }
    return false;
}

/** @brief 報告に、何を試したか（網羅）のまとめを書く */
void SmokeTestRunner::WriteCoverage(std::ostream& out) {
    out << "--- 試した物（網羅。試した数 / 全部の数） ---\n";
    bool any = false;
    testCoverage::ForEach([&](const std::string& category, const std::set<std::string>& declared, const std::set<std::string>& covered) {
        any = true;
        // 試せる物を知らせていない種類は、試した種類の数だけ出す
        if (declared.empty()) {
            out << category << ": " << covered.size() << " 種類を試した\n";
            return;
        }
        size_t coveredCount = 0;
        size_t untriedCount = 0;
        std::string untried;
        for (const std::string& name : declared) {
            if (covered.contains(name)) {
                ++coveredCount;
                continue;
            }
            if (untriedCount < kMaxUntriedNames) {
                untried += (untried.empty() ? "" : ", ") + name;
            }
            ++untriedCount;
        }
        out << category << ": " << coveredCount << " / " << declared.size() << '\n';
        if (untriedCount > 0) {
            out << "    まだ試していない: " << untried;
            if (untriedCount > kMaxUntriedNames) {
                out << " ほか " << (untriedCount - kMaxUntriedNames) << " 個";
            }
            out << '\n';
        }
    });
    if (!any) {
        out << "（数えていない）\n";
    }
}

/** @brief おかしな所があったか止まった時に、再生ファイルを書く */
std::string SmokeTestRunner::WriteReplay(bool passed, const std::string& reason) const {
    // 再生している時と、何も見つからずに最後まで回った時は書かない
    if (replay_.IsPlaying() || (passed && sIssues.empty())) {
        return std::string();
    }
    ReplayData data;
    data.scriptPath = scriptPath_;
    data.script = script_;
    data.seed = seed_;
    data.bugHunt = bugHunt_;
    data.selection = changes_.ExportSelection();
    for (const Issue& issue : sIssues) {
        data.issues.push_back({ issue.kind, issue.subject, issue.firstFrame });
    }
    data.failReason = passed ? std::string() : reason;
    data.failFrame = passed ? 0 : totalFrames_;
    data.totalFrames = totalFrames_;
    const std::string path = ReportPath(kReplayPrefix, kReplayExtension);
    return replay_.Save(path, data) ? path : std::string();
}

/** @brief 結果のファイルを書き出す */
void SmokeTestRunner::WriteReport(bool passed, const std::string& reason) const {
    const std::string headline = passed ? "[SmokeTest] 成功: 全部の手順を終えました" : "[SmokeTest] 失敗: " + reason;
    if (passed) {
        Logger::LogInfo(headline);
    } else {
        Logger::LogError(headline);
    }
    CrashBreadcrumbs::Add("smoke", passed ? "自動テスト成功" : "自動テスト失敗");

    std::error_code error;
    std::filesystem::create_directories(kReportDirectory, error);
    const std::string replayPath = WriteReplay(passed, reason);
    const std::string path = ReportPath("result", "txt");
    std::ofstream report(path);
    if (!report) {
        return;
    }
    const std::lock_guard<std::mutex> lock(sIssueMutex);
    report << (passed ? "成功" : "失敗") << '\n';
    if (!passed) {
        report << "理由: " << reason << '\n';
    }
    report << "手順の表: " << scriptPath_ << "  ランダムの種: " << seed_ << '\n';
    if (bugHunt_) {
        report << "回した所: " << changes_.Summary();
        game_->WriteChangesReport(report);
        report << '\n';
        // 最後まで回った時だけ、次に比べる記録を書く（起動役が全部の回を終えてから置き換える）
        if (passed) {
            changes_.WriteNextState();
        }
    }
    if (!replayPath.empty()) {
        report << "再生ファイル: " << replayPath << "（BugHunt を --replay " << replayPath
               << " で起動すると、見つけた所の少し手前からふつうの速さで同じ動きを見られる）\n";
    }
    // 再生の時は、記録した時に見つけた物がもう一度見つかったかを並べる
    if (replay_.IsPlaying()) {
        report << "再生した記録: " << replayPath_ << '\n';
        for (const ReplayIssue& recorded : replay_.Data().issues) {
            const bool reproduced = std::any_of(sIssues.begin(), sIssues.end(), [&](const Issue& issue) {
                return issue.kind == recorded.kind && issue.subject == recorded.subject;
            });
            report << (reproduced ? "  再現した: " : "  再現しなかった: ") << recorded.kind << " " << recorded.subject
                   << "（記録では frame " << recorded.frame << "）\n";
        }
    }
    report << "かかったフレーム数: " << totalFrames_ << "（約 " << static_cast<int>(static_cast<float>(totalFrames_) * engineConstants::kFrameDeltaTime) << " 秒）\n";
    report << "見つけたおかしな所: " << sIssues.size() << " 件\n";
    report << "--- 見つけたおかしな所（見つけた順） ---\n";
    for (const Issue& issue : sIssues) {
        report << "[" << issue.kind << "] " << issue.subject << "（" << issue.count << " 回、最初は frame " << issue.firstFrame << "）\n";
        report << "    様子: " << issue.detail << '\n';
        report << "    その時: " << issue.context << '\n';
        if (!issue.screenshot.empty()) {
            report << "    画面: " << issue.screenshot << '\n';
        }
    }
    WriteCoverage(report);
    monitors_.WriteSummary(report);
    report << "--- 手順の記録 ---\n";
    for (const std::string& line : log_) {
        report << line << '\n';
    }
    if (!passed) {
        report << "--- 直前の出来事（古い順） ---\n";
        CrashBreadcrumbs::ForEach([&](const CrashBreadcrumbs::Entry& entry) {
            report << "[frame " << entry.frame << "] " << entry.category << ": " << entry.text << '\n';
        });
    }
    Logger::LogInfo("[SmokeTest] 結果を書き出しました: " + path);
}

} // namespace engine

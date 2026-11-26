/**
 * @file SmokeTestMonitors.cpp
 * @brief 自動テストプレイの間、ゲームの中身とは別の見えにくい不具合を見張る部品を実装するファイル
 */
#include "SmokeTestMonitors.h"
#include "DirectXCommon.h"
#include "PoolOverflowCounter.h"
#include "SmokeTestRunner.h"
#include "StringUtility.h"
#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cstdio>

namespace engine::game {

namespace {
constexpr double kBytesPerMb = 1024.0 * 1024.0; // バイトを MB に直す割り算の数
constexpr uint64_t kPoolCheckIntervalFrames = 60; // プールの溢れを調べる間隔（約1秒）
constexpr const char* kScreenshotRoot = "smoke_test/shots"; // 画面を保存するフォルダの親
constexpr size_t kFileNameCapacity = 64; // 画面のファイル名の入れ物の大きさ
constexpr size_t kNumberTextCapacity = 32; // 数を文字にする時の入れ物の大きさ

/** @brief 小数を少しの桁だけ文字にする */
std::string FormatNumber(double value) {
    char text[kNumberTextCapacity];
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
}
} // namespace

/** @brief 見張りを始める */
void SmokeTestMonitors::Start(DirectXCommon* dxCommon, const Settings& settings, const std::string& runTag) {
    dxCommon_ = dxCommon;
    settings_ = settings;
    screenshotFolder_ = std::string(kScreenshotRoot) + "/" + runTag;
    running_ = true;
    if (dxCommon_ == nullptr) {
        return;
    }
    // デバッグレイヤーのエラーで止まると無人で回せないので、止めずに記録して先へ進む
    dxCommon_->SetBreakOnDebugErrors(false);
    dxCommon_->SetDebugMessageHandler([](bool isError, int id, const std::string& description) {
        SmokeTestRunner::ReportIssue(isError ? "D3D12 のエラー" : "D3D12 の警告", "id " + std::to_string(id), description);
    });
}

/** @brief 見張りをやめる */
void SmokeTestMonitors::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (dxCommon_ != nullptr) {
        dxCommon_->SetDebugMessageHandler({ });
        dxCommon_->SetBreakOnDebugErrors(true);
    }
}

/** @brief 1回の更新を始める */
void SmokeTestMonitors::BeginUpdate() {
    if (!running_) {
        return;
    }
    updateStart_ = std::chrono::steady_clock::now();
    updating_ = true;
}

/** @brief 1回の更新を終え、かかった時間を数える */
void SmokeTestMonitors::EndUpdate(bool settled, const std::string& scene) {
    if (!running_ || !updating_) {
        return;
    }
    updating_ = false;
    // 読み込み中や切り替え直後は重くて当然なので数えない
    if (!settled) {
        return;
    }
    const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - updateStart_).count();
    UpdateStats& stats = updateStats_[scene];
    ++stats.count;
    stats.totalMs += ms;
    stats.maxMs = (std::max)(stats.maxMs, ms);
    if (ms >= settings_.slowUpdateMs) {
        ++stats.slowCount;
    }
    if (ms >= settings_.updateSpikeMs) {
        SmokeTestRunner::ReportIssue("重い更新", scene, "ゲームを1回進めるのに " + FormatNumber(ms) + " ミリ秒かかった");
    }
}

/** @brief 今のメモリの量を測る */
SmokeTestMonitors::MemorySample SmokeTestMonitors::Measure(const std::string& label) const {
    MemorySample sample;
    sample.label = label;
    PROCESS_MEMORY_COUNTERS_EX counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
        sample.cpuMb = static_cast<double>(counters.PrivateUsage) / kBytesPerMb;
    }
    if (dxCommon_ != nullptr) {
        sample.gpuMb = static_cast<double>(dxCommon_->GetGpuMemoryUsage()) / kBytesPerMb;
    }
    return sample;
}

/** @brief ステージに入って落ち着いた時に、メモリの量を測る */
void SmokeTestMonitors::OnStageEntered(const std::string& label) {
    if (!running_) {
        return;
    }
    memorySamples_.push_back(Measure(label));
    if (memorySamples_.size() < 2) {
        return;
    }
    // 最初に入った時と比べて、ステージを出入りするたびに片付けきれずに増えていないか
    const MemorySample& first = memorySamples_.front();
    const MemorySample& last = memorySamples_.back();
    if (!cpuGrowthReported_ && last.cpuMb - first.cpuMb >= settings_.memoryGrowthMb) {
        cpuGrowthReported_ = true;
        SmokeTestRunner::ReportIssue("メモリが増え続ける", "CPU",
            first.label + " で " + FormatNumber(first.cpuMb) + " MB だったのが、" + last.label + " で " + FormatNumber(last.cpuMb) + " MB");
    }
    if (!gpuGrowthReported_ && last.gpuMb - first.gpuMb >= settings_.memoryGrowthMb) {
        gpuGrowthReported_ = true;
        SmokeTestRunner::ReportIssue("メモリが増え続ける", "GPU",
            first.label + " で " + FormatNumber(first.gpuMb) + " MB だったのが、" + last.label + " で " + FormatNumber(last.gpuMb) + " MB");
    }
}

/** @brief プールが溢れた回数を見て、初めて溢れたプールを知らせる */
void SmokeTestMonitors::CheckPools(uint64_t frame) {
    if (!running_ || frame % kPoolCheckIntervalFrames != 0) {
        return;
    }
    engine::poolOverflowCounter::ForEach([this](const std::string& poolName, uint64_t count) {
        if (reportedPools_.contains(poolName)) {
            return;
        }
        reportedPools_[poolName] = count;
        SmokeTestRunner::ReportIssue("プールが溢れた", poolName,
            "用意した数が足りず、頼みを " + std::to_string(count) + " 回捨てた（演出や弾が出ていないかもしれない。合計はまとめに出る）");
    });
}

/** @brief おかしな所が初めて見つかった時に、その時の画面を保存するよう頼む */
std::string SmokeTestMonitors::RequestScreenshot(size_t index) {
    if (!running_ || dxCommon_ == nullptr || screenshotCount_ >= settings_.maxScreenshots) {
        return std::string();
    }
    ++screenshotCount_;
    char name[kFileNameCapacity];
    std::snprintf(name, sizeof(name), "/issue_%03zu.png", index);
    const std::string path = screenshotFolder_ + name;
    // 描き終えた次の絵を写すので、見つけた瞬間から少しだけ後の画面になる
    dxCommon_->RequestScreenshot(StringUtility::ConvertString(path));
    return path;
}

/** @brief 報告の最後に、重さ・メモリ・プールのまとめを書く */
void SmokeTestMonitors::WriteSummary(std::ostream& out) const {
    out << "--- 更新の重さ（画面ごと。読み込み中は除く） ---\n";
    for (const auto& [scene, stats] : updateStats_) {
        const double average = stats.count > 0 ? stats.totalMs / static_cast<double>(stats.count) : 0.0;
        out << scene << ": " << stats.count << " 回、平均 " << FormatNumber(average) << " ミリ秒、最大 " << FormatNumber(stats.maxMs)
            << " ミリ秒、" << FormatNumber(settings_.slowUpdateMs) << " ミリ秒以上 " << stats.slowCount << " 回\n";
    }
    out << "--- ステージに入った時のメモリ ---\n";
    for (const MemorySample& sample : memorySamples_) {
        out << sample.label << ": CPU " << FormatNumber(sample.cpuMb) << " MB、GPU " << FormatNumber(sample.gpuMb) << " MB\n";
    }
    out << "--- プールが溢れた回数 ---\n";
    bool anyPool = false;
    engine::poolOverflowCounter::ForEach([&](const std::string& poolName, uint64_t count) {
        out << poolName << ": " << count << " 回\n";
        anyPool = true;
    });
    if (!anyPool) {
        out << "なし\n";
    }
}

} // namespace engine::game

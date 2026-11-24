/**
 * @file FrameProfiler.h
 * @brief CPUのフレーム時間とFPSを測る処理（FrameProfiler）
 */
#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
namespace engine::graphics {

/** @brief 1フレームの中で時間を測る段階（どこで時間がかかったかを調べるため） */
enum class FramePhase {
    kUpdate, ///< 更新
    kDraw, ///< 描画（GPU 待ちと画面へ出す時間も含む）
    kGpuWait, ///< 描画の始めに、前に同じ画面を使った時の GPU の処理が終わるのを待った時間
    kPresent, ///< 描き終えた画面を出す時間
    kCount,
};

// フレームタイムとFPSをトラッキングするシングルトン
// BeginFrame() / EndFrame() を毎フレーム呼ぶだけで使える
class FrameProfiler {
public:
    /** @brief 共有インスタンスを取得する */
    static FrameProfiler* GetInstance() {
        static FrameProfiler sInst;
        return &sInst;
    }

    /** @brief フレームの計測を始める */
    void BeginFrame() {
        // 前のフレームの段階ごとの時間を残し、今のフレームの分を数え直す
        lastPhaseMs_ = phaseMs_;
        phaseMs_ = { };
        // フレーム開始時刻を記録する
        QueryPerformanceCounter(&frameStart_);
    }

    /** @brief 今のフレームの段階に時間を足す（同じ段階を何度測っても足し合わせる） */
    void AddPhaseMs(FramePhase phase, float ms) {
        phaseMs_[static_cast<size_t>(phase)] += ms;
    }
    /** @brief 前のフレームでその段階にかかった時間（ミリ秒） */
    float GetLastPhaseMs(FramePhase phase) const {
        return lastPhaseMs_[static_cast<size_t>(phase)];
    }
    /** @brief 今のフレームが始まってからの時間（ミリ秒） */
    float GetMsSinceFrameStart() const {
        return MsSince(frameStart_);
    }
    /** @brief 時刻 start からの時間（ミリ秒） */
    static float MsSince(const LARGE_INTEGER& start) {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        return float(now.QuadPart - start.QuadPart) / float(freq.QuadPart) * kMillisecondsPerSecond;
    }

    /** @brief フレームの計測を終え、FPSと処理時間を更新する */
    void EndFrame() {
        LARGE_INTEGER now, freq;
        // フレーム終了時刻とパフォーマンスカウンタの周波数を取得し、経過時間(ms)を算出する
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float ms = float(now.QuadPart - frameStart_.QuadPart) / float(freq.QuadPart) * kMillisecondsPerSecond;

        samples_[sampleIdx_] = ms;
        sampleIdx_ = (sampleIdx_ + 1) % kSamples;

        float sum = 0.0f;
        for (float s : samples_) {
            sum += s;
        }
        avgMs_ = sum / kSamples;
        fps_ = avgMs_ > 0.0f ? kMillisecondsPerSecond / avgMs_ : 0.0f;
    }

    /** @brief FPS を返す */
    float GetFPS() const {
        return fps_;
    }
    /** @brief 1フレームの処理時間（ミリ秒）を返す */
    float GetMs() const {
        return avgMs_;
    }

private:
    static constexpr float kMillisecondsPerSecond = 1000.0f;
    /** @brief 計測前のフレーム統計を生成する */
    FrameProfiler() = default;
    static constexpr int kSamples = 60;
    std::array<float, kSamples> samples_ = { };
    int sampleIdx_ = 0;
    float avgMs_ = 0.0f;
    float fps_ = 0.0f;
    LARGE_INTEGER frameStart_ = { };
    static constexpr size_t kPhaseCount = static_cast<size_t>(FramePhase::kCount);
    std::array<float, kPhaseCount> phaseMs_ = { }; ///< 今のフレームの段階ごとの時間
    std::array<float, kPhaseCount> lastPhaseMs_ = { }; ///< 前のフレームの段階ごとの時間
};

/** @brief 作ってから消えるまでの時間を、今のフレームの段階に足す */
class ScopedFramePhase {
public:
    /** @brief 測り始める */
    explicit ScopedFramePhase(FramePhase phase) : phase_(phase) {
        QueryPerformanceCounter(&start_);
    }
    /** @brief 測り終えて足す */
    ~ScopedFramePhase() {
        FrameProfiler::GetInstance()->AddPhaseMs(phase_, FrameProfiler::MsSince(start_));
    }
    ScopedFramePhase(const ScopedFramePhase&) = delete;
    ScopedFramePhase& operator=(const ScopedFramePhase&) = delete;

private:
    FramePhase phase_;
    LARGE_INTEGER start_ = { };
};

} // namespace engine::graphics

/**
 * @file AudioStream.cpp
 * @brief 曲を小分けに読み込む再生処理
 */
#include "Audio.h"
#include "Logger.h"
#include <atomic>
#include <algorithm>

namespace engine {
/** @brief 再生完了を受け取り、使用中の領域を読み込み側へ返す */
struct Audio::StreamState final : IXAudio2VoiceCallback {
    /** @brief 音声ボイスに渡す間、位置と内容を固定する領域 */
    struct Slot {
        std::vector<byte> bytes; ///< PCM の塊
        std::atomic_bool queued{false}; ///< 声に渡して鳴らし終わるのを待っている間 true（音の側のスレッドが false に戻す）
    };
    static constexpr size_t kBufferCount = 3; ///< 回して使う塊の数
    static constexpr UINT32 kChunksPerSecond = 4; ///< 1秒を何個の塊に分けるか（1つが約 0.25 秒）
    std::array<Slot, kBufferCount> slots; ///< 回して使う塊
    Microsoft::WRL::ComPtr<IMFSourceReader> reader; ///< 曲を PCM にしながら読む物
    size_t targetBytes = 0; ///< 1つの塊に詰めるバイト数
    bool loop = true; ///< 終わりまで来たら頭に戻るか
    bool ended = false; ///< 最後まで読んだか
    bool readSinceSeek = false; ///< 頭に戻ってから1バイトでも読めたか（空の曲で戻り続けないため）
    std::atomic_bool failed{false}; ///< 音の側でエラーが出たか

    /** @brief 読み終わった領域を更新側に返す */
    void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override {
        static_cast<Slot*>(context)->queued.store(false, std::memory_order_release);
    }
    /** @brief デバイスの異常を更新側に通知する */
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) noexcept override { failed.store(true); }
    /** @brief 処理開始の通知を受ける */
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    /** @brief 処理完了の通知を受ける */
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    /** @brief 再生終了の通知を受ける */
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
    /** @brief バッファ開始の通知を受ける */
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override {}
    /** @brief 繰り返し終了の通知を受ける */
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
};

/** @brief 曲の先頭を読み込んでから再生を開始する */
void Audio::PlayStreamBGM(const std::string& filePath, bool loop) {
    StopBGM();
    auto state = std::make_shared<StreamState>();
    WAVEFORMATEX format{};
    if (FAILED(OpenReader(filePath, state->reader, format))) {
        Logger::LogWarning("曲を開けませんでした: " + filePath);
        return;
    }
    state->loop = loop;
    state->targetBytes = (std::max)(static_cast<UINT32>(format.nBlockAlign),
        static_cast<UINT32>(format.nAvgBytesPerSec / StreamState::kChunksPerSecond));
    bgmVoice_ = CreateSourceVoice(format, AudioBus::kBgm, state.get());
    if (!bgmVoice_) {
        return;
    }
    stream_ = std::move(state);
    bgmFadeDuration_ = 0.0f;
    bgmVoice_->SetVolume(bgmCurrentVolume_);
    FillStream();
    if (bgmVoice_ && FAILED(bgmVoice_->Start())) {
        StopBGM();
    }
}

/** @brief 更新スレッドで空き領域に続きのサンプルをまとめて積む */
void Audio::FillStream() {
    if (!stream_) {
        return;
    }
    auto& state = *stream_;
    HRESULT hr = state.failed.load() ? E_FAIL : S_OK;
    for (auto& slot : state.slots) {
        if (FAILED(hr) || state.ended) {
            break;
        }
        if (slot.queued.load(std::memory_order_acquire)) {
            continue;
        }
        slot.bytes.clear();
        while (slot.bytes.size() < state.targetBytes && !state.ended) {
            const size_t before = slot.bytes.size();
            bool eof = false;
            hr = AppendSample(state.reader.Get(), slot.bytes, eof);
            if (FAILED(hr)) {
                break;
            }
            state.readSinceSeek |= before != slot.bytes.size();
            if (eof) {
                if (!state.loop || !state.readSinceSeek) {
                    state.ended = true;
                    break;
                }
                // 空の曲で先頭への移動を繰り返さないようにする。
                PROPVARIANT start{};
                start.vt = VT_I8;
                hr = state.reader->SetCurrentPosition(GUID_NULL, start);
                state.readSinceSeek = false;
                if (FAILED(hr)) {
                    break;
                }
            }
        }
        if (FAILED(hr) || slot.bytes.empty()) {
            continue;
        }
        XAUDIO2_BUFFER buffer{};
        buffer.pAudioData = slot.bytes.data();
        buffer.AudioBytes = static_cast<UINT32>(slot.bytes.size());
        buffer.Flags = state.ended ? XAUDIO2_END_OF_STREAM : 0;
        buffer.pContext = &slot;
        slot.queued.store(true);
        hr = bgmVoice_->SubmitSourceBuffer(&buffer);
    }
    if (FAILED(hr)) {
        Logger::LogWarning("曲の読み込みまたは再生に失敗しました");
        StopBGM();
    } else if (state.ended && std::none_of(state.slots.begin(), state.slots.end(),
        [](const StreamState::Slot& slot) { return slot.queued.load(); })) {
        StopBGM();
    }
}
} // namespace engine

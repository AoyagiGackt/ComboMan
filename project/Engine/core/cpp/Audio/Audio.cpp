/**
 * @file Audio.cpp
 * @brief BGM・効果音（SE）の読み込みと再生を管理するクラス
 *
 * XAudio2 を使って音声を再生する
 * Media Foundation（MF）で音声ファイルをデコードし（MP3・WAV など対応）、
 * XAudio2 の SourceVoice に渡して再生する
 *
 * 【BGM】: 1曲だけ再生ループ可能Stop で停止、別のを再生するときは自動で前の曲を停止する
 * 【SE】 : 複数同時再生可能1つのSEが複数の SourceVoice を持てる再生終了したものは自動削除する
 */
#include "Audio.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "StringUtility.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <bit>
#include <mmreg.h>
#include <initializer_list>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

using namespace Microsoft::WRL;

// 初期化 / 終了

// 音声システムを初期化するゲーム起動時に1度だけ呼ぶ
namespace engine {
namespace {
/** @brief キーから曲を引く表と、その表のファイル、今鳴らしている曲のキーを保持する */
struct SceneBgmConfig {
    std::string path; ///< 表の JSON ファイル（ゲームが SetBgmTablePath で決める）
    nlohmann::json table = nlohmann::json::object();
    Audio* audio = nullptr;
    std::string key;
    bool loop = true;
};

/** @brief シーンの BGM 設定を取得する */
SceneBgmConfig& BgmConfig() {
    static SceneBgmConfig sConfig;
    return sConfig;
}

/**
 * @brief 音声データ全体を1つのバッファとして積み、再生を始める
 * @param voice 再生する SourceVoice
 * @param soundData 再生する PCM データ
 * @param loop true なら終わりまで来たら頭から繰り返す
 */
void SubmitAndStart(IXAudio2SourceVoice* voice, const SoundData& soundData, bool loop) {
    XAUDIO2_BUFFER buffer = { };
    buffer.pAudioData = soundData.pBuffer.data(); // PCM データの先頭ポインタ
    buffer.AudioBytes = soundData.bufferSize; // データのバイト数
    buffer.Flags = XAUDIO2_END_OF_STREAM; // これがバッファの終端であることを示す
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0; // ループ回数（無限 or 0 = ループなし）
    const HRESULT hr = voice->SubmitSourceBuffer(&buffer);
    ENGINE_ASSERT(SUCCEEDED(hr));
    voice->Start(0);
}
} // namespace

/** @brief オーディオエンジンの初期化 */
void Audio::Initialize() {
    HRESULT hr;

    // Media Foundation を初期化する（音声・動画のデコードに必要な Windows の仕組み）
    // MFSTARTUP_NOSOCKET = ソケット通信機能なしで起動（軽量化）
    hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // XAudio2 エンジンを作成する（音声の生成・加工・出力を担う中核）
    hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // マスタリングボイスを作成する（最終的な音声出力先スピーカーやヘッドホンにつながる）
    hr = xAudio2_->CreateMasteringVoice(&masteringVoice_);
    ENGINE_ASSERT(SUCCEEDED(hr));

    InitializeBuses();
    Logger::Log("Audio System Initialized.");
}

/** @brief メニューの選択移動音と決定音に使う音声ファイルを設定し、先に読み込んでおく */
void Audio::SetMenuSounds(const std::string& choicePath, const std::string& selectPath) {
    menuChoicePath_ = choicePath;
    menuSelectPath_ = selectPath;
    // 押した瞬間に読み込みで止まらないよう、ここで読んでおく
    for (const std::string& path : { menuChoicePath_, menuSelectPath_ }) {
        if (!path.empty()) {
            GetCachedSound(path);
        }
    }
}

/** @brief 再生を止めて出力先を順に解放する */
void Audio::Finalize() {
    if (!xAudio2_) {
        return; // 初期化されていなければ何もしない
    }

    // 再生中の音声をすべて停止する
    StopBGM();
    StopAllSE();
    StopAllVoice();
    soundCache_.clear();
    for (auto& bus : buses_) {
        if (bus) {
            bus->DestroyVoice();
            bus = nullptr;
        }
    }

    // マスタリングボイスを破棄する
    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }

    // XAudio2 エンジンを解放する
    xAudio2_.Reset();

    // Media Foundation を終了する
    MFShutdown();
}

// ロード

// 音声ファイル（MP3・WAV など）を読み込んでメモリに展開する
// 戻り値の SoundData を PlayBGM / PlaySE に渡して再生する
/** @brief 読み込み済みの音を再利用する */
const SoundData& Audio::GetCachedSound(const std::string& filename) {
    auto it = soundCache_.find(filename);
    if (it == soundCache_.end()) {
        it = soundCache_.emplace(filename, LoadAudio(filename)).first;
    }
    return it->second;
}

/** @brief メニューの選択移動音を再生する */
void Audio::PlayMenuChoice() {
    if (menuChoicePath_.empty()) {
        return;
    }
    PlaySE(GetCachedSound(menuChoicePath_));
}

/** @brief メニューの決定音を再生する */
void Audio::PlayMenuSelect() {
    if (menuSelectPath_.empty()) {
        return;
    }
    PlaySE(GetCachedSound(menuSelectPath_));
}

/** @brief 表でキーに割り当てた曲を、鳴らさずに読み込んでおく */
void Audio::PreloadSceneBGM(const std::string& sceneKey) {
    if (streamSceneBgm_) {
        return;
    }
    const auto& table = BgmConfig().table;
    const std::string path = table.is_object() ? table.value(sceneKey, std::string { }) : std::string { };
    if (path.empty()) {
        return;
    }
    // 読み込み済みなら何もしない（変換した物はアプリを閉じるまで使い回す）
    GetCachedSound(path);
}

/** @brief 表でキーに割り当てた曲を再生する（同じ曲が鳴っていれば続きから流す） */
void Audio::PlaySceneBGM(const std::string& sceneKey, bool loop) {
    // キーと曲の対応は SetBgmTablePath で読み、調整用の画面で保存した時に ReloadBGMConfig() で読み直す
    auto& config = BgmConfig();
    const auto& table = config.table;
    const std::string path = table.is_object() ? table.value(sceneKey, std::string { }) : std::string { };
    if (path.empty()) {
        Logger::LogWarning("Audio: 曲の表 " + BgmConfig().path + " に曲がありません: " + sceneKey);
        return;
    }
    // 戻ってきた時などに同じ曲を最初からやり直さない
    if (bgmVoice_ != nullptr && sceneBgmPath_ == path) {
        config.audio = this;
        config.key = sceneKey;
        config.loop = loop;
        return;
    }
    if (streamSceneBgm_) {
        PlayStreamBGM(path, loop);
    } else {
        PlayBGM(GetCachedSound(path), loop);
    }
    if (!bgmVoice_) {
        return;
    }
    sceneBgmPath_ = path;
    config.audio = this;
    config.key = sceneKey;
    config.loop = loop;
}

/** @brief キーから曲を引く表の JSON ファイルを設定して読み込む */
void Audio::SetBgmTablePath(const std::string& path) {
    auto& config = BgmConfig();
    config.path = path;
    config.table = path.empty() ? nlohmann::json::object() : JsonHelper::Load(path);
}

/** @brief BGM の割り当てを読み直し、再生中のシーンにも反映する */
void Audio::ReloadBGMConfig() {
    auto& config = BgmConfig();
    config.table = config.path.empty() ? nlohmann::json::object() : JsonHelper::Load(config.path);
    if (config.audio) {
        const std::string key = config.key;
        const bool loop = config.loop;
        config.audio->PlaySceneBGM(key, loop);
    }
}

/** @brief pakから読んだ音声ファイルのバイト列を、Media Foundationで読めるストリームにして開く */
static HRESULT CreateSourceReaderFromMemory(const std::vector<uint8_t>& bytes, const std::wstring& fileName,
    ComPtr<IMFSourceReader>& outReader) {
    ComPtr<IStream> memoryStream;
    memoryStream.Attach(SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size())));
    if (!memoryStream) {
        return E_OUTOFMEMORY;
    }
    ComPtr<IMFByteStream> byteStream;
    HRESULT hr = MFCreateMFByteStreamOnStream(memoryStream.Get(), &byteStream);
    if (FAILED(hr)) {
        return hr;
    }
    // ファイル名が無いと形式を判別できないことがあるため、元のファイル名を手がかりとして渡す
    ComPtr<IMFAttributes> streamAttributes;
    if (SUCCEEDED(byteStream.As(&streamAttributes))) {
        streamAttributes->SetString(MF_BYTESTREAM_ORIGIN_NAME, fileName.c_str());
    }
    return MFCreateSourceReaderFromByteStream(byteStream.Get(), nullptr, &outReader);
}

/** @brief ディスクまたはパックから音声を開き、共通の PCM 形式を選ぶ */
HRESULT Audio::OpenReader(const std::string& path, ComPtr<IMFSourceReader>& reader, WAVEFORMATEX& format) {
    const std::wstring name = StringUtility::ConvertString(path);
    std::vector<uint8_t> packed;
    HRESULT hr = AssetPack::GetInstance()->ReadRawIfMissing(path, packed)
        ? CreateSourceReaderFromMemory(packed, name, reader)
        : MFCreateSourceReaderFromURL(name.c_str(), nullptr, &reader);
    ComPtr<IMFMediaType> request;
    if (SUCCEEDED(hr)) {
        hr = MFCreateMediaType(&request);
    }
    if (SUCCEEDED(hr)) {
        hr = request->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    }
    if (SUCCEEDED(hr)) {
        hr = request->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    }
    if (SUCCEEDED(hr)) {
        hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, request.Get());
    }
    ComPtr<IMFMediaType> actual;
    if (SUCCEEDED(hr)) {
        hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
    }
    WAVEFORMATEX* decoded = nullptr;
    UINT32 size = 0;
    if (SUCCEEDED(hr)) {
        hr = MFCreateWaveFormatExFromMFMediaType(actual.Get(), &decoded, &size);
    }
    if (SUCCEEDED(hr)) {
        // 交渉済みの整数 PCM を固定長の波形形式として渡す。
        format = *decoded;
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.cbSize = 0;
    }
    CoTaskMemFree(decoded);
    return hr;
}

/** @brief 次のサンプルを読み、連続した PCM バイト列へ追加する */
HRESULT Audio::AppendSample(IMFSourceReader* reader, std::vector<byte>& bytes, bool& ended) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    HRESULT hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
    ended = (flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0;
    // 読み取りの失敗だけを失敗とする（形式が変わったという知らせは、前と同じく無視して読み続ける）
    if (flags & MF_SOURCE_READERF_ERROR) {
        return E_FAIL;
    }
    if (FAILED(hr) || !sample) {
        return hr;
    }
    ComPtr<IMFMediaBuffer> buffer;
    hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        return hr;
    }
    DWORD length = 0;
    hr = buffer->GetCurrentLength(&length);
    if (FAILED(hr) || length == 0) {
        return hr;
    }
    const size_t offset = bytes.size();
    bytes.resize(offset + length);
    BYTE* data = nullptr;
    hr = buffer->Lock(&data, nullptr, nullptr);
    if (FAILED(hr)) {
        bytes.resize(offset);
        return hr;
    }
    std::memcpy(bytes.data() + offset, data, length);
    return buffer->Unlock();
}

/** @brief 音声を最後まで展開して返す */
SoundData Audio::LoadAudio(const std::string& filename) {
    SoundData sound{};
    ComPtr<IMFSourceReader> reader;
    const HRESULT openResult = OpenReader(filename, reader, sound.wfex);
    // 開けない時だけ止める（ファイルが無い・形式が読めない）
    if (FAILED(openResult)) {
        Logger::LogError("音声を読み込めませんでした: " + filename);
        ENGINE_ASSERT(false);
        return {};
    }
    // 最後まで読む。途中で読めなくなった時は、そこまで読めた分を使う
    bool ended = false;
    HRESULT hr = S_OK;
    while (SUCCEEDED(hr) && !ended) {
        hr = AppendSample(reader.Get(), sound.pBuffer, ended);
    }
    if (FAILED(hr)) {
        Logger::LogWarning("音声を途中までしか読めませんでした: " + filename);
    }
    sound.bufferSize = static_cast<unsigned int>(sound.pBuffer.size());
    return sound;
}

/** @brief 前の曲を止めて展開済みの曲を再生する */
void Audio::PlayBGM(const SoundData& soundData, bool loop) {
    // 前の BGM を停止してから新しいものを再生する
    StopBGM();

    bgmVoice_ = CreateSourceVoice(soundData.wfex, AudioBus::kBgm);
    if (!bgmVoice_) {
        return;
    }
    bgmVoice_->SetVolume(bgmCurrentVolume_);
    bgmFadeDuration_ = 0.0f;

    // 音声バッファを積んで再生を始める
    SubmitAndStart(bgmVoice_, soundData, loop);
}

// BGM を停止して SourceVoice を解放する
/** @brief 曲を止めて音声と読み込み先を解放する */
void Audio::StopBGM() {
    if (BgmConfig().audio == this) {
        BgmConfig().audio = nullptr;
    }
    sceneBgmPath_.clear();
    if (bgmVoice_) {
        bgmVoice_->Stop();
        bgmVoice_->DestroyVoice();
        bgmVoice_ = nullptr;
    }
    stream_.reset();
}

// BGM の音量を変更する（0.0f = 無音、1.0f = 標準、それ以上 = 増幅）
/** @brief 曲のフェード音量を設定する */
void Audio::SetBGMVolume(float volume) {
    bgmCurrentVolume_ = std::clamp(volume, 0.0f, 1.0f);
    bgmTargetVolume_ = bgmCurrentVolume_;
    bgmFadeDuration_ = 0.0f;
    if (bgmVoice_) {
        bgmVoice_->SetVolume(bgmCurrentVolume_);
    }
}

/** @brief 全SEへ適用するカテゴリ音量を設定する */
void Audio::SetSEVolume(float volume) {
    seVolume_ = std::clamp(volume, 0.0f, 1.0f);
}

// BGM の再生速度を変更する（1.0f = 等速、0.5f = 半速、2.0f = 倍速）
/** @brief 曲の再生速度を変更する */
void Audio::SetBGMSpeed(float speed) {
    if (bgmVoice_) {
        bgmVoice_->SetFrequencyRatio(speed);
    }
}

// BGM フェード

/** @brief BGM の音量を現在値から targetVolume へ duration 秒かけて変えるフェードを開始する */
void Audio::FadeVolumeTo(float targetVolume, float duration) {
    if (!bgmVoice_) {
        return;
    }
    bgmFadeStartVolume_ = bgmCurrentVolume_;
    bgmTargetVolume_ = std::clamp(targetVolume, 0.0f, 1.0f);
    bgmFadeDuration_ = (duration > 0.0f) ? duration : 0.0f;
    bgmFadeTimer_ = 0.0f;
}

/** @brief フェードアニメーションを進めるdt = 経過秒数（1/60 など） */
void Audio::Update(float dt) {
    FillStream();
    ReleaseVoices(seVoices_, false);
    ReleaseVoices(spokenVoices_, false);
    if (!bgmVoice_ || bgmFadeDuration_ <= 0.0f) {
        return;
    }

    bgmFadeTimer_ += dt;
    float t = std::clamp(bgmFadeTimer_ / bgmFadeDuration_, 0.0f, 1.0f);
    bgmCurrentVolume_ = bgmFadeStartVolume_ + (bgmTargetVolume_ - bgmFadeStartVolume_) * t;
    bgmVoice_->SetVolume(bgmCurrentVolume_);

    if (t >= 1.0f) {
        bgmFadeDuration_ = 0.0f;
    }
}

/** @brief マスターと同じ出力形式で三つの出力先を作る */
void Audio::InitializeBuses() {
    XAUDIO2_VOICE_DETAILS details{};
    masteringVoice_->GetVoiceDetails(&details);
    outputChannels_ = details.InputChannels;
    masteringVoice_->GetChannelMask(&channelMask_);
    for (size_t index = 0; index < buses_.size(); ++index) {
        const HRESULT hr = xAudio2_->CreateSubmixVoice(&buses_[index], outputChannels_, details.InputSampleRate);
        ENGINE_ASSERT(SUCCEEDED(hr));
        SetBusVolume(static_cast<AudioBus>(index), busVolumes_[index]);
    }
    SetMasterVolume(masterVolume_);
    const auto config = JsonHelper::Load("Resources/Config/audio.json");
    if (config.is_object()) {
        const auto number = [&](const char* key, float fallback) {
            const auto it = config.find(key);
            return it != config.end() && it->is_number() ? it->get<float>() : fallback;
        };
        nearDistance_ = (std::max)(0.0f, number("nearDistance", kDefaultNear));
        farDistance_ = number("farDistance", kDefaultFar);
        if (!std::isfinite(farDistance_) || farDistance_ <= nearDistance_) {
            nearDistance_ = kDefaultNear;
            farDistance_ = kDefaultFar;
        }
        panStrength_ = std::clamp(number("panStrength", 1.0f), 0.0f, 1.0f);
        streamSceneBgm_ = config.contains("streamSceneBgm") && config.at("streamSceneBgm") == true;
    }
}

/** @brief 指定の出力先に接続した再生ボイスを作る */
IXAudio2SourceVoice* Audio::CreateSourceVoice(const WAVEFORMATEX& format, AudioBus bus, IXAudio2VoiceCallback* callback) {
    XAUDIO2_SEND_DESCRIPTOR destination{0, buses_[static_cast<size_t>(bus)]};
    if (!destination.pOutputVoice) {
        return nullptr;
    }
    XAUDIO2_VOICE_SENDS sends{1, &destination};
    IXAudio2SourceVoice* voice = nullptr;
    const HRESULT hr = xAudio2_->CreateSourceVoice(&voice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, callback, &sends);
    ENGINE_ASSERT(SUCCEEDED(hr));
    return voice;
}

/** @brief 出力先の音量を再生中の音にも反映する */
void Audio::SetBusVolume(AudioBus bus, float volume) {
    const size_t index = static_cast<size_t>(bus);
    if (index < buses_.size()) {
        busVolumes_[index] = std::clamp(volume, 0.0f, 1.0f);
        if (buses_[index]) {
            buses_[index]->SetVolume(busVolumes_[index]);
        }
    }
}

/** @brief 指定した出力先の音量を返す */
float Audio::GetBusVolume(AudioBus bus) const {
    const size_t index = static_cast<size_t>(bus);
    return index < buses_.size() ? busVolumes_[index] : 0.0f;
}

/** @brief マスターの音量を全出力に反映する */
void Audio::SetMasterVolume(float volume) {
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (masteringVoice_) {
        masteringVoice_->SetVolume(masterVolume_);
    }
}

/** @brief 描画の視点と同じ左手系の外積から聞き手の右を求める */
void Audio::SetListener(const Vector3& position, const Vector3& forward, const Vector3& up) {
    listenerPosition_ = position;
    listenerRight_ = Normalize(Cross(up, forward));
}

/** @brief 点音源の全入力を合成し、左右の前方スピーカーへ振り分ける */
float Audio::ApplyPosition(IXAudio2SourceVoice* voice, UINT32 channels, const Vector3& position) const {
    const Vector3 offset = Subtract(position, listenerPosition_);
    const float distance = Length(offset);
    // 近距離と無音になる距離を直接調整できるように線形で減衰させる。
    const float gain = std::clamp((farDistance_ - distance) / (farDistance_ - nearDistance_), 0.0f, 1.0f);
    const float pan = std::clamp(Dot(Normalize(offset), listenerRight_) * panStrength_, -1.0f, 1.0f);
    const auto speaker = [&](DWORD bit, UINT32 fallback) -> UINT32 {
        const UINT32 index = std::popcount(channelMask_ & (bit - 1));
        return (channelMask_ & bit) && index < outputChannels_ ? index : fallback;
    };
    const UINT32 left = speaker(SPEAKER_FRONT_LEFT, 0);
    const UINT32 right = speaker(SPEAKER_FRONT_RIGHT, outputChannels_ > 1 ? 1 : 0);
    std::vector<float> matrix(static_cast<size_t>(outputChannels_) * channels);
    constexpr float kHalf = 0.5f;
    // 出力チャンネルごとに全入力の係数を並べる。単一出力では左右を合算する。
    for (UINT32 input = 0; input < channels; ++input) {
        matrix[left * channels + input] += kHalf * (1.0f - pan) / channels;
        matrix[right * channels + input] += kHalf * (1.0f + pan) / channels;
    }
    voice->SetOutputMatrix(buses_[static_cast<size_t>(AudioBus::kSe)], channels, outputChannels_, matrix.data());
    return gain;
}

/** @brief 一度だけ鳴る音の音量と出力行列を再生前に設定する */
void Audio::PlayOneShot(const SoundData& sound, float volume, AudioBus bus, const Vector3* position) {
    auto& voices = bus == AudioBus::kSe ? seVoices_ : spokenVoices_;
    ReleaseVoices(voices, false);
    auto* voice = CreateSourceVoice(sound.wfex, bus);
    if (!voice) {
        return;
    }
    const float category = bus == AudioBus::kSe ? seVolume_ : 1.0f;
    const float distanceGain = position ? ApplyPosition(voice, sound.wfex.nChannels, *position) : 1.0f;
    voice->SetVolume(std::clamp(volume, 0.0f, 1.0f) * category * distanceGain);
    SubmitAndStart(voice, sound, false);
    voices.push_back(voice);
}

/** @brief 効果音を再生する */
void Audio::PlaySE(const SoundData& sound, float volume) {
    PlayOneShot(sound, volume, AudioBus::kSe, nullptr);
}

/** @brief 指定位置の効果音を再生する */
void Audio::PlaySE3D(const SoundData& sound, const Vector3& position, float volume) {
    PlayOneShot(sound, volume, AudioBus::kSe, &position);
}

/** @brief 声を再生する */
void Audio::PlayVoice(const SoundData& sound, float volume) {
    PlayOneShot(sound, volume, AudioBus::kVoice, nullptr);
}

/** @brief 再生が終わった音または指定した出力先の全音声を破棄する */
void Audio::ReleaseVoices(std::vector<IXAudio2SourceVoice*>& voices, bool stopAll) {
    std::erase_if(voices, [stopAll](IXAudio2SourceVoice* voice) {
        XAUDIO2_VOICE_STATE state{};
        voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (!stopAll && state.BuffersQueued != 0) {
            return false;
        }
        voice->DestroyVoice();
        return true;
    });
}

/** @brief すべての効果音を止める */
void Audio::StopAllSE() {
    ReleaseVoices(seVoices_, true);
}

/** @brief すべての声を止める */
void Audio::StopAllVoice() {
    ReleaseVoices(spokenVoices_, true);
}

} // namespace engine

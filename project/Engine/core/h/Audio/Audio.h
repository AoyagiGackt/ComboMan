/**
 * @file Audio.h
 * @brief XAudio2を使用した音声ファイルの読み込み・再生を管理するファイル
 *
 * 使い方:
 *   audio->PlayBGM(bgmData);          // BGM再生（ループ）
 *   audio->PlaySE(seData);             // SE再生（複数同時可）
 *   audio->StopBGM();                  // BGM停止
 *   audio->SetBGMVolume(0.5f);         // BGM音量変更
 */
#pragma once
#include "Vector3.h"
#include <array>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <wrl/client.h>
#include <xaudio2.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfuuid.lib")
namespace engine {

/**
 * @brief 再生する音声データを保持する構造体
 */
struct SoundData {
    WAVEFORMATEX wfex; ///< 波形フォーマット
    std::vector<byte> pBuffer; ///< 音声データ本体
    unsigned int bufferSize; ///< バッファのサイズ（バイト）
};

/** @brief 音量をまとめて調節する出力先 */
enum class AudioBus { kBgm, kSe, kVoice, kCount };

/**
 * @brief オーディオ再生を管理するクラス
 *
 * BGM チャンネル（1本・ループ）と SE チャンネル（複数同時再生）を独立管理します
 */
class Audio {
public:
    /**
     * @brief オーディオエンジンの初期化
     */
    void Initialize();

    /**
     * @brief オーディオエンジンの終了処理
     */
    void Finalize();

    /**
     * @brief 音声ファイルをロードする
     * @param filename ファイルパス（WAV / MP3 等）
     * @return ロード済みの SoundData
     */
    SoundData LoadAudio(const std::string& filename);
    /**
     * @brief メニューの選択移動音と決定音に使う音声ファイルを設定し、先に読み込んでおく
     * @note どの音を使うかはゲームが決める（設定しない間は PlayMenuChoice / PlayMenuSelect は何も鳴らさない）
     */
    void SetMenuSounds(const std::string& choicePath, const std::string& selectPath);
    /** @brief メニューの選択移動音を再生する */
    void PlayMenuChoice();
    /** @brief メニューの決定音を再生する */
    void PlayMenuSelect();
    /**
     * @brief キーから曲を引く表の JSON ファイルを設定して読み込む（{ "キー": "曲のパス", ... }）
     * @note どのファイルを使い、どんなキーを書くかはゲームが決める。設定しない間は表が空になる
     */
    static void SetBgmTablePath(const std::string& path);
    /**
     * @brief 表でキーに割り当てた曲を再生する（同じ曲が鳴っていれば続きから流す）
     * @param sceneKey 表のキー
     * @param loop false なら最後まで流したら止める（ゲームオーバーやクリアの曲）
     */
    void PlaySceneBGM(const std::string& sceneKey, bool loop = true);
    /**
     * @brief 表でキーに割り当てた曲を、鳴らさずに読み込んでおく
     * @param sceneKey 表のキー
     * @note 曲は初めて鳴らす時に読み込んで変換するので、ゲームの途中で初めて鳴らす曲（クリアややられた時）は
     *       シーンの読み込みの時に読んでおかないと、鳴らした瞬間に画面が止まる
     */
    void PreloadSceneBGM(const std::string& sceneKey);

    /** @brief BGM の割り当てを読み直し、再生中のシーンにも反映する */
    static void ReloadBGMConfig();

    // BGM（1チャンネル、ループ再生）

    /**
     * @brief BGM を再生する（前の BGM は自動停止）
     * @param soundData 再生する音声データ
     * @param loop true でループ再生（デフォルト）
     */
    void PlayBGM(const SoundData& soundData, bool loop = true);

    /**
     * @brief BGM を停止する
     */
    void StopBGM();

    /**
     * @brief BGM の音量を設定する
     * @param volume 0.0f（無音）〜 1.0f（最大）
     */
    void SetBGMVolume(float volume);

    /** @brief 全SEへ適用するカテゴリ音量を設定する */
    void SetSEVolume(float volume);

    /** @brief 現在のBGMカテゴリ音量を返す */
    float GetBGMVolume() const {
        return bgmCurrentVolume_;
    }

    /** @brief 現在のSEカテゴリ音量を返す */
    float GetSEVolume() const {
        return seVolume_;
    }

    /**
     * @brief BGM の再生速度（ピッチ）を変更する
     * @param speed 1.0f で通常速度
     */
    void SetBGMSpeed(float speed);

    // SE（複数同時再生、自動クリーンアップ）

    /**
     * @brief SE を再生する（複数同時再生可）
     * @param soundData 再生する音声データ
     * @param volume 音量（0.0f〜1.0f、デフォルト 1.0f）
     */
    void PlaySE(const SoundData& soundData, float volume = 1.0f);

    /**
     * @brief 再生中の SE をすべて停止する
     */
    void StopAllSE();

    /** @brief 出力先全体の音量を設定する */
    void SetBusVolume(AudioBus bus, float volume);
    /** @brief 出力先全体の音量を取得する */
    float GetBusVolume(AudioBus bus) const;
    /** @brief マスター音量を設定する */
    void SetMasterVolume(float volume);
    /** @brief マスター音量を取得する */
    float GetMasterVolume() const {
        return masterVolume_;
    }
    /** @brief 声を声用の出力先で再生する */
    void PlayVoice(const SoundData& soundData, float volume = 1.0f);
    /** @brief 再生中の声をすべて止める */
    void StopAllVoice();
    /** @brief 聞き手の位置と向きを設定する */
    void SetListener(const Vector3& position, const Vector3& forward, const Vector3& up);
    /** @brief 指定した位置の効果音を再生する */
    void PlaySE3D(const SoundData& soundData, const Vector3& position, float volume = 1.0f);
    /** @brief 曲を少しずつ読み込みながら再生する */
    void PlayStreamBGM(const std::string& filePath, bool loop = true);

    // BGM フェード

    /**
     * @brief BGM の音量を duration 秒かけて targetVolume へフェードする
     * @param targetVolume 目標音量（0.0f〜1.0f）
     * @param duration     フェードにかかる秒数
     */
    void FadeVolumeTo(float targetVolume, float duration);

    // 毎フレーム更新（Framework::Update から呼ぶ）

    /** @brief フェードアニメーションを進めるdt = 経過秒数（1/60 など）*/
    void Update(float dt);

    // 後方互換 API（PlayWave / StopWave）

    /** @brief BGM として再生する（PlayBGM の別名） */
    void PlayWave(const SoundData& soundData) {
        PlayBGM(soundData, true);
    }

    /** @brief BGM を停止する（StopBGM の別名） */
    void StopWave() {
        StopBGM();
    }

    /** @brief BGM の速度を変更する（SetBGMSpeed の別名） */
    void SetPlaybackSpeed(float speed) {
        SetBGMSpeed(speed);
    }

private:
    /** @brief 読み込み済みの音を取得する */
    const SoundData& GetCachedSound(const std::string& filename);
    // シーンを離れた後も再生中のSEが参照できるよう、音声システム側で保持する。
    std::unordered_map<std::string, SoundData> soundCache_;
    /** @brief SourceVoice を生成するヘルパー */
    IXAudio2SourceVoice* CreateSourceVoice(const WAVEFORMATEX& format, AudioBus bus, IXAudio2VoiceCallback* callback = nullptr);
    /** @brief ファイルまたはパックから音声の読み込みを始める */
    static HRESULT OpenReader(const std::string& path, Microsoft::WRL::ComPtr<IMFSourceReader>& reader, WAVEFORMATEX& format);
    /** @brief 次の音声サンプルを末尾に追加し、終端を通知する */
    static HRESULT AppendSample(IMFSourceReader* reader, std::vector<byte>& bytes, bool& ended);
    /** @brief 空いたバッファに曲の続きを補充する */
    void FillStream();
    /** @brief ストリームの読み込み位置と再生待ちの領域 */
    struct StreamState;
    std::shared_ptr<StreamState> stream_;
    /** @brief サブミックスと距離設定を初期化する */
    void InitializeBuses();
    /** @brief 一度だけ鳴る音を指定の出力先で再生する */
    void PlayOneShot(const SoundData& sound, float volume, AudioBus bus, const Vector3* position);
    /** @brief 終了した音または指定されたすべての音を解放する */
    static void ReleaseVoices(std::vector<IXAudio2SourceVoice*>& voices, bool stopAll);
    /** @brief 音源の位置から左右の出力行列と距離減衰を求める */
    float ApplyPosition(IXAudio2SourceVoice* voice, UINT32 channels, const Vector3& position) const;

    static constexpr size_t kBusCount = static_cast<size_t>(AudioBus::kCount); ///< 出力先の数
    static constexpr float kDefaultNear = 1.0f; ///< audio.json が無い時の、減衰が始まる距離
    static constexpr float kDefaultFar = 30.0f; ///< audio.json が無い時の、無音になる距離
    std::array<IXAudio2SubmixVoice*, kBusCount> buses_{}; ///< BGM・SE・声の出力先（全部マスターへ出る）
    std::array<float, kBusCount> busVolumes_{1.0f, 1.0f, 1.0f}; ///< 出力先ごとの音量
    float masterVolume_ = 1.0f; ///< 全体の音量
    UINT32 outputChannels_ = 0; ///< マスターの出力チャンネル数
    DWORD channelMask_ = 0; ///< マスターのスピーカーの並び
    Vector3 listenerPosition_{}; ///< 聞き手の位置
    Vector3 listenerRight_{1.0f, 0.0f, 0.0f}; ///< 聞き手の右向き（左右の振り分けに使う）
    float nearDistance_ = kDefaultNear; ///< この距離までは減衰しない
    float farDistance_ = kDefaultFar; ///< この距離で無音になる
    float panStrength_ = 1.0f; ///< 左右への振り分けの強さ（0 で振らない）
    bool streamSceneBgm_ = false; ///< シーンの曲を少しずつ読み込んで鳴らすか
    std::vector<IXAudio2SourceVoice*> spokenVoices_; ///< 再生中の声

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masteringVoice_ = nullptr;

    /** @brief BGM 専用チャンネル */
    IXAudio2SourceVoice* bgmVoice_ = nullptr;
    /** @brief PlaySceneBGM で流している曲のパス（それ以外の再生中・停止中は空） */
    std::string sceneBgmPath_;
    std::string menuChoicePath_; ///< メニューの選択移動音（空なら鳴らさない）
    std::string menuSelectPath_; ///< メニューの決定音（空なら鳴らさない）

    /** @brief SE チャンネル（複数同時再生） */
    std::vector<IXAudio2SourceVoice*> seVoices_;

    // BGM フェード状態
    float bgmCurrentVolume_ = 1.0f;
    float bgmTargetVolume_ = 1.0f;
    float bgmFadeStartVolume_ = 1.0f;
    float bgmFadeDuration_ = 0.0f;
    float bgmFadeTimer_ = 0.0f;
    float seVolume_ = 1.0f; ///< 個別SE音量へ乗算するカテゴリ音量
};

} // namespace engine

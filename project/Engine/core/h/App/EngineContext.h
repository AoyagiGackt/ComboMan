/**
 * @file EngineContext.h
 * @brief エンジンの主な仕組み（デバイス・入力・音・素材の管理など）を一か所から取り出す入口のファイル
 */
#pragma once
namespace engine {
class Audio;
class DirectXCommon;
class Input;
class TimeManager;
class AssetPack;
namespace graphics {
class LightManager;
class ModelManager;
class ParticleManager;
class SrvManager;
class TextureManager;
}

/**
 * @brief エンジンの主な仕組みへのポインタの組
 * @note どれも持ち主ではない（作って消すのは Framework）。テストでは一部を差し替えた組を Set できる
 */
struct EngineServices {
    DirectXCommon* dxCommon = nullptr; ///< デバイス・コマンド・画面
    Input* input = nullptr; ///< キーボード・マウス・パッド
    Audio* audio = nullptr; ///< 曲・効果音・声
    TimeManager* time = nullptr; ///< 経過時間と時間の速さ
    AssetPack* assetPack = nullptr; ///< 配布用にまとめた素材
    graphics::SrvManager* srvManager = nullptr; ///< シェーダーから読む資源のデスクリプタ
    graphics::TextureManager* textures = nullptr; ///< テクスチャの読み込みと共有
    graphics::ModelManager* models = nullptr; ///< モデルの読み込みと共有
    graphics::ParticleManager* particles = nullptr; ///< パーティクル
    graphics::LightManager* lights = nullptr; ///< 光源
};

/**
 * @brief エンジンの主な仕組みを取り出す入口
 * @note それぞれの GetInstance を直接呼ぶ代わりにここから取ると、テストや別のゲームで
 *       中身を差し替えられる（呼ぶ側を書き換えずに済む）。今ある GetInstance の呼び出しは、
 *       触った所から少しずつここへ移す
 */
class EngineContext {
public:
    /** @brief 今の組を返す（Framework の初期化が済むまでは全部 nullptr） */
    static const EngineServices& Get() {
        return sServices;
    }

    /** @brief 組を入れ替える（Framework が初期化の最後に呼ぶ。テストでは差し替えに使う） */
    static void Set(const EngineServices& services) {
        sServices = services;
    }

    /**
     * @brief スコープの間だけ組を差し替え、抜ける時に元へ戻す（テスト用）
     */
    class ScopedOverride {
    public:
        /** @brief 今の組を覚えて、渡された組に差し替える */
        explicit ScopedOverride(const EngineServices& services)
            : previous_(sServices) {
            sServices = services;
        }
        /** @brief 覚えておいた組に戻す */
        ~ScopedOverride() {
            sServices = previous_;
        }
        ScopedOverride(const ScopedOverride&) = delete;
        ScopedOverride& operator=(const ScopedOverride&) = delete;

    private:
        EngineServices previous_; ///< 差し替える前の組
    };

private:
    /** @brief 今の組 */
    static inline EngineServices sServices {};
};

/** @brief 今の組を短く取り出す（EngineContext::Get() と同じ） */
inline const EngineServices& Services() {
    return EngineContext::Get();
}

} // namespace engine

/**
 * @file StaticModelOverride.h
 * @brief プレイヤーの見た目を、ボーンの無い別のモデル1つに差し替える部品を定義するファイル
 */
#pragma once
#include "Object3d.h"
#include "Vector3.h"
#include <memory>
#include <string>

namespace engine::graphics {
class Model;
class ModelCommon;
}

namespace engine::game {

/**
 * @brief 見た目の差し替えモデル
 * @note 差し替えている間は、本体のリグと武器の代わりにこのモデルだけを描く。
 *       モデルの高さはまちまちなので、当たり判定の高さ（1）に合わせて大きさをそろえ、最下点を足元に合わせる。
 */
class StaticModelOverride {
public:
    /**
     * @brief 差し替えるモデルを読み込む（モデルのパスが空なら差し替えをやめる）
     * @param modelCommon モデル共通処理
     * @param modelPath モデルのパス
     * @param texturePath テクスチャのパス（空なら白）
     */
    void Set(engine::graphics::ModelCommon* modelCommon, const std::string& modelPath, const std::string& texturePath);
    /** @brief 差し替えをやめる */
    void Clear();
    /** @brief 差し替えているか */
    bool IsActive() const {
        return object_ != nullptr;
    }
    /**
     * @brief 差し替えたモデルを描く
     * @param footPosition 足元の位置（当たり判定の下端）
     * @param yaw 縦軸回りの向き
     */
    void Draw(const Vector3& footPosition, float yaw);
    /** @brief 差し替えに使っているモデルのパス */
    const std::string& GetModelPath() const {
        return modelPath_;
    }
    /** @brief 差し替えに使っているテクスチャのパス */
    const std::string& GetTexturePath() const {
        return texturePath_;
    }

private:
    engine::graphics::Model* model_ = nullptr; ///< 実体は ModelManager が所有・共有する
    std::unique_ptr<engine::graphics::Object3d> object_; ///< 描く実体（差し替えていなければ nullptr）
    float footOffset_ = 0.0f; ///< モデル原点から最下点までの距離（大きさをそろえた後。中心が原点のモデルでも足元を合わせる）
    std::string modelPath_; ///< 差し替えに使っているモデルのパス
    std::string texturePath_; ///< 差し替えに使っているテクスチャのパス
};

} // namespace engine::game

/**
 * @file StaticModelOverride.cpp
 * @brief プレイヤーの見た目の差し替えモデルを実装するファイル
 */
#include "StaticModelOverride.h"
#include "EngineContext.h"
#include "ModelManager.h"
#include "SceneVisualTuning.h"
#include <algorithm>
#include <limits>

namespace engine::game {

using namespace engine::graphics;

namespace {
constexpr float kMinModelHeight = 0.001f; // これより低いモデルは高さが取れないとみなし、既定の大きさにする
constexpr const char* kDefaultTexturePath = "Resources/white.png"; // テクスチャを指定しない時の白
} // namespace

/** @brief 差し替えるモデルを読み込み、当たり判定の高さに合わせて大きさと足元をそろえる */
void StaticModelOverride::Set(ModelCommon* modelCommon, const std::string& modelPath, const std::string& texturePath) {
    if (modelPath.empty() || modelCommon == nullptr) {
        Clear();
        return;
    }
    modelPath_ = modelPath;
    texturePath_ = texturePath.empty() ? kDefaultTexturePath : texturePath;
    model_ = engine::Services().models->GetOrLoad(modelCommon, modelPath, texturePath_);
    object_ = std::make_unique<Object3d>();
    object_->Initialize(modelCommon);
    object_->SetModel(model_);
    object_->SetEnableLighting(true);

    // 差し替えモデルは身長がまちまちなので、当たり判定（1x1x1 の箱）の高さに合わせて大きさをそろえる
    // 原点が中心にあるモデル（足元が0でない）でも、最下点を箱の下端に合わせられるよう足元のずれも求める
    float minY = (std::numeric_limits<float>::max)();
    float maxY = (std::numeric_limits<float>::lowest)();
    for (const auto& vertex : model_->GetVertices()) {
        minY = (std::min)(minY, vertex.position.y);
        maxY = (std::max)(maxY, vertex.position.y);
    }
    const float height = maxY - minY;
    const float scale = (height > kMinModelHeight) ? (1.0f / height) : SceneVisualTuning::Player().fallbackOverrideScale;
    object_->SetScale({ scale, scale, scale });
    footOffset_ = -minY * scale;
}

/** @brief 差し替えをやめる */
void StaticModelOverride::Clear() {
    object_.reset();
    model_ = nullptr;
    footOffset_ = 0.0f;
    modelPath_.clear();
    texturePath_.clear();
}

/** @brief 足元の位置に最下点が来るように置いて描く */
void StaticModelOverride::Draw(const Vector3& footPosition, float yaw) {
    if (object_ == nullptr) {
        return;
    }
    object_->SetPosition({ footPosition.x, footPosition.y + footOffset_, footPosition.z });
    object_->SetRotation({ 0.0f, yaw, 0.0f });
    object_->Update();
    object_->Draw();
}

} // namespace engine::game

/**
 * @file BorderBlockBuilder.h
 * @brief 壁沿いの境界ブロック配置をJSON設定から組み立てるビルダー（TrainingScene/BattleTestSceneで共用）を定義するファイル
 */
#pragma once
#include "JsonFieldTable.h"
#include "EngineContext.h"
#include "JsonHelper.h"
#include "Model.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include <memory>
#include <string>
#include <vector>
namespace engine::game {

/**
 * @brief 標準の壁沿い境界ブロックのレイアウト定義
 * @note 各値の既定値は、以前このファイルにハードコードされていた形状と同じ
 * （ブロック1個=1ワールド単位、Z=0の2.5D平面に並べる）
 */
struct BorderBlockLayout {
    std::string palette; ///< 地形の色見本の画像の場所
    float floorY = -0.6f; ///< 床のY座標
    float ceilingY = 13.0f; ///< 天井のY座標
    float leftWallX = 2.0f; ///< 左壁のX座標
    float rightWallX = 28.0f; ///< 右壁のX座標
    int wallMinIndex = 0; ///< 各辺のブロック開始インデックス
    int floorCeilingMaxIndex = 28; ///< 床/天井の左右方向のブロック数-1
    int wallMaxIndex = 12; ///< 左右壁の縦方向のブロック数-1
    float blockZ = 0.0f; ///< ブロックを並べる奥行き
};

/**
 * @brief JSONファイルから境界ブロックのレイアウトを読み込む
 * @param jsonPath レイアウトJSONのパス
 * @return 読み込んだレイアウト。ファイルが無い・フィールドが欠けている場合はBorderBlockLayoutの既定値を使う
 */
inline BorderBlockLayout LoadBorderBlockLayout(const std::string& jsonPath) {
    BorderBlockLayout layout;
    nlohmann::json j = engine::JsonHelper::Load(jsonPath);
    if (!j.is_object()) {
        return layout;
    }
    constexpr const char* kPaletteKey = "palette";
    static constexpr JsonField<BorderBlockLayout> kFields[] = {
        { nullptr, kPaletteKey, &BorderBlockLayout::palette },
        { nullptr, "floorY", &BorderBlockLayout::floorY },
        { nullptr, "ceilingY", &BorderBlockLayout::ceilingY },
        { nullptr, "leftWallX", &BorderBlockLayout::leftWallX },
        { nullptr, "rightWallX", &BorderBlockLayout::rightWallX },
        { nullptr, "wallMinIndex", &BorderBlockLayout::wallMinIndex },
        { nullptr, "floorCeilingMaxIndex", &BorderBlockLayout::floorCeilingMaxIndex },
        { nullptr, "wallMaxIndex", &BorderBlockLayout::wallMaxIndex },
        { nullptr, "blockZ", &BorderBlockLayout::blockZ },
    };
    ReadJsonFields(j, kFields, layout);
    return layout;
}

/**
 * @brief 標準の壁沿いに境界ブロックを並べて生成する（TrainingScene/BattleTestSceneで共用）
 * @param jsonPath レイアウト設定のJSONパス（省略時はResources/Config/border_layout.json）。
 *        ファイルが無ければ既存互換の既定形状で生成する
 */
inline void BuildBorderBlocks(engine::graphics::ModelCommon* modelCommon, engine::graphics::Model* modelBlock,
    std::vector<std::unique_ptr<engine::graphics::Object3d>>& borderBlocks,
    const std::string& jsonPath = "Resources/Config/border_layout.json") {
    const BorderBlockLayout layout = LoadBorderBlockLayout(jsonPath);

    constexpr const char* kGroundModelPath = "Resources/block/terrain/terrain_ground.obj";
    constexpr const char* kCeilingModelPath = "Resources/block/terrain/terrain_ceiling.obj";
    constexpr const char* kWallModelPath = "Resources/block/terrain/terrain_wall.obj";
    auto* groundModel = modelBlock;
    auto* ceilingModel = modelBlock;
    auto* wallModel = modelBlock;
    if (!layout.palette.empty()) {
        auto* manager = engine::Services().models;
        groundModel = manager->GetOrLoad(modelCommon, kGroundModelPath, layout.palette);
        ceilingModel = manager->GetOrLoad(modelCommon, kCeilingModelPath, layout.palette);
        wallModel = manager->GetOrLoad(modelCommon, kWallModelPath, layout.palette);
    }

    auto addBlock = [&](engine::graphics::Model* model, float x, float y, float z) {
        auto b = std::make_unique<engine::graphics::Object3d>();
        b->Initialize(modelCommon);
        b->SetModel(model);
        b->SetEnableLighting(false);
        b->SetPosition({ x, y, z });
        b->Update();
        borderBlocks.push_back(std::move(b));
    };
    for (int x = layout.wallMinIndex; x <= layout.floorCeilingMaxIndex; ++x) {
        addBlock(groundModel, static_cast<float>(x), layout.floorY, layout.blockZ);
    }
    for (int x = layout.wallMinIndex; x <= layout.floorCeilingMaxIndex; ++x) {
        addBlock(ceilingModel, static_cast<float>(x), layout.ceilingY, layout.blockZ);
    }
    for (int y = layout.wallMinIndex; y <= layout.wallMaxIndex; ++y) {
        addBlock(wallModel, layout.leftWallX, static_cast<float>(y), layout.blockZ);
    }
    for (int y = layout.wallMinIndex; y <= layout.wallMaxIndex; ++y) {
        addBlock(wallModel, layout.rightWallX, static_cast<float>(y), layout.blockZ);
    }
}

} // namespace engine::game

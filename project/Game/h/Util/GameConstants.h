/**
 * @file GameConstants.h
 * @brief ゲーム全体で使う定数をまとめたファイル
 * @note フレームレート・画面サイズ・数学定数はエンジンの EngineConstants.h にあり、ここからも同じ名前で使える
 */
#pragma once
#include "EngineConstants.h"

namespace engine {
namespace gameConstants {

    // エンジンの定数（ゲーム側では gameConstants:: のまま使えるようにする）
    using engineConstants::kTargetFps;
    using engineConstants::kFrameDeltaTime;
    using engineConstants::kScreenWidth;
    using engineConstants::kScreenHeight;
    using engineConstants::kScreenCenterX;
    using engineConstants::kScreenCenterY;
    using engineConstants::kPi;
    using engineConstants::kTwoPi;
    using engineConstants::kHalfPi;
    using engineConstants::kDegToRad;

    // カメラ投影半幅・半高（fovY=0.45, dist=24 のカメラで Z=0 面に映る可視半幅・半高）
    inline constexpr float kCameraHalfW = 9.800f;
    inline constexpr float kCameraHalfH = 5.510f;
    // カメラのZ距離（上のkCameraHalfW/Hの前提そのもの。全シーンのSetTranslate/UpdateCameraFollowで共通）
    inline constexpr float kCameraDistanceZ = -24.0f;
    // カメラ追従時、プレイヤーのYより少し上を映すオフセット（頭上の間合いを見せるため）
    inline constexpr float kCameraFollowOffsetY = 3.0f;

}

/** @brief TimeManager の時間の通り道の番号（その物の更新で IsChannelStopped を見る） */
namespace timeChannel {
    inline constexpr int kEnemies = 0; ///< 敵とボスの体・ボスの技の進行（プレイヤーより長く止めて、斬った後の間を作る）
}
} // namespace engine

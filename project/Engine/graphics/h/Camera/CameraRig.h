/**
 * @file CameraRig.h
 * @brief 見たい所と進む向きから、カメラの置き場所と向きを決める部品（横から・後ろから）
 * @note ゲームに依存しない。どの部品を使うかと距離などの値は、呼び出し側が設定ファイルから決める
 */
#pragma once
#include "Vector3.h"
#include <cmath>
#include <memory>
#include <string>

namespace engine::graphics {

/** @brief カメラの目の位置と、見る点 */
struct CameraShotPose {
    Vector3 eye; ///< カメラの位置
    Vector3 target; ///< 見る点
};

/** @brief カメラの部品に渡す距離などの値 */
struct CameraRigSettings {
    float distance = 24.0f; ///< 見たい所からカメラまでの距離
    float height = 3.0f; ///< カメラを見たい所より上げる量
    float targetHeight = 3.0f; ///< 見る点を見たい所より上げる量
    float lookAhead = 0.0f; ///< 見る点を進む向きへ先に出す量
};

/**
 * @brief 目の位置と見る点から、カメラの回転（x=見下ろし、y=向き）を求める
 * @note x が正で下を向き、y は +Z を 0 として +X 側へ回る向き
 */
inline Vector3 LookAtRotation(const Vector3& eye, const Vector3& target) {
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float flat = std::sqrt(dx * dx + dz * dz);
    return { std::atan2(-dy, flat), std::atan2(dx, dz), 0.0f };
}

/**
 * @brief カメラの置き方の共通の形
 * @note 種類ごとに派生クラスを作り、Evaluate だけを差し替える
 */
class CameraRig {
public:
    virtual ~CameraRig() = default;
    /**
     * @brief カメラの置き場所を決める
     * @param focus   見たい所（プレイヤーや道の上の今の位置）
     * @param forward 進む向き（長さ1、上下は無視する）
     * @param settings 距離などの値
     */
    virtual CameraShotPose Evaluate(const Vector3& focus, const Vector3& forward, const CameraRigSettings& settings) const = 0;

protected:
    /** @brief 上下を除いた進む向き（長さ1。ほぼ真上・真下なら +X） */
    static Vector3 FlatForward(const Vector3& forward) {
        const Vector3 flat = { forward.x, 0.0f, forward.z };
        return Length(flat) > kMinFlatLength ? Normalize(flat) : Vector3 { 1.0f, 0.0f, 0.0f };
    }
    /** @brief 真上の向き */
    static constexpr Vector3 kUp = { 0.0f, 1.0f, 0.0f };

private:
    static constexpr float kMinFlatLength = 1.0e-4f; ///< これより短い向きは使わない
};

/** @brief 進む向きの横から映す（横スクロールの見え方。進む向きが +X なら手前の -Z から映す） */
class SideCameraRig : public CameraRig {
public:
    /** @brief 進む向きの左手側（+X に進むなら -Z）へ離して置く */
    CameraShotPose Evaluate(const Vector3& focus, const Vector3& forward, const CameraRigSettings& settings) const override {
        const Vector3 ahead = FlatForward(forward);
        const Vector3 side = Cross(kUp, ahead);
        const Vector3 lift = kUp * settings.height;
        return { focus + side * settings.distance + lift, focus + ahead * settings.lookAhead + kUp * settings.targetHeight };
    }
};

/** @brief 進む向きの後ろから映す（奥へ進む見え方） */
class BehindCameraRig : public CameraRig {
public:
    /** @brief 進む向きの反対側へ離して置き、少し先を見る */
    CameraShotPose Evaluate(const Vector3& focus, const Vector3& forward, const CameraRigSettings& settings) const override {
        const Vector3 ahead = FlatForward(forward);
        return { focus + ahead * -settings.distance + kUp * settings.height,
            focus + ahead * settings.lookAhead + kUp * settings.targetHeight };
    }
};

/**
 * @brief 名前からカメラの部品を作る
 * @param type "side"（横から）か "behind"（後ろから）。知らない名前なら横から
 */
inline std::unique_ptr<CameraRig> CreateCameraRig(const std::string& type) {
    if (type == "behind") {
        return std::make_unique<BehindCameraRig>();
    }
    return std::make_unique<SideCameraRig>();
}

} // namespace engine::graphics

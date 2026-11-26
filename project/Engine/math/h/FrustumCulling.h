/**
 * @file FrustumCulling.h
 * @brief 箱がカメラの見える範囲（視錐台）の外にあるかを、ワールド・ビュー・プロジェクション行列で調べる関数を定義するファイル
 */
#pragma once
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace engine {

/** @brief 箱の角の数 */
inline constexpr size_t kBoxCornerCount = 8;

/**
 * @brief 点を行ベクトルとして行列に掛け、クリップ空間の座標にする（シェーダーの mul(position, WVP) と同じ）
 * @param point ローカル座標（w は 1 とみなす）
 * @param matrix ワールド・ビュー・プロジェクション行列
 */
inline Vector4 TransformToClip(const Vector3& point, const Matrix4x4& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0],
        point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1],
        point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2],
        point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3],
    };
}

/**
 * @brief ローカル空間の箱が、画面に映る範囲の外に完全に出ているかを調べる
 * @param worldViewProjection 描画に使うワールド・ビュー・プロジェクション行列
 * @param boxMin 箱の最小の角（ローカル空間）
 * @param boxMax 箱の最大の角（ローカル空間）
 * @return 8つの角がどれも同じ面の外側にあれば true（その時だけ確実に映らない）
 * @note 角がすべて違う面の外にある大きな箱は外と判定しない（映るかもしれない物は描く側に倒す）
 */
inline bool IsBoxOutsideClipSpace(const Matrix4x4& worldViewProjection, const Vector3& boxMin, const Vector3& boxMax) {
    // 8つの角をクリップ空間へ移す
    const std::array<Vector3, kBoxCornerCount> corners = { {
        { boxMin.x, boxMin.y, boxMin.z }, { boxMax.x, boxMin.y, boxMin.z },
        { boxMin.x, boxMax.y, boxMin.z }, { boxMax.x, boxMax.y, boxMin.z },
        { boxMin.x, boxMin.y, boxMax.z }, { boxMax.x, boxMin.y, boxMax.z },
        { boxMin.x, boxMax.y, boxMax.z }, { boxMax.x, boxMax.y, boxMax.z },
    } };
    // 6つの面それぞれについて、全部の角がその外側にあるかを数える（左・右・下・上・手前・奥）
    size_t outsideLeft = 0;
    size_t outsideRight = 0;
    size_t outsideBottom = 0;
    size_t outsideTop = 0;
    size_t outsideNear = 0;
    size_t outsideFar = 0;
    for (const Vector3& corner : corners) {
        const Vector4 clip = TransformToClip(corner, worldViewProjection);
        outsideLeft += clip.x < -clip.w ? 1 : 0;
        outsideRight += clip.x > clip.w ? 1 : 0;
        outsideBottom += clip.y < -clip.w ? 1 : 0;
        outsideTop += clip.y > clip.w ? 1 : 0;
        outsideNear += clip.z < 0.0f ? 1 : 0;
        outsideFar += clip.z > clip.w ? 1 : 0;
    }
    return outsideLeft == kBoxCornerCount || outsideRight == kBoxCornerCount
        || outsideBottom == kBoxCornerCount || outsideTop == kBoxCornerCount
        || outsideNear == kBoxCornerCount || outsideFar == kBoxCornerCount;
}

/**
 * @brief 頂点の並びの position をすべて囲む箱を求める（頂点が無ければ原点の点にする）
 * @param vertices position（x・y・z を持つ）を持つ頂点の並び
 * @param boxMin 求めた最小の角
 * @param boxMax 求めた最大の角
 */
template<class Vertex>
inline void ComputeVertexBounds(const std::vector<Vertex>& vertices, Vector3& boxMin, Vector3& boxMax) {
    if (vertices.empty()) {
        boxMin = { 0.0f, 0.0f, 0.0f };
        boxMax = { 0.0f, 0.0f, 0.0f };
        return;
    }
    // 最初の頂点から始めて、各軸の小さい方と大きい方を広げていく
    const auto& first = vertices.front().position;
    boxMin = { first.x, first.y, first.z };
    boxMax = boxMin;
    for (const Vertex& vertex : vertices) {
        boxMin.x = (std::min)(boxMin.x, vertex.position.x);
        boxMin.y = (std::min)(boxMin.y, vertex.position.y);
        boxMin.z = (std::min)(boxMin.z, vertex.position.z);
        boxMax.x = (std::max)(boxMax.x, vertex.position.x);
        boxMax.y = (std::max)(boxMax.y, vertex.position.y);
        boxMax.z = (std::max)(boxMax.z, vertex.position.z);
    }
}

/**
 * @brief 箱を各軸の大きさの一定の割合だけ広げてから、画面に映る範囲の外に完全に出ているかを調べる
 * @param worldViewProjection 描画に使うワールド・ビュー・プロジェクション行列
 * @param boxMin 箱の最小の角（ローカル空間）
 * @param boxMax 箱の最大の角（ローカル空間）
 * @param paddingRatio 各軸の大きさに対する広げる割合（動く頂点や丸め誤差で縁が欠けないようにする）
 */
inline bool IsPaddedBoxOutsideClipSpace(const Matrix4x4& worldViewProjection,
    const Vector3& boxMin, const Vector3& boxMax, float paddingRatio) {
    // 各軸の大きさに割合を掛けた分だけ、両側へ広げる
    const Vector3 padding = {
        (boxMax.x - boxMin.x) * paddingRatio,
        (boxMax.y - boxMin.y) * paddingRatio,
        (boxMax.z - boxMin.z) * paddingRatio,
    };
    const Vector3 paddedMin = { boxMin.x - padding.x, boxMin.y - padding.y, boxMin.z - padding.z };
    const Vector3 paddedMax = { boxMax.x + padding.x, boxMax.y + padding.y, boxMax.z + padding.z };
    return IsBoxOutsideClipSpace(worldViewProjection, paddedMin, paddedMax);
}

} // namespace engine

/**
 * @file PathTrack.h
 * @brief 点を順に通るなめらかな道を作り、道のりから位置・進む向き・速さを引く部品
 * @note カメラや物を決まった道に沿って動かすのに使う（ゲームに依存しない）
 */
#pragma once
#include "Vector3.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace engine {

/**
 * @brief Catmull-Rom 曲線で点をなめらかにつなぎ、道のりで引けるようにした道
 * @note Build で点と速さを渡すと、細かく区切った長さの表を作る。PositionAt などは道のり（ワールド単位）で引く
 */
class PathTrack {
public:
    /** @brief 道の通り道の1点 */
    struct Point {
        Vector3 position; ///< 通る位置
        float speed; ///< この点での進む速さ（ワールド単位/秒）
    };

    /**
     * @brief 点の並びから道を作り直す
     * @param points 通る順の点（2つ未満なら道は無い扱い）
     */
    void Build(const std::vector<Point>& points) {
        points_ = points;
        samples_.clear();
        if (points_.size() < kMinPoints) {
            return;
        }
        // 区間ごとに細かく区切り、そこまでの道のりを積み上げた表を作る
        float length = 0.0f;
        Vector3 previous = points_.front().position;
        samples_.push_back({ 0.0f, 0.0f });
        const size_t segments = points_.size() - 1;
        for (size_t segment = 0; segment < segments; ++segment) {
            for (int step = 1; step <= kStepsPerSegment; ++step) {
                const float t = static_cast<float>(step) / static_cast<float>(kStepsPerSegment);
                const Vector3 current = Evaluate(segment, t);
                length += Distance(previous, current);
                samples_.push_back({ length, static_cast<float>(segment) + t });
                previous = current;
            }
        }
    }

    /** @brief 道が使えるか（点が2つ以上あるか） */
    bool IsValid() const {
        return !samples_.empty();
    }

    /** @brief 道の全長（ワールド単位） */
    float TotalLength() const {
        return samples_.empty() ? 0.0f : samples_.back().distance;
    }

    /** @brief 道のりの位置を返す（範囲の外は端で止める） */
    Vector3 PositionAt(float distance) const {
        if (!IsValid()) {
            return points_.empty() ? Vector3 { 0.0f, 0.0f, 0.0f } : points_.front().position;
        }
        const float u = ParameterAt(distance);
        const size_t segment = SegmentOf(u);
        return Evaluate(segment, u - static_cast<float>(segment));
    }

    /** @brief 道のりの位置で進む向きを返す（長さ1。道が無ければ +X） */
    Vector3 DirectionAt(float distance) const {
        const Vector3 ahead = PositionAt(distance + kDirectionProbe);
        const Vector3 behind = PositionAt(distance - kDirectionProbe);
        const Vector3 delta = Subtract(ahead, behind);
        return engine::Length(delta) > kMinDirectionLength ? Normalize(delta) : Vector3 { 1.0f, 0.0f, 0.0f };
    }

    /** @brief 道のりの位置での速さを返す（前後の点の速さを道に沿ってなめらかに混ぜる） */
    float SpeedAt(float distance) const {
        if (!IsValid()) {
            return 0.0f;
        }
        const float u = ParameterAt(distance);
        const size_t segment = SegmentOf(u);
        const float t = u - static_cast<float>(segment);
        return points_[segment].speed + (points_[segment + 1].speed - points_[segment].speed) * t;
    }

    /** @brief 指定の位置に一番近い所の道のりを返す（途中から始める時に使う） */
    float NearestDistance(const Vector3& position) const {
        float best = 0.0f;
        float bestSquared = -1.0f;
        for (const Sample& sample : samples_) {
            const size_t segment = SegmentOf(sample.parameter);
            const Vector3 point = Evaluate(segment, sample.parameter - static_cast<float>(segment));
            const Vector3 delta = Subtract(point, position);
            const float squared = Dot(delta, delta);
            if (bestSquared < 0.0f || squared < bestSquared) {
                bestSquared = squared;
                best = sample.distance;
            }
        }
        return best;
    }

private:
    /** @brief 長さの表の1行（そこまでの道のりと、曲線の上の位置） */
    struct Sample {
        float distance; ///< 始まりからの道のり
        float parameter; ///< 区間の番号と区間の中の割合を足した値
    };

    static constexpr size_t kMinPoints = 2; ///< 道にするのに必要な点の数
    static constexpr int kStepsPerSegment = 24; ///< 長さを測る時に1区間を区切る数
    static constexpr float kDirectionProbe = 0.25f; ///< 向きを測る時に前後へずらす道のり
    static constexpr float kMinDirectionLength = 1.0e-4f; ///< これより短い差は向きに使わない
    static constexpr float kCatmullHalf = 0.5f; ///< Catmull-Rom の式の係数

    /** @brief 区間の番号と割合から、曲線の上の位置を求める（端の区間は端の点を重ねて使う） */
    Vector3 Evaluate(size_t segment, float t) const {
        const size_t last = points_.size() - 1;
        const Vector3& p0 = points_[segment == 0 ? 0 : segment - 1].position;
        const Vector3& p1 = points_[segment].position;
        const Vector3& p2 = points_[(std::min)(segment + 1, last)].position;
        const Vector3& p3 = points_[(std::min)(segment + 2, last)].position;
        const float t2 = t * t;
        const float t3 = t2 * t;
        // 0.5 * (2p1 + (-p0 + p2)t + (2p0 - 5p1 + 4p2 - p3)t^2 + (-p0 + 3p1 - 3p2 + p3)t^3)
        const auto axis = [&](float a, float b, float c, float d) {
            return kCatmullHalf * (2.0f * b + (-a + c) * t + (2.0f * a - 5.0f * b + 4.0f * c - d) * t2 + (-a + 3.0f * b - 3.0f * c + d) * t3);
        };
        return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.y, p1.y, p2.y, p3.y), axis(p0.z, p1.z, p2.z, p3.z) };
    }

    /** @brief 道のりを、曲線の上の位置（区間の番号＋割合）に直す */
    float ParameterAt(float distance) const {
        const float clamped = std::clamp(distance, 0.0f, TotalLength());
        // 長さの表を二分探索し、前後の行の間を割合で埋める
        const auto upper = std::lower_bound(samples_.begin(), samples_.end(), clamped, [](const Sample& sample, float value) {
            return sample.distance < value;
        });
        if (upper == samples_.begin()) {
            return 0.0f;
        }
        if (upper == samples_.end()) {
            return samples_.back().parameter;
        }
        const Sample& after = *upper;
        const Sample& before = *(upper - 1);
        const float span = after.distance - before.distance;
        const float ratio = span > 0.0f ? (clamped - before.distance) / span : 0.0f;
        return before.parameter + (after.parameter - before.parameter) * ratio;
    }

    /** @brief 曲線の上の位置から区間の番号を返す（最後の点ちょうどは最後の区間に入れる） */
    size_t SegmentOf(float parameter) const {
        const size_t lastSegment = points_.size() - 2;
        return (std::min)(static_cast<size_t>((std::max)(parameter, 0.0f)), lastSegment);
    }

    std::vector<Point> points_; ///< 通る順の点
    std::vector<Sample> samples_; ///< 道のりの表（点が2つ未満なら空）
};

} // namespace engine

/**
 * @file Object3dRenderQueue.h
 * @brief 描く Object3d を集めてから、まとめやすい順と正しく重なる順に並べて描くクラスのファイル
 */
#pragma once
#include "Vector3.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace engine::graphics {
class Model;
class Object3d;

/**
 * @brief 同じパイプラインの設定のまま描く Object3d を集め、並べ替えてから描く列
 * @note 不透明な物はモデルごとにまとめ（頂点とテクスチャの設定を減らす）、同じモデルの中は手前から描く
 *       （奥の物の色の計算を深度で省ける）。半透明な物は不透明な物の後に奥から描く（重なりが正しくなる）。
 *       並べ替えるのは集めた物の中だけなので、パイプラインを切り替える所をまたいで集めないこと
 */
class Object3dRenderQueue {
public:
    /** @brief 1回の描画で数えた値 */
    struct Stats {
        uint32_t submitted = 0; ///< 集めた数
        uint32_t drawn = 0; ///< 実際に描いた数（画面外で飛ばした物を除く）
        uint32_t modelBinds = 0; ///< 頂点とテクスチャを設定し直した回数
        uint32_t drawCalls = 0; ///< 描画の命令の数（まとめ描きした組は1つと数える）
        uint32_t instancedObjects = 0; ///< まとめ描きで描いた物の数
    };

    /** @brief 集めた物を空にする（毎フレームの描き始めに呼ぶ） */
    void Clear();

    /** @brief 描く物を1つ足す（nullptr は無視する） */
    void Add(Object3d* object);

    /**
     * @brief 集めた物を描く順に並べる
     * @param cameraPosition 手前と奥を決めるカメラの位置
     */
    void Sort(const Vector3& cameraPosition);

    /** @brief 集めた数を返す */
    size_t Size() const {
        return entries_.size();
    }

    /**
     * @brief 並べた順で [begin, end) の範囲を描く（バンドルへ分けて記録する時は範囲ごとに呼ぶ）
     * @note 呼ぶ前に ModelCommon::CommonDrawSettings() と Object3d::RebindCommonLighting() を済ませておく
     */
    void DrawRange(size_t begin, size_t end);

    /** @brief 並べた順ですべて描く */
    void DrawAll() {
        DrawRange(0, entries_.size());
    }

    /** @brief 直前の描画で数えた値を返す（範囲に分けて描いた時はその合計） */
    Stats GetStats() const;

    /** @brief 並べ替えを使うかを設定する（比べる時に切る。切ると足した順のまま描く） */
    static void SetSortingEnabled(bool enabled) {
        sSortingEnabled = enabled;
    }
    /** @brief 並べ替えを使うかを返す */
    static bool IsSortingEnabled() {
        return sSortingEnabled;
    }
    /** @brief 同じモデル・同じ見た目の物をまとめて1回で描くかを設定する（並べ替えを切った時は使わない） */
    static void SetInstancingEnabled(bool enabled) {
        sInstancingEnabled = enabled;
    }
    /** @brief まとめ描きを使うかを返す */
    static bool IsInstancingEnabled() {
        return sInstancingEnabled;
    }

    /** @brief 全部の列で、前に取り出してから描いた命令の数とまとめ描きした物の数を取り出して 0 に戻す（フレームに1回） */
    static Stats TakeFrameStats();

    /** @brief まとめて描く組の最少の数（これより少ない組はいつもの描き方の方が手間が少ない） */
    static constexpr size_t kMinInstancedGroup = 2;

private:
    /** @brief 集めた1件と、並べ替えに使う値 */
    struct Entry {
        Object3d* object = nullptr; ///< 描く物
        const Model* model = nullptr; ///< 同じモデルをまとめるための見分け
        float distanceSq = 0.0f; ///< カメラからの距離の2乗
        bool translucent = false; ///< 半透明なら true（不透明な物の後に奥から描く）
        uint64_t batchKey = 0; ///< まとめて描いてよい組の見分け（同じモデルの中で同じ値の物を並べる）
        uint32_t order = 0; ///< 足した順（同じ値の物の順番を毎フレーム同じにする）
    };

    /** @brief 集めた物 */
    std::vector<Entry> entries_;
    /** @brief 範囲ごとに描いた数（バンドルを別のスレッドで記録するので、範囲の先頭の位置ごとに分けて持つ） */
    std::vector<Stats> rangeStats_;

    /** @brief 並べ替えを使うか */
    static bool sSortingEnabled;
    /** @brief まとめ描きを使うか */
    static bool sInstancingEnabled;
    /** @brief 全部の列の、フレームごとの数（範囲を別のスレッドで描くので atomic） */
    static std::atomic<uint32_t> sFrameDrawn;
    static std::atomic<uint32_t> sFrameDrawCalls;
    static std::atomic<uint32_t> sFrameInstanced;

    /** @brief i 番目から、まとめて描ける物が何個続くかを数える（end を越えない） */
    size_t CountGroup(size_t i, size_t end) const;
};

} // namespace engine::graphics

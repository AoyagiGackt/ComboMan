/**
 * @file Object3dRenderQueue.cpp
 * @brief 描く Object3d を集めてから並べ替えて描くクラスの実装
 */
#include "Object3dRenderQueue.h"
#include "Object3d.h"
#include <algorithm>
#include <functional>
namespace engine::graphics {
namespace {

/** @brief 2点の距離の2乗（並べ替えには大小だけ分かればよいので平方根を取らない） */
float DistanceSquared(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}

bool Object3dRenderQueue::sSortingEnabled = true;
bool Object3dRenderQueue::sInstancingEnabled = true;
std::atomic<uint32_t> Object3dRenderQueue::sFrameDrawn = 0;
std::atomic<uint32_t> Object3dRenderQueue::sFrameDrawCalls = 0;
std::atomic<uint32_t> Object3dRenderQueue::sFrameInstanced = 0;

/** @brief 集めた物と数えた値を空にする */
void Object3dRenderQueue::Clear() {
    entries_.clear();
    rangeStats_.clear();
}

/** @brief 描く物を1つ足す */
void Object3dRenderQueue::Add(Object3d* object) {
    if (object == nullptr) {
        return;
    }
    Entry entry;
    entry.object = object;
    entry.order = static_cast<uint32_t>(entries_.size());
    entries_.push_back(entry);
}

/** @brief 不透明な物はモデルごと・手前から、半透明な物は不透明な物の後に奥から、の順に並べる */
void Object3dRenderQueue::Sort(const Vector3& cameraPosition) {
    // 範囲ごとの数は範囲の先頭の位置に書くので、集めた数だけ用意しておく
    rangeStats_.assign(entries_.size(), Stats {});
    if (!sSortingEnabled) {
        return;
    }
    // 並べ替えに使う値を、描く物の今の状態から取る
    for (Entry& entry : entries_) {
        entry.model = entry.object->GetModel();
        entry.translucent = entry.object->IsTranslucent();
        entry.distanceSq = DistanceSquared(entry.object->GetWorldPosition(), cameraPosition);
        entry.batchKey = entry.translucent ? 0 : entry.object->ComputeBatchKey();
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        // 不透明な物を先に描く
        if (a.translucent != b.translucent) {
            return !a.translucent;
        }
        if (a.translucent) {
            // 半透明な物は奥から（遠い方を先に）
            if (a.distanceSq != b.distanceSq) {
                return a.distanceSq > b.distanceSq;
            }
            return a.order < b.order;
        }
        // 不透明な物は同じモデルをまとめ、その中は同じ見た目の物をまとめ、さらにその中は手前から
        if (a.model != b.model) {
            return std::less<const Model*> {}(a.model, b.model);
        }
        if (a.batchKey != b.batchKey) {
            return a.batchKey < b.batchKey;
        }
        if (a.distanceSq != b.distanceSq) {
            return a.distanceSq < b.distanceSq;
        }
        return a.order < b.order;
    });
}

/** @brief 並べた順で範囲を描き、同じモデルが続く間は頂点とテクスチャを設定し直さない */
void Object3dRenderQueue::DrawRange(size_t begin, size_t end) {
    end = (std::min)(end, entries_.size());
    if (begin >= end) {
        return;
    }
    // Sort を呼ばずに描いた時も数を書けるようにする（範囲を分けずに描く時だけここで用意される）
    if (rangeStats_.size() < entries_.size()) {
        rangeStats_.assign(entries_.size(), Stats {});
    }
    Stats stats;
    // 範囲の最初は、前に何が設定されているか分からないので必ず設定する
    const Model* boundModel = nullptr;
    // まとめて描く組の物を並べる場所（範囲ごとに別のスレッドで描くので、スレッドごとに持つ）
    thread_local std::vector<Object3d*> sGroup;
    size_t i = begin;
    while (i < end) {
        const Model* before = boundModel;
        const size_t groupSize = CountGroup(i, end);
        if (groupSize >= kMinInstancedGroup) {
            // 同じモデル・同じ見た目が続く所は、変換行列だけを並べて1回で描く
            sGroup.clear();
            for (size_t k = i; k < i + groupSize; ++k) {
                sGroup.push_back(entries_[k].object);
            }
            const uint32_t drawn = Object3d::DrawInstancedGroup(sGroup.data(), sGroup.size(), boundModel);
            stats.submitted += static_cast<uint32_t>(groupSize);
            stats.drawn += drawn;
            stats.drawCalls += drawn > 0 ? 1u : 0u;
            stats.instancedObjects += drawn > 1 ? drawn : 0u;
        } else {
            // 1体だけの所はいつもの描き方
            ++stats.submitted;
            if (entries_[i].object->DrawBatched(boundModel)) {
                ++stats.drawn;
                ++stats.drawCalls;
            }
        }
        stats.modelBinds += boundModel != before ? 1u : 0u;
        i += groupSize;
    }
    rangeStats_[begin] = stats;
    // 診断パネル用に、全部の列の分を足しておく
    sFrameDrawn += stats.drawn;
    sFrameDrawCalls += stats.drawCalls;
    sFrameInstanced += stats.instancedObjects;
}

/** @brief フレームごとの数を取り出して 0 に戻す */
Object3dRenderQueue::Stats Object3dRenderQueue::TakeFrameStats() {
    Stats stats;
    stats.drawn = sFrameDrawn.exchange(0);
    stats.drawCalls = sFrameDrawCalls.exchange(0);
    stats.instancedObjects = sFrameInstanced.exchange(0);
    return stats;
}

/** @brief 並べ替えた後、同じモデル・同じ見分けの不透明な物が i から何個続くか（まとめ描きを切っている時は 1） */
size_t Object3dRenderQueue::CountGroup(size_t i, size_t end) const {
    const Entry& head = entries_[i];
    // 並べ替えていない時は同じ物が並ぶとは限らず、半透明な物は1体ずつ奥から描くので、まとめない
    if (!sInstancingEnabled || !sSortingEnabled || head.translucent || head.model == nullptr) {
        return 1;
    }
    size_t count = 1;
    while (i + count < end) {
        const Entry& next = entries_[i + count];
        if (next.translucent || next.model != head.model || next.batchKey != head.batchKey) {
            break;
        }
        ++count;
    }
    return count;
}

/** @brief 範囲ごとに数えた値を合計して返す */
Object3dRenderQueue::Stats Object3dRenderQueue::GetStats() const {
    Stats total;
    for (const Stats& stats : rangeStats_) {
        total.submitted += stats.submitted;
        total.drawn += stats.drawn;
        total.modelBinds += stats.modelBinds;
        total.drawCalls += stats.drawCalls;
        total.instancedObjects += stats.instancedObjects;
    }
    return total;
}

} // namespace engine::graphics

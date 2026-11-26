/**
 * @file DeferredReleaseQueue.cpp
 * @brief GPU がまだ使っているかもしれない資源を、使い終わるまで預かってから手放すクラスの実装
 */
#include "DeferredReleaseQueue.h"
#include <algorithm>
namespace engine {

DeferredReleaseQueue* DeferredReleaseQueue::sCurrent = nullptr;

/** @brief 資源をこのフレームの分として預ける */
void DeferredReleaseQueue::Push(Microsoft::WRL::ComPtr<IUnknown> object) {
    if (!object) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    thisFrame_.push_back(std::move(object));
}

/** @brief このフレームに預かった物へ、GPU がこのフレームを終えた目印となるフェンスの値を付ける */
void DeferredReleaseQueue::EndFrame(uint64_t signaledFenceValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    // このフレームの命令（預ける前に記録した描画を含む）が終わると、フェンスがこの値まで進む
    for (auto& object : thisFrame_) {
        pending_.push_back({ signaledFenceValue, std::move(object) });
    }
    thisFrame_.clear();
}

/** @brief GPU が終えた値までの物を手放す */
void DeferredReleaseQueue::ReleaseCompleted(uint64_t completedFenceValue) {
    // 手放す処理（資源の破棄）は重いことがあるので、鍵を持たずに行うよう一度取り出す
    std::vector<PendingObject> released;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto firstPending = std::stable_partition(pending_.begin(), pending_.end(),
            [completedFenceValue](const PendingObject& p) {
                return p.fenceValue <= completedFenceValue;
            });
        released.assign(std::make_move_iterator(pending_.begin()), std::make_move_iterator(firstPending));
        pending_.erase(pending_.begin(), firstPending);
    }
    // released がここで消えて、資源の参照が手放される
}

/** @brief 預かっている物をすべて手放す */
void DeferredReleaseQueue::ReleaseAll() {
    std::vector<PendingObject> pending;
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> thisFrame;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending.swap(pending_);
        thisFrame.swap(thisFrame_);
    }
    // 鍵の外で消えて、資源の参照が手放される
}

/** @brief 預かっている数を返す */
size_t DeferredReleaseQueue::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size() + thisFrame_.size();
}

/** @brief 登録した列へ預ける。列が無ければ object がここで消えてその場で手放される */
void DeferredReleaseQueue::Release(Microsoft::WRL::ComPtr<IUnknown> object) {
    if (sCurrent != nullptr) {
        sCurrent->Push(std::move(object));
    }
}

} // namespace engine

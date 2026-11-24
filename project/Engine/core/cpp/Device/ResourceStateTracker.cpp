/**
 * @file ResourceStateTracker.cpp
 * @brief GPU リソースの今の状態を覚えておき、必要な状態との違いからバリアを作るクラスの実装
 */
#include "ResourceStateTracker.h"
#include "EngineAssert.h"

namespace engine {

/** @brief リソースを今の状態と一緒に登録する */
void ResourceStateTracker::Register(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    if (resource == nullptr) {
        return;
    }
    states_[resource] = state;
}

/** @brief リソースの登録を外す */
void ResourceStateTracker::Unregister(ID3D12Resource* resource) {
    states_.erase(resource);
}

/** @brief リソースが登録されているか */
bool ResourceStateTracker::IsRegistered(ID3D12Resource* resource) const {
    return states_.contains(resource);
}

/** @brief 今の状態と違えば遷移バリアを積み、覚えている状態を進める */
void ResourceStateTracker::Require(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    const auto found = states_.find(resource);
    // 登録していないリソースは状態が分からないので、バリアを作れない（宣言の書き忘れを早く見つける）
    ENGINE_ASSERT(found != states_.end());
    if (found == states_.end() || found->second == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier { };
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = found->second;
    barrier.Transition.StateAfter = state;
    pending_.push_back(barrier);
    found->second = state;
}

/** @brief 積んだバリアをまとめて発行する */
void ResourceStateTracker::Flush(ID3D12GraphicsCommandList* commandList) {
    if (pending_.empty()) {
        return;
    }
    commandList->ResourceBarrier(static_cast<UINT>(pending_.size()), pending_.data());
    pending_.clear();
}

/** @brief 覚えている今の状態を返す */
D3D12_RESOURCE_STATES ResourceStateTracker::GetState(ID3D12Resource* resource) const {
    const auto found = states_.find(resource);
    return found != states_.end() ? found->second : D3D12_RESOURCE_STATE_COMMON;
}

} // namespace engine

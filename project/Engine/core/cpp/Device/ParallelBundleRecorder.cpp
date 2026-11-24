/**
 * @file ParallelBundleRecorder.cpp
 * @brief 描画の命令を複数のスレッドで同時にバンドルへ記録し、メインのコマンドリストから順に呼ぶクラスの実装
 */
#include "ParallelBundleRecorder.h"
#include "EngineAssert.h"
#include "WorkerPool.h"

namespace engine {
namespace {
/** @brief 今のスレッドが記録中のバンドル（記録している間だけ入っている） */
thread_local ID3D12GraphicsCommandList* tRecordingBundle = nullptr;
} // namespace

/** @brief バンドルを作るデバイスとフレームの数を設定する */
void ParallelBundleRecorder::Initialize(ID3D12Device* device, UINT frameCount) {
    device_ = device;
    frameCount_ = frameCount;
}

/** @brief これから記録するフレームのアロケータを使い直す */
void ParallelBundleRecorder::BeginFrame(UINT frameIndex) {
    frameIndex_ = frameIndex;
    // この番号の前回分は GPU が読み終えているので、記録した中身ごと捨ててよい
    for (Slot& slot : slots_) {
        const HRESULT hr = slot.allocators[frameIndex_]->Reset();
        ENGINE_ASSERT(SUCCEEDED(hr));
    }
    usedThisFrame_ = 0;
}

/** @brief このフレームで使うバンドルが足りるよう、必要な数まで作り足す */
void ParallelBundleRecorder::EnsureSlots(size_t required) {
    while (slots_.size() < required) {
        Slot slot;
        for (UINT frame = 0; frame < frameCount_; ++frame) {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            HRESULT hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&allocator));
            ENGINE_ASSERT(SUCCEEDED(hr));
            slot.allocators.push_back(allocator);
        }
        HRESULT hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, slot.allocators[frameIndex_].Get(), nullptr, IID_PPV_ARGS(&slot.bundle));
        ENGINE_ASSERT(SUCCEEDED(hr));
        // 作った直後は記録中なので閉じておき、使う時に Reset で開き直す
        hr = slot.bundle->Close();
        ENGINE_ASSERT(SUCCEEDED(hr));
        slots_.push_back(std::move(slot));
    }
}

/** @brief count 個のバンドルを別々のスレッドで記録し、directList から番号の順に実行する */
void ParallelBundleRecorder::RecordAndExecute(ID3D12GraphicsCommandList* directList, int count, const std::function<void(int)>& record) {
    if (count <= 0 || device_ == nullptr) {
        return;
    }
    const size_t first = usedThisFrame_;
    EnsureSlots(first + static_cast<size_t>(count));
    // 開き直しはメインのスレッドでまとめて行う（記録はスレッドごとに別のバンドルなので同時に進めてよい）
    for (int i = 0; i < count; ++i) {
        Slot& slot = slots_[first + i];
        const HRESULT hr = slot.bundle->Reset(slot.allocators[frameIndex_].Get(), nullptr);
        ENGINE_ASSERT(SUCCEEDED(hr));
    }
    WorkerPool::GetInstance()->ParallelFor(count, [&](int index) {
        ID3D12GraphicsCommandList* bundle = slots_[first + index].bundle.Get();
        tRecordingBundle = bundle;
        record(index);
        tRecordingBundle = nullptr;
        const HRESULT hr = bundle->Close();
        ENGINE_ASSERT(SUCCEEDED(hr));
    });
    // 記録した順番どおりに呼ぶ（描く順が変わらないので、半透明の重なりも今までと同じ）
    for (int i = 0; i < count; ++i) {
        directList->ExecuteBundle(slots_[first + i].bundle.Get());
    }
    usedThisFrame_ = first + static_cast<size_t>(count);
}

/** @brief 今のスレッドが記録中のバンドルを返す */
ID3D12GraphicsCommandList* ParallelBundleRecorder::ThreadCommandList() {
    return tRecordingBundle;
}

/** @brief 同時に記録できるスレッドの数 */
int ParallelBundleRecorder::GetParallelism() {
    return WorkerPool::GetInstance()->GetWorkerCount();
}

} // namespace engine

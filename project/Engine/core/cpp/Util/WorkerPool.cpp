/**
 * @file WorkerPool.cpp
 * @brief 起動時に作ったスレッドを使い回して、仕事を分けて同時に進める小さなスレッドの集まりの実装
 */
#include "WorkerPool.h"
#include <algorithm>

namespace engine {
namespace {
/** @brief スレッドの数の上限（描画の記録を分けるだけなので、多すぎても分け損になる） */
constexpr unsigned int kMaxWorkers = 4;
/** @brief コアの数が分からない時に使うスレッドの数 */
constexpr unsigned int kFallbackWorkers = 2;
/** @brief ゲームの更新と描画をするメインのスレッドのぶん、コアの数から引く数 */
constexpr unsigned int kReservedForMainThread = 1;
} // namespace

/** @brief 共有のスレッドの集まりを返す */
WorkerPool* WorkerPool::GetInstance() {
    static WorkerPool sInstance;
    return &sInstance;
}

/** @brief スレッドを作る（数は CPU のコア数から決める） */
WorkerPool::WorkerPool() {
    const unsigned int cores = std::thread::hardware_concurrency();
    const unsigned int available = cores > kReservedForMainThread ? cores - kReservedForMainThread : kFallbackWorkers;
    const unsigned int count = std::clamp(available, 1u, kMaxWorkers);
    for (unsigned int i = 0; i < count; ++i) {
        threads_.emplace_back([this] {
            WorkerLoop();
        });
    }
}

/** @brief 作ったスレッドを止めて片付ける */
WorkerPool::~WorkerPool() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wakeWorkers_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

/** @brief 番号ごとに job を呼び、全部終わるまで待つ */
void WorkerPool::ParallelFor(int count, const std::function<void(int)>& job) {
    if (count <= 0) {
        return;
    }
    std::unique_lock lock(mutex_);
    job_ = &job;
    jobCount_ = count;
    nextIndex_ = 0;
    finishedCount_ = 0;
    ++generation_;
    wakeWorkers_.notify_all();
    wakeCaller_.wait(lock, [this] {
        return finishedCount_ == jobCount_;
    });
    job_ = nullptr;
    jobCount_ = 0;
}

/** @brief 仕事が来るまで寝て、来たら番号を1つずつ取って進める */
void WorkerPool::WorkerLoop() {
    unsigned long long seenGeneration = 0;
    std::unique_lock lock(mutex_);
    while (true) {
        wakeWorkers_.wait(lock, [this, &seenGeneration] {
            return stopping_ || (generation_ != seenGeneration && nextIndex_ < jobCount_);
        });
        if (stopping_) {
            return;
        }
        // 残っている番号を1つずつ取り、順番待ちを外して進める
        while (job_ != nullptr && nextIndex_ < jobCount_) {
            const int index = nextIndex_++;
            const std::function<void(int)>* job = job_;
            lock.unlock();
            (*job)(index);
            lock.lock();
            ++finishedCount_;
            if (finishedCount_ == jobCount_) {
                wakeCaller_.notify_one();
            }
        }
        seenGeneration = generation_;
    }
}

} // namespace engine

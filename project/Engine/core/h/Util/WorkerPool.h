/**
 * @file WorkerPool.h
 * @brief 起動時に作ったスレッドを使い回して、仕事を分けて同時に進める小さなスレッドの集まり
 */
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

/**
 * @brief 決まった数のスレッドを持ち続け、番号で分けた仕事を同時に進めて、全部終わるまで待つ
 * @note 毎フレームスレッドを作ると作る時間がかかるので、スレッドは最初に作って寝かせておく
 */
class WorkerPool {
public:
    /** @brief 共有のスレッドの集まりを返す（最初に呼んだ時にスレッドを作る） */
    static WorkerPool* GetInstance();

    /** @brief 作ったスレッドを止めて片付ける */
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    /** @brief 仕事を同時に進められるスレッドの数 */
    int GetWorkerCount() const {
        return static_cast<int>(threads_.size());
    }

    /**
     * @brief 0〜count-1 の番号ごとに job を呼び、全部終わるまで待つ
     * @param count 仕事の数
     * @param job 番号を受け取って1つ分の仕事をする処理（別のスレッドから同時に呼ばれる）
     * @note 呼んだスレッドは待つだけ。job の中でこの関数を呼び直してはいけない
     */
    void ParallelFor(int count, const std::function<void(int)>& job);

private:
    /** @brief スレッドを作る（数は CPU のコア数から決める） */
    WorkerPool();

    /** @brief 各スレッドの中身。仕事が来るまで寝て、来たら番号を1つずつ取って進める */
    void WorkerLoop();

    std::vector<std::thread> threads_; ///< 仕事をするスレッド
    std::mutex mutex_; ///< 下の値を守る
    std::condition_variable wakeWorkers_; ///< 仕事が来たことをスレッドへ知らせる
    std::condition_variable wakeCaller_; ///< 仕事が全部終わったことを呼んだ側へ知らせる
    const std::function<void(int)>* job_ = nullptr; ///< 今の仕事（ParallelFor の間だけ有効）
    int jobCount_ = 0; ///< 今の仕事の数
    int nextIndex_ = 0; ///< 次に渡す仕事の番号
    int finishedCount_ = 0; ///< 終わった仕事の数
    unsigned long long generation_ = 0; ///< ParallelFor を呼んだ回数（新しい仕事が来たかの見分けに使う）
    bool stopping_ = false; ///< 片付けの途中か
};

} // namespace engine

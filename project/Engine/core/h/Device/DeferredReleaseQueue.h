/**
 * @file DeferredReleaseQueue.h
 * @brief GPU がまだ使っているかもしれない資源を、使い終わるまで預かってから手放すクラスのファイル
 */
#pragma once
#include <Windows.h>
#include <wrl/client.h>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>
namespace engine {

/**
 * @brief 捨てられた GPU 資源を、そのフレームの描画を GPU が終えるまで預かる列
 * @note 描画は何フレームか並べて進むので、更新の途中で資源を手放すと、前のフレームの命令がまだ読んでいて
 *       デバッグレイヤーの #921（使用中の物を消した）で落ちる。手放す代わりにここへ預け、
 *       フレームの終わりに積んだフェンスの値まで GPU が進んだら本当に手放す。
 *       DirectXCommon が1つ持ち、作った時に Current として登録する。
 */
class DeferredReleaseQueue {
public:
    /** @brief 自分が登録されたまま消える時は登録を外す（消えた列へ預けないように） */
    ~DeferredReleaseQueue() {
        if (sCurrent == this) {
            sCurrent = nullptr;
        }
    }

    /**
     * @brief 資源を預ける（どのスレッドから呼んでもよい）
     * @param object 手放したい資源（空なら何もしない）
     */
    void Push(Microsoft::WRL::ComPtr<IUnknown> object);

    /**
     * @brief このフレームに預かった物へ、フレームの終わりに積んだフェンスの値を付ける
     * @param signaledFenceValue このフレームの命令を出した後に積んだフェンスの値
     */
    void EndFrame(uint64_t signaledFenceValue);

    /**
     * @brief GPU が終えたフェンスの値までの物を手放す
     * @param completedFenceValue GPU が終えたフェンスの値
     */
    void ReleaseCompleted(uint64_t completedFenceValue);

    /** @brief 預かっている物をすべて手放す（GPU を待ち終えた後だけ呼ぶ） */
    void ReleaseAll();

    /** @brief 預かっている数を返す（このフレームの分も含む） */
    size_t GetPendingCount() const;

    /** @brief どこからでも預けられるよう、今使う列を登録する（nullptr で外す） */
    static void SetCurrent(DeferredReleaseQueue* queue) {
        sCurrent = queue;
    }

    /**
     * @brief 登録した列へ資源を預ける。列が無い時（起動前・終了後）はその場で手放す
     * @param object 手放したい資源
     */
    static void Release(Microsoft::WRL::ComPtr<IUnknown> object);

private:
    /** @brief フェンスの値を付けて待たせている資源 */
    struct PendingObject {
        uint64_t fenceValue = 0; ///< GPU がこの値まで進んだら手放してよい
        Microsoft::WRL::ComPtr<IUnknown> object; ///< 預かっている資源
    };

    /** @brief Push とフレームの区切りが別のスレッドから来てもよいように守る */
    mutable std::mutex mutex_;
    /** @brief このフレームに預かり、まだフェンスの値が付いていない物 */
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> thisFrame_;
    /** @brief フェンスの値を付けて GPU を待っている物 */
    std::vector<PendingObject> pending_;

    /** @brief Release が預ける先 */
    static DeferredReleaseQueue* sCurrent;
};

/**
 * @brief 消える時と付け替える時に、持っていた GPU 資源を DeferredReleaseQueue へ預ける ComPtr
 * @note ComPtr を受け取る関数にそのまま渡せる（ComPtr を継承している）。メンバの型を
 *       Microsoft::WRL::ComPtr から置き換えるだけで、描画中に持ち主が消えても落ちなくなる
 */
template<class T>
class DeferredComPtr : public Microsoft::WRL::ComPtr<T> {
public:
    using Base = Microsoft::WRL::ComPtr<T>;

    /** @brief 空で作る */
    DeferredComPtr() = default;
    /** @brief 空で作る（nullptr から） */
    DeferredComPtr(std::nullptr_t) {
    }
    /** @brief ComPtr から受け取る */
    DeferredComPtr(Base other) noexcept
        : Base(std::move(other)) {
    }
    /** @brief 写しを作る（参照の数が増えるだけ） */
    DeferredComPtr(const DeferredComPtr& other) = default;
    /** @brief 中身を移す（移した元は空になる） */
    DeferredComPtr(DeferredComPtr&& other) noexcept = default;

    /** @brief 消える時に、持っていた資源を預ける */
    ~DeferredComPtr() {
        DeferCurrent();
    }

    /** @brief 別の資源に付け替える（前の資源は預ける） */
    DeferredComPtr& operator=(const DeferredComPtr& other) {
        if (this != &other) {
            DeferCurrent();
            Base::operator=(other);
        }
        return *this;
    }
    /** @brief 別の資源に付け替える（前の資源は預ける） */
    DeferredComPtr& operator=(DeferredComPtr&& other) noexcept {
        if (this != &other) {
            DeferCurrent();
            Base::operator=(std::move(other));
        }
        return *this;
    }
    /** @brief ComPtr を受け取って付け替える（前の資源は預ける） */
    DeferredComPtr& operator=(Base other) noexcept {
        DeferCurrent();
        Base::operator=(std::move(other));
        return *this;
    }
    /** @brief 空にする（前の資源は預ける） */
    DeferredComPtr& operator=(std::nullptr_t) noexcept {
        DeferCurrent();
        return *this;
    }

    /** @brief 空にする（前の資源は預ける。ComPtr::Reset と違い、その場では手放さない） */
    void Reset() {
        DeferCurrent();
    }

private:
    /** @brief 今持っている資源を列へ預けて、自分は空になる */
    void DeferCurrent() {
        if (this->Get() == nullptr) {
            return;
        }
        // 参照を1つ持ったまま IUnknown として預ける（増減させずに手渡す）
        Microsoft::WRL::ComPtr<IUnknown> object;
        object.Attach(this->Detach());
        DeferredReleaseQueue::Release(std::move(object));
    }
};

} // namespace engine

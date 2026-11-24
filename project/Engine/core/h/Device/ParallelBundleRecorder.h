/**
 * @file ParallelBundleRecorder.h
 * @brief 描画の命令を複数のスレッドで同時にバンドルへ記録し、メインのコマンドリストから順に呼ぶクラス
 */
#pragma once
#include <d3d12.h>
#include <functional>
#include <vector>
#include <wrl/client.h>

namespace engine {

/**
 * @brief 描画の命令を複数のスレッドでバンドルへ同時に記録し、メインのコマンドリストから記録した順に実行する
 * @note バンドルは呼び出したコマンドリストの描画先・ビューポート・シザーをそのまま使うので、
 *       描画先を知らなくても途中の描画だけを分けて記録できる。
 *       記録する処理の中では DirectXCommon::GetCommandList() がそのスレッドのバンドルを返す。
 *       バンドルの中では描画先の設定・バリア・クリアはできない（ルートシグネチャ・PSO・デスクリプタヒープは自分で設定する）
 */
class ParallelBundleRecorder {
public:
    /**
     * @brief バンドルを作るデバイスと、同時に処理中になりうるフレームの数を設定する
     * @param device バンドルとアロケータを作るデバイス
     * @param frameCount フレームの数（バックバッファの数）
     */
    void Initialize(ID3D12Device* device, UINT frameCount);

    /**
     * @brief これから記録するフレームのアロケータを使い直す
     * @param frameIndex バックバッファの番号（GPU がこの番号の前回分を読み終えたことを確かめてから呼ぶ）
     */
    void BeginFrame(UINT frameIndex);

    /**
     * @brief count 個のバンドルを別々のスレッドで記録し、directList から番号の順に実行する
     * @param directList バンドルを呼び出すメインのコマンドリスト
     * @param count 分ける数
     * @param record 番号を受け取って、その分の描画を記録する処理（別のスレッドから同時に呼ばれる）
     */
    void RecordAndExecute(ID3D12GraphicsCommandList* directList, int count, const std::function<void(int)>& record);

    /** @brief 今のスレッドが記録中のバンドルを返す（記録中でなければ nullptr） */
    static ID3D12GraphicsCommandList* ThreadCommandList();

    /** @brief 同時に記録できるスレッドの数（分ける数の目安） */
    static int GetParallelism();

    /** @brief 複数のスレッドで記録するか（切ると呼び出し側は今までどおり1本のリストへ積む。比べる時や不具合を切り分ける時に使う） */
    static bool IsEnabled() {
        return sEnabled;
    }
    /** @brief 複数のスレッドで記録するかを切り替える（メインのスレッドから呼ぶ） */
    static void SetEnabled(bool enabled) {
        sEnabled = enabled;
    }

private:
    /** @brief 複数のスレッドで記録するか */
    static inline bool sEnabled = true;

    /** @brief バンドル1本と、フレームごとのアロケータ */
    struct Slot {
        std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> allocators; ///< フレームの番号ごとのアロケータ
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> bundle; ///< 毎フレーム記録し直すバンドル
    };

    /** @brief このフレームで使うバンドルが足りるよう、必要な数まで作り足す */
    void EnsureSlots(size_t required);

    ID3D12Device* device_ = nullptr;
    UINT frameCount_ = 0;
    UINT frameIndex_ = 0;
    std::vector<Slot> slots_; ///< 作ったバンドル（フレームをまたいで使い回す）
    size_t usedThisFrame_ = 0; ///< このフレームで使ったバンドルの数（1つのバンドルは1フレームに1回しか記録しない）
};

} // namespace engine

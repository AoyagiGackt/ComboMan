/**
 * @file ResourceStateTracker.h
 * @brief GPU リソースの今の状態を覚えておき、必要な状態との違いからバリアを作るクラス
 */
#pragma once
#include <d3d12.h>
#include <unordered_map>
#include <vector>

namespace engine {

/**
 * @brief 登録した GPU リソースの今の状態を覚え、求められた状態と違う時だけ遷移バリアを積む
 * @note 描画パスが読む／書くリソースを宣言すると、RenderPassGraph がここを通してバリアを自動で入れる。
 *       登録したリソースは、ここ以外で状態を変えたら同じ関数のうちに元の状態へ戻すこと（戻さないと覚えている状態とずれる）
 */
class ResourceStateTracker {
public:
    /**
     * @brief リソースを今の状態と一緒に登録する（同じリソースをもう一度登録すると状態を上書きする）
     * @param resource 状態を覚えるリソース
     * @param state 登録した時点の状態（作った時の初期状態）
     */
    void Register(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);

    /** @brief リソースの登録を外す（解放する前に呼ぶ） */
    void Unregister(ID3D12Resource* resource);

    /** @brief リソースが登録されているか */
    bool IsRegistered(ID3D12Resource* resource) const;

    /**
     * @brief リソースを指定の状態にしたいと伝える。今の状態と違えば遷移バリアを積む（発行は Flush）
     * @param resource 登録済みのリソース
     * @param state 次の処理で必要な状態
     */
    void Require(ID3D12Resource* resource, D3D12_RESOURCE_STATES state);

    /** @brief 積んだバリアをまとめて1回で発行する（積んでいなければ何もしない） */
    void Flush(ID3D12GraphicsCommandList* commandList);

    /** @brief 覚えている今の状態を返す（登録されていなければ COMMON） */
    D3D12_RESOURCE_STATES GetState(ID3D12Resource* resource) const;

private:
    /** @brief リソースごとの今の状態 */
    std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
    /** @brief まだ発行していない遷移バリア */
    std::vector<D3D12_RESOURCE_BARRIER> pending_;
};

} // namespace engine

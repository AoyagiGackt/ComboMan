/**
 * @file RenderPassGraph.h
 * @brief 名前付き描画パスを登録順に実行し、パスが読む／書くリソースの宣言から状態遷移バリアを自動で入れる軽量Render Graph
 */
#pragma once
#include "DirectXCommon.h"
#include "EngineAssert.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::graphics {

/** @brief 描画パスが使うリソース1つと、使う間に必要な状態 */
struct PassResource {
    ID3D12Resource* resource = nullptr; ///< 使うリソース（nullptr の項目は無視する）
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; ///< パスの間に必要な状態
};

/** @brief 描画パスが読むリソースと書くリソースの宣言 */
struct PassResources {
    std::vector<PassResource> reads; ///< 読むだけのリソース（シェーダーから読む・コピー元など）
    std::vector<PassResource> writes; ///< 書き込むリソース（描画先・コピー先など）
};

/** @brief ピクセルシェーダーから読むテクスチャの宣言を作る */
inline PassResource ReadTexture(ID3D12Resource* resource) {
    return { resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
}

/** @brief 描画先として書き込むテクスチャの宣言を作る */
inline PassResource WriteRenderTarget(ID3D12Resource* resource) {
    return { resource, D3D12_RESOURCE_STATE_RENDER_TARGET };
}

/**
 * @brief 1フレーム内の描画パス順序と診断名、パスごとのリソースの使い方をまとめて管理する
 * @note 宣言したリソースは DirectXCommon の ResourceStateTracker に登録しておくこと。
 *       各パスを実行する直前に、宣言した状態と今の状態の違いだけ遷移バリアをまとめて1回で発行する。
 *       宣言の無いパスは今までどおり、パスの中でバリアを書く
 */
class RenderPassGraph {
public:
    /** @brief グラフの実行に使うDirectX基盤を設定する */
    explicit RenderPassGraph(engine::DirectXCommon* dxCommon)
        : dxCommon_(dxCommon) {
    }

    /**
     * @brief 読み書きするリソースを宣言しない描画パスを末尾へ追加する
     * @param name GPU診断とDREDへ記録するパス名
     * @param execute 描画コマンドを記録する処理
     * @param enabled falseの場合はパスを登録するが実行しない
     */
    void AddPass(std::string name, std::function<void()> execute, bool enabled = true) {
        AddPass(std::move(name), PassResources { }, std::move(execute), enabled);
    }

    /**
     * @brief 読み書きするリソースを宣言した描画パスを末尾へ追加する
     * @param name GPU診断とDREDへ記録するパス名
     * @param resources このパスが読むリソースと書くリソース
     * @param execute 描画コマンドを記録する処理（宣言したリソースは、ここに入った時に宣言した状態になっている）
     * @param enabled falseの場合はパスを登録するが実行しない（バリアも入れない）
     */
    void AddPass(std::string name, PassResources resources, std::function<void()> execute, bool enabled = true) {
        passes_.push_back({ std::move(name), std::move(resources), std::move(execute), enabled });
    }

    /** @brief 有効な描画パスを登録順に、宣言どおりの状態へ遷移させてから実行する */
    void Execute() {
        ResourceStateTracker& tracker = dxCommon_->GetResourceStateTracker();
        for (const Pass& pass : passes_) {
            if (!pass.enabled) {
                continue;
            }
            // D3D12のBeginEventはPIX形式のメタデータが必要になる環境があるため使わない
            // パス名はDirectXCommonの診断コンテキストへ保存してエラーログへ付加する
            dxCommon_->SetDiagnosticContext(pass.name);
            ValidateDeclarations(pass.resources);
            for (const PassResource& use : pass.resources.reads) {
                if (use.resource != nullptr) {
                    tracker.Require(use.resource, use.state);
                }
            }
            for (const PassResource& use : pass.resources.writes) {
                if (use.resource != nullptr) {
                    tracker.Require(use.resource, use.state);
                }
            }
            tracker.Flush(dxCommon_->GetCommandList());
            pass.execute();
        }
    }

private:
    /**
     * @brief 登録された1描画パス分の名前・使うリソース・実行処理・有効フラグ
     */
    struct Pass {
        std::string name;
        PassResources resources;
        std::function<void()> execute;
        bool enabled = true;
    };

    /** @brief 同じリソースを1つのパスで読みながら書く宣言は、状態が1つに決まらないので受け付けない */
    static void ValidateDeclarations(const PassResources& resources) {
        for (const PassResource& read : resources.reads) {
            for (const PassResource& write : resources.writes) {
                ENGINE_ASSERT(read.resource == nullptr || read.resource != write.resource);
            }
        }
    }

    engine::DirectXCommon* dxCommon_ = nullptr;
    std::vector<Pass> passes_;
};

} // namespace engine::graphics

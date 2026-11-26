/**
 * @file PipelineCache.h
 * @brief パイプライン（PSO）を作る時の保存と再利用、シェーダーを書き換えた時の作り直しを受け持つクラスを定義するファイル
 */
#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
class DirectXCommon;
}

namespace engine::graphics {

/**
 * @brief パイプラインの保存・再利用と、シェーダーのホットリロードを受け持つシングルトンクラス
 * @note 保存と再利用: 作ったパイプラインを ID3D12PipelineLibrary に入れ、終了の時にファイルへ書く。
 *       次の起動では同じ設定のパイプラインをファイルから取り出すので、ドライバのコンパイルを省ける。
 *       ホットリロード（USE_IMGUI の版だけ）: 作ったパイプラインの設定と元のシェーダーのファイルを覚えておき、
 *       ファイルが書き換わったらコンパイルし直して作り直す。描く側は SetPipelineState の前に Resolve で今のものを引く
 */
class PipelineCache {
public:
    /** @brief 保存と再利用の数（診断の表示用） */
    struct Stats {
        uint32_t hits = 0; ///< ファイルから取り出せた数
        uint32_t misses = 0; ///< 新しく作った数
        double createMilliseconds = 0.0; ///< パイプラインを用意するのにかかった時間の合計
        uint32_t reloaded = 0; ///< ホットリロードで作り直した数（起動してからの合計）
    };

    /** @brief 共有インスタンスを取得する */
    static PipelineCache* GetInstance() {
        static PipelineCache sInstance;
        return &sInstance;
    }

    /**
     * @brief 保存したファイルを読み、パイプラインの入れ物を作る（ファイルが無い・合わない時は空から始める）
     * @param dxCommon DirectX共通基盤（シェーダーの元のファイルを引くのと、GPU を待つのに使う）
     */
    void Initialize(engine::DirectXCommon* dxCommon);
    /** @brief 新しく入れた物があればファイルへ書き、入れ物を手放す */
    void Finalize();

    /** @brief グラフィックス用のパイプラインを、保存した物があれば取り出し、無ければ作って保存する */
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphics(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
    /** @brief コンピュート用のパイプラインを、保存した物があれば取り出し、無ければ作って保存する */
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCompute(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc);

    /**
     * @brief ホットリロードで作り直した後のパイプラインを引く（作り直していなければそのまま返す）
     * @note SetPipelineState に渡す直前に通す
     */
    static ID3D12PipelineState* Resolve(ID3D12PipelineState* pipeline) {
        return GetInstance()->ResolveReplacement(pipeline);
    }

    /**
     * @brief ルートシグネチャに、シリアライズした中身から作った目印を付ける（同じ設定のパイプラインを見分けるのに使う）
     * @param rootSignature 目印を付けるルートシグネチャ
     * @param serialized シリアライズした中身
     */
    static void TagRootSignature(ID3D12RootSignature* rootSignature, ID3DBlob* serialized);

    /** @brief シェーダーのファイルが書き換わったかを調べ、書き換わっていればパイプラインを作り直す（Update の中で呼ぶ） */
    void CheckHotReload();
    /** @brief ホットリロードを使うかを設定する */
    void SetHotReloadEnabled(bool enabled) {
        hotReloadEnabled_ = enabled;
    }
    /** @brief ホットリロードを使うかを返す */
    bool IsHotReloadEnabled() const {
        return hotReloadEnabled_;
    }
    /** @brief 保存と再利用の数を返す */
    Stats GetStats() const;

private:
    /** @brief ホットリロードのために覚えておく、シェーダー1段ぶんの元のファイル */
    struct ShaderStageSource {
        std::wstring path; ///< 空ならこの段は使っていない
        std::wstring profile;
    };
    /** @brief ホットリロードのために覚えておく、パイプライン1つぶんの設定 */
    struct TrackedPipeline {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> original; ///< 消えた時に知らせを受け取れない環境だけ、キーのアドレスが使い回されないよう持っておく
        Microsoft::WRL::ComPtr<ID3D12PipelineState> current; ///< 今使う物（作り直していなければ空）
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature; ///< 作り直す時に使うルートシグネチャ
        bool isCompute = false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsDesc {}; ///< ポインタの所は作り直す時に差し替える
        D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc {};
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements; ///< 頂点の並び（名前のポインタは空。作り直す時に semanticNames から付ける）
        std::vector<std::string> semanticNames;
        std::vector<ShaderStageSource> stages; ///< グラフィックスは VS・PS・DS・HS・GS の順、コンピュートは CS だけ
    };
    /** @brief 調べるファイル1つぶんの、前に見た書き換えの時刻 */
    using FileTimes = std::unordered_map<std::wstring, std::filesystem::file_time_type>;

    /** @brief 資源が未確保の状態を作る */
    PipelineCache() = default;
    ~PipelineCache() = default;
    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    /** @brief 保存したファイルの中身から入れ物を作る（だめなら空の入れ物を作る） */
    void CreateLibrary(ID3D12Device* device);
    /** @brief 名前で保存した物を探し、無ければ作って保存する（グラフィックスとコンピュートで共通の流れ） */
    template <class Desc, class LoadFn, class CreateFn>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> LoadOrCreate(const std::wstring& name, const Desc& desc, LoadFn load, CreateFn create);
    /** @brief 作り直した後のパイプラインを引く */
    ID3D12PipelineState* ResolveReplacement(ID3D12PipelineState* pipeline) const;
    /** @brief グラフィックス用のパイプラインの設定と元のシェーダーを覚える（ホットリロード用） */
    void TrackGraphics(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
    /** @brief コンピュート用のパイプラインの設定と元のシェーダーを覚える（ホットリロード用） */
    void TrackCompute(const Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc);
    /** @brief 覚えたパイプラインが消えた時に記録を消すよう頼む（頼めない環境なら false） */
    static bool WatchDestruction(ID3D12PipelineState* pipeline);
    /** @brief パイプラインが消える時に D3D12 から呼ばれ、そのパイプラインの記録を消す */
    static void WINAPI OnPipelineDestroyed(void* pipeline);
    /** @brief バイトコードから元のファイルを引いて、シェーダー1段ぶんの元を作る（引けなければ空） */
    ShaderStageSource FindStageSource(const D3D12_SHADER_BYTECODE& bytecode) const;
    /** @brief 調べる対象のファイル（覚えたシェーダーと、シェーダーのフォルダの共通ファイル）の今の時刻を集める */
    FileTimes CollectFileTimes() const;
    /** @brief 覚えたパイプラインのうち、書き換わったファイルを使う物を作り直す */
    void RebuildChanged(const std::vector<std::wstring>& changedFiles, bool rebuildAll);
    /** @brief 覚えたパイプライン1つを、コンパイルし直したシェーダーで作り直す（だめなら空） */
    Microsoft::WRL::ComPtr<ID3D12PipelineState> Rebuild(const TrackedPipeline& tracked,
        const std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>>& compiled) const;

    engine::DirectXCommon* dxCommon_ = nullptr;

    // 保存と再利用
    Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> library_;
    std::vector<char> libraryBlob_; ///< 入れ物が読む元のファイルの中身（入れ物を手放すまで持っておく）
    bool libraryDirty_ = false; ///< 新しく入れた物があるか（あれば終了の時に書く）
    mutable std::mutex libraryMutex_;
    Stats stats_;

    // ホットリロード
    std::unordered_map<ID3D12PipelineState*, TrackedPipeline> tracked_;
    mutable std::shared_mutex trackedMutex_;
    std::atomic<bool> anyReplaced_ = false; ///< 1つでも作り直したか（無ければ Resolve は表を引かない）
    bool hotReloadEnabled_ = true;
    FileTimes fileTimes_; ///< 前に調べた時のファイルの時刻
    std::chrono::steady_clock::time_point lastCheck_ {}; ///< 前に調べた時刻
};

} // namespace engine::graphics

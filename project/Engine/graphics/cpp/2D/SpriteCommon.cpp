/**
 * @file SpriteCommon.cpp
 * @brief スプライト共通のルートシグネチャ・PSO（SpriteCommon）の実装
 */
#include "SpriteCommon.h"
#include "EngineAssert.h"
#include "Logger.h"
#include "PipelineHelper.h"

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace ph = engine::graphics::pipelineHelper;

namespace {
/** @brief GPU がバインドレス（シェーダーからデスクリプタヒープを番号で直接引く）に対応しているか */
bool SupportsBindless(ID3D12Device* device) {
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel { D3D_SHADER_MODEL_6_6 };
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
        || shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS options { };
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
        return false;
    }
    return options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
}

/** @brief 定数バッファを直接渡すルート引数を作る（1.1 形式。中身は描画ごとに別の場所へ書くので、変わり得る扱いにする） */
D3D12_ROOT_PARAMETER1 MakeVolatileCbvParam(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER1 param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    param.ShaderVisibility = visibility;
    param.Descriptor.ShaderRegister = shaderRegister;
    param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
    return param;
}

/** @brief 32bit の値を1つ渡すルート定数の引数を作る（1.1 形式） */
D3D12_ROOT_PARAMETER1 MakeSingleConstantParam(UINT shaderRegister, D3D12_SHADER_VISIBILITY visibility) {
    D3D12_ROOT_PARAMETER1 param { };
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.ShaderVisibility = visibility;
    param.Constants.ShaderRegister = shaderRegister;
    param.Constants.RegisterSpace = 0;
    param.Constants.Num32BitValues = 1;
    return param;
}

/**
 * @brief バインドレス版のルートシグネチャを作る（マテリアル・変換行列・テクスチャの番号）
 * @note デスクリプタヒープを直接引く指定は新しい形式で書く（1.1 形式で直列化する）
 */
Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateBindlessRootSignature(ID3D12Device* device, const D3D12_STATIC_SAMPLER_DESC* samplers, UINT samplerCount) {
    constexpr UINT kTextureIndexRegister = 1; // テクスチャの番号を受け取る b1
    const D3D12_ROOT_PARAMETER1 parameters[] = {
        MakeVolatileCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
        MakeVolatileCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
        MakeSingleConstantParam(kTextureIndexRegister, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャの番号 (PS, b1)
    };
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc { };
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = _countof(parameters);
    desc.Desc_1_1.pParameters = parameters;
    desc.Desc_1_1.NumStaticSamplers = samplerCount;
    desc.Desc_1_1.pStaticSamplers = samplers;
    // シェーダーからデスクリプタヒープを番号で直接引けるようにする
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    if (FAILED(D3D12SerializeVersionedRootSignature(&desc, &signatureBlob, &errorBlob))) {
        if (errorBlob) {
            Logger::LogError(static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        return nullptr;
    }
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    if (FAILED(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) {
        return nullptr;
    }
    // 同じ設定のパイプラインを保存から見分けられるよう、中身から作った目印を付ける
    PipelineCache::TagRootSignature(rootSignature.Get(), signatureBlob.Get());
    return rootSignature;
}
} // namespace

/** @brief 2D描画共通設定の初期化PSOの生成やルートシグネチャの作成を行う */
void SpriteCommon::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // 対応している GPU では、テクスチャを記述子テーブルではなく番号で渡す（バインドレス）
    bindless_ = SupportsBindless(device);

    // スプライトは全ミップを使用する
    auto staticSamplers = ph::MakeDefaultSamplers(D3D12_FLOAT32_MAX);

    // バインドレス版のルートシグネチャが作れなかった時は、今までの記述子テーブルの形に戻す
    if (bindless_) {
        rootSignature_ = CreateBindlessRootSignature(device, staticSamplers.data(), static_cast<UINT>(staticSamplers.size()));
        bindless_ = rootSignature_ != nullptr;
    }
    if (!bindless_) {
        // スプライト専用シェーダーが使用するマテリアル・変換行列・テクスチャだけを宣言する
        D3D12_DESCRIPTOR_RANGE texRange = ph::MakeSrvRange(0); // t0

        D3D12_ROOT_PARAMETER rootParameters[3] = {
            ph::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL), // 0: マテリアル (PS, b0)
            ph::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_VERTEX), // 1: 変換行列 (VS, b0)
            ph::MakeSrvTableParam(&texRange, D3D12_SHADER_VISIBILITY_PIXEL), // 2: テクスチャ (PS, t0)
        };

        D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature { };
        descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        descriptionRootSignature.pParameters = rootParameters;
        descriptionRootSignature.NumParameters = _countof(rootParameters);
        descriptionRootSignature.pStaticSamplers = staticSamplers.data();
        descriptionRootSignature.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
        rootSignature_ = ph::CreateRootSignature(device, descriptionRootSignature);
    }
    ENGINE_ASSERT(rootSignature_);
    Logger::LogInfo(bindless_ ? "Sprite: バインドレスでテクスチャを渡す" : "Sprite: 記述子テーブルでテクスチャを渡す（GPU がバインドレスに未対応）");

    Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"Resources/shaders/object3d/Object3dVS.hlsl", L"vs_6_0");
    Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = bindless_
        ? dxCommon_->CompileShader(L"Resources/shaders/sprite/SpriteBindlessPS.hlsl", L"ps_6_6")
        : dxCommon_->CompileShader(L"Resources/shaders/sprite/SpritePS.hlsl", L"ps_6_0");

    // PSO の作成（アルファブレンド固定・深度テストなし）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc = ph::MakeDefault3dPsoDesc();
    graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
    graphicsPipelineStateDesc.InputLayout = { ph::kStandardInputLayout, _countof(ph::kStandardInputLayout) };
    graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
    graphicsPipelineStateDesc.BlendState.RenderTarget[0] = ph::MakeBlendDesc(BlendMode::kAlpha);
    graphicsPipelineStateDesc.DepthStencilState.DepthEnable = FALSE; // 2Dは常に最前面に描く

    // 保存した物があれば取り出し、無ければ作って保存する
    graphicsPipelineState_ = ph::CreateGraphicsPipeline(device, graphicsPipelineStateDesc);
    graphicsPipelineState_->SetName(L"SpriteCommon.SpritePS");
}

/** @brief 描画コマンドに2Dスプライト用の共通設定（PSO, ルートシグネチャ, プリミティブトポロジ）をセットする */
void SpriteCommon::CommonDrawSettings() {
    ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(PipelineCache::Resolve(graphicsPipelineState_.Get()));
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace engine::graphics

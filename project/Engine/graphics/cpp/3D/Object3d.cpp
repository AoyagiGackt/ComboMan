/**
 * @file Object3d.cpp
 * @brief 3Dオブジェクト（Object3d）の行列・マテリアルの更新と描画の実装
 */
#include "Object3d.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "Camera.h"
#include "FrustumCulling.h"
#include "LightManager.h"
#include "LightingMode.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3dCommon.h"
#include "OutlineEffect.h"
#include "ShadowManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <cmath>
#include <vector>

namespace engine::graphics {
namespace {

// マテリアルの初期値
constexpr engine::graphics::ObjectMaterialLayout kDefaultMaterial {};

/** @brief 画面外の判定で、モデルを囲む箱を広げる割合（シェーダーで頂点が少し動く物や丸め誤差で縁が欠けないようにする） */
constexpr float kCullingBoundsPadding = 0.1f;

/** @brief まとめ描きの見分けに使う 64bit FNV-1a の初期値と掛ける数 */
constexpr uint64_t kBatchKeyOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kBatchKeyPrime = 1099511628211ull;

/** @brief 変換行列だけを指定スロットへ束縛し、材質を伴わないパスでモデル形状を描画する */
void DrawGeometryWithTransform(Model* model, ModelCommon* common, D3D12_GPU_VIRTUAL_ADDRESS transform, UINT slot) {
    if (!model) {
        return;
    }
    ID3D12GraphicsCommandList* commandList = common->GetDxCommon()->GetCommandList();
    commandList->SetGraphicsRootConstantBufferView(slot, transform);
    model->DrawGeometryOnly(common);
}

}

using namespace Microsoft::WRL;

Camera* Object3d::sCommonCamera = nullptr;
Matrix4x4 Object3d::sCommonLightVP = MakeIdentity4x4();
Object3dCommon* Object3d::sCommonObjectCommon = nullptr;
ShadowManager* Object3d::sCommonShadowManager = nullptr;
bool Object3d::sFrustumCullingEnabled = true;
std::atomic<uint32_t> Object3d::sDrawnCount = 0;
std::atomic<uint32_t> Object3d::sCulledCount = 0;

/** @brief 数えた描画の数を取り出して 0 に戻す */
Object3d::CullingStats Object3d::TakeCullingStats() {
    CullingStats stats;
    stats.drawn = sDrawnCount.exchange(0);
    stats.culled = sCulledCount.exchange(0);
    return stats;
}

/** @brief 今の変換行列で、モデルを囲む箱が画面に映る範囲の外に完全に出ているか */
bool Object3d::IsOutsideView() const {
    // 箱を各軸の大きさの一定の割合だけ広げてから調べる（映るかもしれない物は描く側に倒す）
    return IsPaddedBoxOutsideClipSpace(transformationMatrix_.wvp,
        model_->GetLocalBoundsMin(), model_->GetLocalBoundsMax(), kCullingBoundsPadding);
}

/** @brief ワールド行列とライト空間の行列で、シャドウマップに映る範囲の外に完全に出ているか */
bool Object3d::IsOutsideShadowView() const {
    // 影のシェーダーと同じ行列（ワールド行列を掛けたライト空間の行列）で調べる
    return IsPaddedBoxOutsideClipSpace(transformationMatrix_.lightVP,
        model_->GetLocalBoundsMin(), model_->GetLocalBoundsMax(), kCullingBoundsPadding);
}

/** @brief 画面外の判定をして、描いた数と飛ばした数を数える */
bool Object3d::PassesViewCulling() {
    // 画面に映る範囲の外に完全に出ている物は、命令を積まずに飛ばす
    if (sFrustumCullingEnabled && IsOutsideView()) {
        ++sCulledCount;
        return false;
    }
    ++sDrawnCount;
    return true;
}

/** @brief マテリアル・座標変換・法線マップのルート引数を設定する */
void Object3d::BindObjectConstants(ID3D12GraphicsCommandList* commandList) {
    // マテリアルと座標変換を設定（描画ごとに別の領域へ写すので、同じフレームで値を変えて何度描いてもよい）
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kMaterial, UploadMaterial());
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kTransform, UploadTransform());
    BindNormalMap(commandList);
}

/** @brief スロット7 に法線マップを設定する */
void Object3d::BindNormalMap(ID3D12GraphicsCommandList* commandList) const {
    // 未設定時は diffuse をダミーとして流用する
    const std::string& normalPath = normalMapFilePath_.empty() ? model_->GetTextureFilePath() : normalMapFilePath_;
    commandList->SetGraphicsRootDescriptorTable(pipelineHelper::modelRootSlot::kNormalMap, TextureManager::GetInstance()->GetSrvHandleGPU(normalPath));
}

/** @brief ライト・シャドウマップのルート引数を再バインドする */
void Object3d::RebindCommonLighting(ID3D12GraphicsCommandList* cmd) {
    // OutlineEffect 等でルートシグネチャを切り替えた後、CommonDrawSettings() で
    // 通常のルートシグネチャに戻しただけではライト/シャドウマップのスロットは未初期化のまま
    // （ルートシグネチャの切り替えは全スロットの束縛を破棄するため）。ここで再バインドする
    pipelineHelper::BindCommonLighting(cmd, sCommonObjectCommon, sCommonShadowManager);
}

/** @brief 全てのObject3dで共通して使用するデフォルトカメラを設定する */
void Object3d::SetCommonCamera(Camera* camera) {
    sCommonCamera = camera;
}

/** @brief 全 Object3d で共通して使うライト空間行列を設定する（毎フレーム GamePlayScene から呼ぶ） */
void Object3d::SetLightViewProjection(const Matrix4x4& lvp) {
    sCommonLightVP = lvp;
}

/** @brief オブジェクトを初期化し、変換行列とマテリアルを既定値にする */
void Object3d::Initialize(ModelCommon* modelCommon) {
    modelCommon_ = modelCommon;
    // 更新はCPU側で行い、描画の直前にGPU用領域へ退避する。
    transformationMatrix_ = { MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4(), MakeIdentity4x4() };

    material_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
    material_.enableLighting = true;
    material_.shadingType = 1; // HalfLambert
    material_.useCubemap = 0;
    material_.useTexture = 1; // デフォルトはテクスチャあり
    material_.uvTransform = MakeIdentity4x4();
    material_.specularColor = { 1.0f, 1.0f, 1.0f }; // 白いハイライト
    material_.shininess = kDefaultMaterial.shininess; // ほどよい光沢
    material_.cameraWorldPos = { 0.0f, 0.0f, 0.0f };
    material_.envMapIntensity = 0.0f;
    material_.rimColor = { 1.0f, 1.0f, 1.0f };
    material_.rimPower = kDefaultMaterial.rimPower;
    material_.rimIntensity = 0.0f;
    material_.enableRim = 0;
    material_.useNormalMap = 0;
    material_.metallic = 0.0f;
    material_.roughness = 0.5f;
}

/** @brief 毎フレームの更新処理トランスフォームからワールド行列とWVP行列を計算する */
void Object3d::Update() {
    Matrix4x4 worldMatrix = useLocalMatrix_
        ? localMatrix_
        : MakeAffineMatrix(transform_.scale, transform_.rotate, transform_.translate);

    Matrix4x4 viewMatrix;
    Matrix4x4 projectionMatrix;

    // カメラがあればカメラから行列をもらう
    Camera* camera = camera_ ? camera_ : sCommonCamera;

    if (camera) {
        viewMatrix = camera->GetViewMatrix();
        projectionMatrix = camera->GetProjectionMatrix();
        // カメラのワールド座標をマテリアルに書き込む（スペキュラ計算に使用）
        material_.cameraWorldPos = camera->GetTranslate();
    }

    // WVP行列の合成
    Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

    // 定数バッファへ転送
    transformationMatrix_.wvp = worldViewProjectionMatrix;
    transformationMatrix_.world = worldMatrix;
    transformationMatrix_.worldInverseTranspose = Transpose(Inverse(worldMatrix));
    // シェーダーはモデル内の座標にそのまま掛けるので、ワールド行列を先に掛けておく
    transformationMatrix_.lightVP = Multiply(worldMatrix, sCommonLightVP);

    // 毎フレームライティングモードをマテリアルに反映させる（ロック済みオブジェクトは除外）
    if (!lockShadingType_) {
        material_.shadingType = LightManager::GetInstance()->GetLightingMode();
    }

    collider_.aabb.min = { transform_.translate.x - 0.5f, transform_.translate.y - 0.5f, transform_.translate.z - 0.5f };
    collider_.aabb.max = { transform_.translate.x + 0.5f, transform_.translate.y + 0.5f, transform_.translate.z + 0.5f };
}

/** @brief 現在の変換行列を、このフレームの描画用領域へコピーしてアドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS Object3d::UploadTransform() {
    return modelCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&transformationMatrix_, sizeof(transformationMatrix_));
}

/** @brief 現在のマテリアルを、このフレームの描画用領域へコピーしてアドレスを返す */
D3D12_GPU_VIRTUAL_ADDRESS Object3d::UploadMaterial() {
    return modelCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(&material_, sizeof(material_));
}

/** @brief シャドウパス用の描画（テクスチャなし・深度のみ） */
void Object3d::DrawShadow() {
    if (!model_) {
        return;
    }
    // シャドウマップに映る範囲の外の物は影を落とさないので飛ばす
    if (sFrustumCullingEnabled && IsOutsideShadowView()) {
        ++sCulledCount;
        return;
    }
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::shadowRootSlot::kTransform);
}

/** @brief WVP行列のみをバインドしてジオメトリだけを描画する（マテリアル/テクスチャなし） */
void Object3d::DrawForNormalCapture() {
    // NormalCapture RS  slot0 = VS b0 (TransformationMatrix) - DrawShadow と同じスロット配置
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::shadowRootSlot::kTransform);
}

/** @brief アウトラインを描く */
void Object3d::DrawOutline(OutlineEffect* effect) {
    if (!model_ || !effect) {
        return;
    }
    DrawGeometryWithTransform(model_, modelCommon_, UploadTransform(), pipelineHelper::outlineRootSlot::kTransform);
}

/** @brief ファイルパスを指定して、ModelManagerからモデルを検索してセットする */
void Object3d::SetModel(const std::string& filePath) {
    // マネージャーからモデルを検索してセット
    model_ = ModelManager::GetInstance()->FindModel(filePath);
}

/** @brief 法線マップテクスチャを読み込んで設定し、useNormalMapフラグを有効にする */
void Object3d::SetNormalMap(const std::string& filePath) {
    normalMapFilePath_ = filePath;
    TextureManager::GetInstance()->LoadTexture(filePath);
    material_.useNormalMap = 1;
}

/** @brief オブジェクトを描画するコマンドを積む */
void Object3d::Draw() {
    if (!model_) {
        return;
    }
    // 画面外の物は飛ばす（影のパスは DrawShadow でシャドウマップの範囲を別に調べる）
    if (!PassesViewCulling()) {
        return;
    }

    // ModelCommonからコマンドリストを取得
    ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

    // Spriteやエフェクトがルートシグネチャを切り替えた後でも、通常3D描画が
    // 使用する平行光源・ポイントライト・シャドウマップを描画直前に保証する
    RebindCommonLighting(commandList);

    BindObjectConstants(commandList);
    model_->Draw(modelCommon_);
}

/** @brief 並べ替えた順に続けて描く。直前と同じモデルなら頂点とテクスチャを設定し直さない */
bool Object3d::DrawBatched(const Model*& boundModel) {
    if (!model_ || !PassesViewCulling()) {
        return false;
    }
    DrawPrepared(modelCommon_->GetDxCommon()->GetCommandList(), boundModel);
    return true;
}

/** @brief 定数を設定し、モデルが変わった時だけ頂点とテクスチャを設定してから1体描く */
void Object3d::DrawPrepared(ID3D12GraphicsCommandList* commandList, const Model*& boundModel) {
    BindObjectConstants(commandList);
    // モデルが変わった時だけ、頂点・添字のバッファとテクスチャを設定する
    if (boundModel != model_) {
        model_->BindForDraw(modelCommon_);
        boundModel = model_;
    }
    model_->DrawIndexed(modelCommon_);
}

/** @brief モデルの共通設定・マテリアルのバイト列・法線マップの名前から、64bit の見分けを作る */
uint64_t Object3d::ComputeBatchKey() const {
    // 並びの中身をそのまま混ぜる（マテリアルは 4 バイトの値だけで詰め物が無いので、バイトで比べてよい）
    uint64_t hash = kBatchKeyOffsetBasis;
    const auto mix = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kBatchKeyPrime;
        }
    };
    const ModelCommon* common = modelCommon_;
    mix(&common, sizeof(common));
    mix(&material_, sizeof(material_));
    mix(normalMapFilePath_.data(), normalMapFilePath_.size());
    return hash;
}

/** @brief 画面内の物の変換行列を並べ、2本目の頂点バッファとして渡して1回で描く */
uint32_t Object3d::DrawInstancedGroup(Object3d* const* objects, size_t count, const Model*& boundModel) {
    // 1体ごとの変換行列を並べる場所（スレッドごとに持ち、確保を使い回す）
    thread_local std::vector<TransformationMatrix> sInstanceTransforms;
    sInstanceTransforms.clear();
    Object3d* first = nullptr;
    for (size_t i = 0; i < count; ++i) {
        Object3d* object = objects[i];
        if (!object->model_ || !object->PassesViewCulling()) {
            continue;
        }
        if (first == nullptr) {
            first = object;
        }
        sInstanceTransforms.push_back(object->transformationMatrix_);
    }
    if (first == nullptr) {
        return 0;
    }
    ID3D12GraphicsCommandList* commandList = first->modelCommon_->GetDxCommon()->GetCommandList();
    // 画面内に1体しか残らなければ、いつもの描き方の方が手間が少ない
    if (sInstanceTransforms.size() == 1) {
        first->DrawPrepared(commandList, boundModel);
        return 1;
    }

    // マテリアルと法線マップは組の最初の物を使う（組は同じ中身の物だけなので、どれを使っても同じ）
    commandList->SetGraphicsRootConstantBufferView(pipelineHelper::modelRootSlot::kMaterial, first->UploadMaterial());
    first->BindNormalMap(commandList);
    if (boundModel != first->model_) {
        first->model_->BindForDraw(first->modelCommon_);
        boundModel = first->model_;
    }

    // 変換行列の並びを、このフレームの領域へ写して頂点バッファにする
    const UINT stride = static_cast<UINT>(sizeof(TransformationMatrix));
    const UINT instanceCount = static_cast<UINT>(sInstanceTransforms.size());
    D3D12_VERTEX_BUFFER_VIEW instanceView {};
    instanceView.BufferLocation = first->modelCommon_->GetDxCommon()->GetFrameUploadAllocator().Allocate(
        sInstanceTransforms.data(), static_cast<size_t>(stride) * instanceCount);
    instanceView.SizeInBytes = stride * instanceCount;
    instanceView.StrideInBytes = stride;
    commandList->IASetVertexBuffers(pipelineHelper::kInstanceInputSlot, 1, &instanceView);

    // まとめ描き用のパイプラインで描き、通常のパイプラインに戻す
    first->modelCommon_->BeginInstancedDraw();
    first->model_->DrawIndexed(first->modelCommon_, instanceCount);
    first->modelCommon_->EndInstancedDraw();
    return instanceCount;
}

} // namespace engine::graphics

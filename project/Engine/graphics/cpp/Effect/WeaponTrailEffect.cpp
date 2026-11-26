/**
 * @file WeaponTrailEffect.cpp
 * @brief WeaponTrailEffectの軌跡サンプルの管理、Catmull-Rom補間による帯の生成、描画を実装するファイル
 */
#include "WeaponTrailEffect.h"
#include "PipelineHelper.h"
#include "GraphicsTuning.h"
#include "Camera.h"
#include "EngineAssert.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <algorithm>
#include <array>
#include <iterator>

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace {

constexpr float kMinLifetime = 0.001f; // 寿命0で割らないための下限
constexpr const char* kWhiteTextureName = "weapon_trail_white"; // 筋の画像を指定しない時に使う白一色の画像の名前
constexpr uint32_t kWhiteTextureSize = 1; // 白一色の画像の幅と高さ
constexpr uint8_t kWhiteTexel = 255;
constexpr UINT kTrailRootScene = 0; // ルートの並び: 定数
constexpr UINT kTrailRootTexture = 1; // ルートの並び: 筋の画像

/** @brief Catmull-Rom曲線の項の数（t^0〜t^3 の4次数、制御点も4つ） */
constexpr int kCatmullRomTerms = 4;
/** @brief Catmull-Rom曲線の基底行列（行が t^0〜t^3 の係数、列が p0〜p3 にかかる重み） */
constexpr float kCatmullRomBasis[kCatmullRomTerms][kCatmullRomTerms] = {
    { 0.0f, 2.0f, 0.0f, 0.0f },
    { -1.0f, 0.0f, 1.0f, 0.0f },
    { 2.0f, -5.0f, 4.0f, -1.0f },
    { -1.0f, 3.0f, -3.0f, 1.0f },
};
/** @brief 基底行列で求めた多項式に掛ける全体の係数（張力 0.5 の Catmull-Rom） */
constexpr float kCatmullRomScale = 0.5f;

/** @brief Catmull-Rom曲線上の点を返す（p1→p2間をtで補間し、p0/p3は接線を決める前後の点） */
Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
    const float powers[kCatmullRomTerms] = { 1.0f, t, t * t, t * t * t };
    // 1軸ぶんの4点の値から、t の各次数の係数を基底行列で求めて足し合わせる
    auto axis = [&powers](float a0, float a1, float a2, float a3) {
        float sum = 0.0f;
        for (int degree = 0; degree < kCatmullRomTerms; ++degree) {
            const float* w = kCatmullRomBasis[degree];
            sum += (w[0] * a0 + w[1] * a1 + w[2] * a2 + w[3] * a3) * powers[degree];
        }
        return kCatmullRomScale * sum;
    };
    return { axis(p0.x, p1.x, p2.x, p3.x), axis(p0.y, p1.y, p2.y, p3.y), axis(p0.z, p1.z, p2.z, p3.z) };
}

} // namespace

/** @brief 武器の軌跡の描画資源を用意する */
void WeaponTrailEffect::Initialize(DirectXCommon* dxCommon) {
    ENGINE_ASSERT(dxCommon);
    dxCommon_ = dxCommon;
    style_.tipColor = GraphicsTuning::Color("weaponTrail.tipColor", style_.tipColor);
    style_.baseColor = GraphicsTuning::Color("weaponTrail.baseColor", style_.baseColor);
    style_.lifetime = GraphicsTuning::Float("weaponTrail.lifetime", style_.lifetime, kMinLifetime);
    style_.subdivisions = GraphicsTuning::Int("weaponTrail.subdivisions", style_.subdivisions, 1, kMaxSubdivisions);

    sceneCB_.viewProj = MakeIdentity4x4();
    sceneCB_.intensity = 1.0f;
    vertices_.resize(kMaxVertices);

    // 筋の画像を指定しない帯でも同じパイプラインで描けるよう、白一色の画像を用意しておく
    TextureManager* textures = TextureManager::GetInstance();
    if (!textures->HasTexture(kWhiteTextureName)) {
        const std::array<uint8_t, 4> white = { kWhiteTexel, kWhiteTexel, kWhiteTexel, kWhiteTexel };
        textures->LoadFromRawRGBA8(kWhiteTextureName, white.data(), kWhiteTextureSize, kWhiteTextureSize);
        textures->FlushUploads();
    }

    CreatePipeline();
}

/** @brief 今フレームの刃の位置を積む */
void WeaponTrailEffect::AddSample(const Vector3& base, const Vector3& tip) {
    if (static_cast<int>(samples_.size()) >= kMaxSamples) {
        samples_.pop_front();
    }
    samples_.push_back({ base, tip, 0.0f, strokeId_, clock_ });
}

/** @brief 軌跡の見た目の設定を変える */
void WeaponTrailEffect::SetStyle(const WeaponTrailStyle& style) {
    if (style.texture != style_.texture && !style.texture.empty()) {
        PreloadTexture(style.texture);
    }
    style_ = style;
}

/** @brief 筋の画像を先に読んでおく（シーンの初期化で呼ぶ） */
void WeaponTrailEffect::PreloadTexture(const std::string& texture) {
    TextureManager* textures = TextureManager::GetInstance();
    if (texture.empty() || textures->HasTexture(texture)) {
        return;
    }
    textures->LoadTexture(texture);
    textures->FlushUploads();
}

/** @brief 今の設定の筋の画像（無ければ白一色の画像）を返す */
const std::string& WeaponTrailEffect::CurrentTexture() const {
    static const std::string kWhite = kWhiteTextureName;
    return style_.texture.empty() ? kWhite : style_.texture;
}

/** @brief 寿命を進めて頂点バッファを組み直す */
void WeaponTrailEffect::Update(float dt, Camera* camera) {
    if (camera != nullptr) {
        sceneCB_.viewProj = Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());
    }

    clock_ += dt;
    for (auto& s : samples_) {
        s.age += dt;
    }
    // 筋の画像は軌跡に沿って流し、明るさの倍率はそのまま渡す
    sceneCB_.intensity = style_.intensity;
    sceneCB_.uOffset = clock_ * style_.textureScroll;
    while (!samples_.empty() && samples_.front().age >= style_.lifetime) {
        samples_.pop_front();
    }

    RebuildVertices();
}

/** @brief 補間位置1点ぶんの頂点色を作る */
Vector4 WeaponTrailEffect::MakeColor(const Vector4& color, float age) const {
    // 生まれた直後が最も濃く、寿命に向かって二乗で抜ける（尾がすっと消える）
    const float life = (std::max)(style_.lifetime, kMinLifetime);
    float fade = std::clamp(1.0f - age / life, 0.0f, 1.0f);
    fade *= fade;
    return { color.x, color.y, color.z, color.w * fade };
}

/** @brief サンプル列から帯の三角形を組み立てる */
void WeaponTrailEffect::RebuildVertices() {
    // 頂点を作り直す（分割数は上限で丸める）
    vertexCount_ = 0;
    const int count = static_cast<int>(samples_.size());
    const int subdiv = std::clamp(style_.subdivisions, 1, kMaxSubdivisions);

    // 筋の画像の横の位置は、その点が積まれた時刻から決める（帯が伸びても、描かれた筋はその場に残る）
    const float life = (std::max)(style_.lifetime, kMinLifetime);
    const float uPerSecond = style_.textureRepeat / life;
    constexpr float kBaseV = 0.0f;
    constexpr float kTipV = 1.0f;
    auto pushVertex = [this](const Vector3& p, const Vector4& c, float u, float v) {
        vertices_[vertexCount_++] = { { p.x, p.y, p.z, 1.0f }, c, { u, v } };
    };

    // 隣り合う2つの記録点の間を、同じ振りの中だけ帯でつなぐ
    for (int i = 0; i + 1 < count; ++i) {
        const Sample& a = samples_[i];
        const Sample& b = samples_[i + 1];
        if (a.stroke != b.stroke) {
            continue;
        }
        // 帯の端では前後の点が無いので端点自身を使う（端で曲線が外へはみ出さない）
        const Sample& prev = (i > 0 && samples_[i - 1].stroke == a.stroke) ? samples_[i - 1] : a;
        const Sample& next = (i + 2 < count && samples_[i + 2].stroke == a.stroke) ? samples_[i + 2] : b;

        for (int s = 0; s < subdiv; ++s) {
            if (vertexCount_ + kVertsPerQuad > kMaxVertices) {
                return;
            }
            // 区間を分割数ぶんに切り、根元と刃先をそれぞれ曲線で補間する
            const float t0 = static_cast<float>(s) / static_cast<float>(subdiv);
            const float t1 = static_cast<float>(s + 1) / static_cast<float>(subdiv);

            const Vector3 base0 = CatmullRom(prev.base, a.base, b.base, next.base, t0);
            const Vector3 base1 = CatmullRom(prev.base, a.base, b.base, next.base, t1);
            const Vector3 tip0 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t0);
            const Vector3 tip1 = CatmullRom(prev.tip, a.tip, b.tip, next.tip, t1);
            const float age0 = a.age + (b.age - a.age) * t0;
            const float age1 = a.age + (b.age - a.age) * t1;
            const float u0 = (a.bornTime + (b.bornTime - a.bornTime) * t0) * uPerSecond;
            const float u1 = (a.bornTime + (b.bornTime - a.bornTime) * t1) * uPerSecond;

            // 古い点ほど薄くする色
            const Vector4 baseColor0 = MakeColor(style_.baseColor, age0);
            const Vector4 baseColor1 = MakeColor(style_.baseColor, age1);
            const Vector4 tipColor0 = MakeColor(style_.tipColor, age0);
            const Vector4 tipColor1 = MakeColor(style_.tipColor, age1);

            // 四角形1つを三角形2つで作る
            pushVertex(base0, baseColor0, u0, kBaseV);
            pushVertex(tip0, tipColor0, u0, kTipV);
            pushVertex(tip1, tipColor1, u1, kTipV);
            pushVertex(base0, baseColor0, u0, kBaseV);
            pushVertex(tip1, tipColor1, u1, kTipV);
            pushVertex(base1, baseColor1, u1, kBaseV);
        }
    }
}

/** @brief 最新のサンプルから指定秒数さかのぼった間に刃先が動いた量を返す */
bool WeaponTrailEffect::GetRecentTipMotion(float window, Vector3& outMotion) const {
    if (samples_.size() < 2) {
        return false;
    }
    const Sample& newest = samples_.back();
    const Sample* oldest = nullptr;
    for (auto it = samples_.rbegin() + 1; it != samples_.rend(); ++it) {
        if (it->stroke != newest.stroke || it->age - newest.age > window) {
            break;
        }
        oldest = &(*it);
    }
    if (oldest == nullptr) {
        return false;
    }
    outMotion = { newest.tip.x - oldest->tip.x, newest.tip.y - oldest->tip.y, newest.tip.z - oldest->tip.z };
    return true;
}

/** @brief 帯の描画コマンドを積む（3D描画パス内で呼ぶ） */
void WeaponTrailEffect::Draw() {
    if (vertexCount_ == 0) {
        return;
    }

    // このフレーム用の領域へ頂点と定数を写して、その場所から描く
    FrameUploadAllocator& upload = dxCommon_->GetFrameUploadAllocator();
    const UINT bytes = static_cast<UINT>(sizeof(TrailVertex) * vertexCount_);
    D3D12_VERTEX_BUFFER_VIEW vbv { };
    vbv.BufferLocation = upload.Allocate(vertices_.data(), bytes);
    vbv.SizeInBytes = bytes;
    vbv.StrideInBytes = sizeof(TrailVertex);
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    SrvManager* srvManager = SrvManager::GetInstance();
    ID3D12DescriptorHeap* heaps[] = { srvManager->GetSrvDescriptorHeap() };
    cmd->SetDescriptorHeaps(1, heaps);
    pipelineHelper::SetTrianglePipeline(cmd, rootSignature_.Get(), pipelineState_.Get());
    cmd->IASetVertexBuffers(0, 1, &vbv);
    cmd->SetGraphicsRootConstantBufferView(kTrailRootScene, upload.Allocate(&sceneCB_, sizeof(SceneCB)));
    cmd->SetGraphicsRootDescriptorTable(kTrailRootTexture, TextureManager::GetInstance()->GetSrvHandleGPU(CurrentTexture()));
    cmd->DrawInstanced(vertexCount_, 1, 0, 0);
}

/** @brief 武器の軌跡用のパイプラインを生成する（定数と筋の画像を読み、深度は書かずに加算で描く） */
void WeaponTrailEffect::CreatePipeline() {
    const auto range = pipelineHelper::MakeSrvRange(0);
    const D3D12_ROOT_PARAMETER parameters[] = {
        pipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_ALL),
        pipelineHelper::MakeSrvTableParam(&range, D3D12_SHADER_VISIBILITY_PIXEL),
    };
    // 横（軌跡の長さ方向）は繰り返し、縦（根元〜刃先）は端で止める
    const D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    rootSignature_ = pipelineHelper::CreateRootSignature(dxCommon_->GetDevice(), parameters, static_cast<UINT>(std::size(parameters)),
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);
    constexpr D3D12_INPUT_ELEMENT_DESC kInputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    auto desc = pipelineHelper::MakeEffectMeshPsoDesc(BlendMode::kAdd, D3D12_DEPTH_WRITE_MASK_ZERO);
    desc.pRootSignature = rootSignature_.Get();
    desc.InputLayout = { kInputLayout, static_cast<UINT>(std::size(kInputLayout)) };
    pipelineState_ = pipelineHelper::CreateGraphicsPipeline(dxCommon_, desc,
        L"Resources/shaders/weapontrail/WeaponTrail.VS.hlsl", L"Resources/shaders/weapontrail/WeaponTrail.PS.hlsl");
}

} // namespace engine::graphics

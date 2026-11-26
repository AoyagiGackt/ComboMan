/**
 * @file WeaponTrailEffect.h
 * @brief 武器の根元と刃先の軌跡を帯ポリゴンとして加算描画するファイル
 */
#pragma once
#include "DirectXCommon.h"
#include "MakeAffine.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace engine::graphics {

class Camera;

/** @brief 軌跡の見た目（色・寿命・補間の細かさ・筋の画像） */
struct WeaponTrailStyle {
    Vector4 tipColor = { 1.0f, 1.0f, 1.0f, 0.9f }; ///< 刃先側の色（アルファは最大不透明度）
    Vector4 baseColor = { 0.4f, 0.7f, 1.0f, 0.0f }; ///< 根元側の色（アルファ0で根元へ向かって透ける）
    float lifetime = 0.16f; ///< 1サンプルが消えるまでの秒数
    int subdivisions = 6; ///< サンプル間をCatmull-Rom補間で何分割するか（1で補間なし）
    std::string texture; ///< 筋の画像（白いほど濃い。横=軌跡の長さ方向、縦=根元から刃先）。空なら一様な帯
    float intensity = 1.0f; ///< 明るさの倍率（1を超えると芯が白く飛び、ブルームで光る）
    float textureRepeat = 1.0f; ///< 寿命ぶんの長さに筋の画像を何回並べるか
    float textureScroll = 0.0f; ///< 筋の画像を軌跡に沿って流す速さ（1秒に画像の何枚ぶん）
};

/**
 * @brief 武器軌跡の帯描画
 * @note 毎フレーム刃の根元/先端をAddSample()で積み、サンプル間をCatmull-Rom曲線で分割して帯を作る。
 *       60fpsで速い振りをサンプルすると折れ線になるため、補間でフレーム間の弧を復元する。
 *       BreakStroke()で次のサンプルと前のサンプルを繋がなくする（段の切れ目用）。
 *       帯には UV を持たせ、筋の画像を掛けて武器ごとの質感（炎・稲妻・氷など）を出す
 */
class WeaponTrailEffect {
public:
    void Initialize(engine::DirectXCommon* dxCommon);

    /**
     * @brief 今フレームの刃の位置を積む
     * @param base 刃の根元（ワールド座標）
     * @param tip  刃の先端（ワールド座標）
     */
    void AddSample(const Vector3& base, const Vector3& tip);

    /** @brief 振りの区切り 次に積むサンプルを新しい帯の始点にする */
    void BreakStroke() {
        ++strokeId_;
    }

    /**
     * @brief 寿命を進めて頂点バッファを組み直す
     * @param dt     経過秒数（ヒットストップ中に0を渡すと軌跡もその場で止まる）
     * @param camera ビュープロジェクション行列の取得元
     */
    void Update(float dt, Camera* camera);

    /** @brief 帯の描画コマンドを積む（3D描画パス内で呼ぶ） */
    void Draw();

    /** @brief 全サンプルを消す */
    void Clear() {
        samples_.clear();
        vertexCount_ = 0;
    }

    /**
     * @brief 軌跡の見た目の設定を変える
     * @note 筋の画像はまだ読んでいなければここで読む（毎フレーム呼んでよいが、初めての画像は PreloadTexture で先に読んでおくと途中で止まらない）
     */
    void SetStyle(const WeaponTrailStyle& style);
    /** @brief 軌跡の見た目の設定を返す */
    const WeaponTrailStyle& GetStyle() const {
        return style_;
    }

    /** @brief 筋の画像を先に読んでおく（シーンの初期化で呼ぶ） */
    static void PreloadTexture(const std::string& texture);

    /**
     * @brief 最新のサンプルから指定秒数さかのぼった間に刃先が動いた量を返す
     * @param window さかのぼる秒数
     * @param outMotion 刃先の移動量（最新 - 過去）
     * @return 同じ振りの中に比較できるサンプルがあればtrue
     */
    bool GetRecentTipMotion(float window, Vector3& outMotion) const;

    /** @brief 頂点数を返す */
    uint32_t GetVertexCount() const {
        return vertexCount_;
    }
    /** @brief 軌跡のサンプル数を返す */
    int GetSampleCount() const {
        return static_cast<int>(samples_.size());
    }

private:
    static constexpr int kMaxSamples = 64;
    static constexpr int kMaxSubdivisions = 12;
    static constexpr int kVertsPerQuad = 6;
    static constexpr uint32_t kMaxVertices = kMaxSamples * kMaxSubdivisions * kVertsPerQuad;

    /** @brief 頂点（WeaponTrail.VS.hlsl の入力レイアウトと一致させること） */
    struct TrailVertex {
        Vector4 position;
        Vector4 color;
        Vector2 uv; ///< u: 軌跡の長さ方向（生まれた時刻から決まるので、帯が伸びても筋は流れない） v: 根元0〜刃先1
    };

    /** @brief cbuffer のメモリレイアウト（HLSL の TrailParams と一致させること） */
    struct SceneCB {
        Matrix4x4 viewProj;
        float intensity; ///< 明るさの倍率
        float uOffset; ///< 筋の画像を流すずれ
        float pad[2];
    };

    /** @brief 1フレーム分の刃の位置 */
    struct Sample {
        Vector3 base;
        Vector3 tip;
        float age = 0.0f;
        uint32_t stroke = 0;
        float bornTime = 0.0f; ///< 積んだ時の経過時間（筋の画像の横の位置に使う）
    };

    void CreatePipeline();
    /** @brief サンプル列から帯の三角形を組み立てる */
    void RebuildVertices();
    /** @brief 補間位置1点ぶんの頂点色を作る */
    Vector4 MakeColor(const Vector4& color, float age) const;
    /** @brief 今の設定の筋の画像（無ければ白一色の画像）を返す */
    const std::string& CurrentTexture() const;

    engine::DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // 頂点と定数は CPU 側で組み、描く時にこのフレーム用のアップロード領域へ写す
    // （Map したままの1本を毎フレーム書き換えると、GPU が前のフレームを読んでいる間に上書きする恐れがあるため）
    std::vector<TrailVertex> vertices_; ///< 組み立てた頂点（先頭から vertexCount_ 個が有効）
    uint32_t vertexCount_ = 0;
    SceneCB sceneCB_ { }; ///< ビュー射影行列と見た目の値

    std::deque<Sample> samples_; ///< 古い順（末尾が最新）
    uint32_t strokeId_ = 0;
    float clock_ = 0.0f; ///< 軌跡が進んだ時間の合計（ヒットストップ中は進まない）
    WeaponTrailStyle style_;
};

} // namespace engine::graphics

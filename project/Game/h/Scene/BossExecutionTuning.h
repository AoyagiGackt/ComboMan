/**
 * @file BossExecutionTuning.h
 * @brief 倒したボスが膝をつく姿勢と、武器を奪う時のとどめの流れの調整値（Resources/Config/boss_execution.json）
 */
#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::game {

/** @brief 膝をつかせる時に1本のボーンを向ける先 */
struct KneelReach {
    std::string bone; ///< ボーン名
    Vector3 direction {}; ///< モデルの座標での向き（+Z が前、+Y が上。長さは問わない）
    float weight = 1.0f; ///< 目標の向きへ寄せる割合（0で元の姿勢、1で目標の向き）
};

/** @brief 倒したボスが武器を地面に突き立てて膝をつく姿勢の調整値 */
struct BossKneelTuning {
    float sink = 0.33f; ///< 体を沈める量（膝が床に付く高さまで）
    float bodyLean = 0.15f; ///< 体の前への傾き（ラジアン）
    float breathAmount = 0.015f; ///< 肩で息をする上下の幅
    float breathSpeed = 2.2f; ///< 肩で息をする速さ（ラジアン毎秒）
    float animSpeed = 0.25f; ///< 待機アニメを流す速さ（0で完全に止める）
    Vector3 weaponOffset = { 0.6f, 0.55f, 0.0f }; ///< 突き立てた武器の位置（X は向いている方、Y は足元からの高さ）
    float weaponTilt = 3.1416f; ///< 突き立てた武器の傾き（ラジアン。π で切っ先が下）
    Vector4 weaponDimColor = { 0.75f, 0.85f, 1.0f, 1.0f }; ///< 武器の明滅の暗い側（明るい側は白）
    float weaponPulseSpeed = 3.0f; ///< 武器が明滅する速さ（ラジアン毎秒）
    std::vector<KneelReach> reaches; ///< 曲げるボーンの一覧（親から子の順に並べる）
};

/**
 * @brief とどめの流れの1拍（前の拍から delay 秒たった時に、書いてある事をまとめて行う）
 * @note 位置はボスの足元を原点にした座標で、X の正はプレイヤーが最初にいた側、Y は上
 */
struct ExecutionBeat {
    float delay = 0.0f; ///< 前の拍からの待ち（秒）
    bool afterMove = false; ///< 前の移動が終わってから待ち始めるか
    bool move = false; ///< moveTo へ動くか
    Vector3 moveTo {}; ///< 動く先（ボスの足元からの位置）
    float moveSeconds = 0.0f; ///< 動くのにかける秒数（0 で一瞬で移る）
    float walkSpeed = 0.0f; ///< 0 より大きければ、この速さで歩いて動く（秒数は距離から決める）
    std::string face; ///< 向き（boss=ボスの方、away=ボスに背を向ける、空=そのまま）
    std::string pose; ///< 取る技の姿勢（g0〜=地上の段、glast=地上の最後、a0〜=空中の段、alast=空中の最後、launch=打ち上げ、s0〜=射撃の段、slast=射撃の最後、none=構えを解く）
    float poseSeconds = 0.3f; ///< 技の姿勢を最後まで動かす秒数（射撃の段は、撃たない拍なら構え切るまで、撃つ拍なら反動から構え直すまで）
    bool shot = false; ///< 銃を撃つか（ボスの体の真ん中へ弾を飛ばし、射撃の姿勢を発砲の瞬間から動かす。pose が空なら今の射撃の段のまま撃つ）
    bool grab = false; ///< ボスが突き立てた武器を抜き取って手に持つか
    float slashAngle = 0.0f; ///< 斬撃線の角度（度。0 が水平、プレイヤーのいる側へ向けて反転する）
    float slashLength = 0.0f; ///< 斬撃線の半分の長さ（0 なら出さない）
    Vector3 slashOffset {}; ///< 斬撃線の中心のずれ（ボスの体の真ん中から）
    bool screenSlash = false; ///< 画面を横切る細い線も引くか
    float screenSlashLength = 6.0f; ///< 画面を横切る線の半分の長さ
    int hitStop = 0; ///< ヒットストップのフレーム数
    float shake = 0.0f; ///< 画面の揺れの強さ
    float shakeSeconds = 0.0f; ///< 画面の揺れの長さ
    std::string sparks; ///< ボスの体の真ん中に出す火花（hit_effects.json の sparks）
    std::string ring; ///< ボスの体の真ん中に出す輪。rings の項目の名前
    std::string flash; ///< 画面の閃光。flashes の項目の名前
    std::string impact; ///< 揺れと止めの組。impacts の項目の名前
    std::string playerSparks; ///< プレイヤーの位置に出す火花（一瞬で移る時は移る前と後の両方）
    std::string sound; ///< sounds.json の名前
    std::string cue; ///< 決まった効果音（beat / release / glass / swing / shot）
    float slowSeconds = 0.0f; ///< スローにする秒数
    float slowScale = 1.0f; ///< スローの速さの倍率
    float zoom = 0.0f; ///< カメラの寄り（0 より大きければこの値へ寄せる。1 が通常、小さいほど寄る）
    bool slice = false; ///< ボスを切り分けて崩す
    bool bomb = false; ///< ボスにめり込んだ爆弾をここで爆発させる（爆弾のとどめの型で使う）
};

/** @brief とどめの型1つ（拍を順に進め、最後の拍から endHold 秒で終わる） */
struct ExecutionStyle {
    float endHold = 1.0f; ///< 最後の拍の後に余韻を見せる秒数
    std::vector<ExecutionBeat> beats; ///< 拍の並び（共通の歩み寄りの後に続く）
};

/** @brief 膝をつく姿勢と、とどめの流れの調整値一式 */
struct BossExecutionTuning {
    BossKneelTuning kneel; ///< 膝をつく姿勢
    float bossCenterHeight = 0.8f; ///< ボスの体の真ん中の、足元からの高さ
    float cameraHeight = 0.6f; ///< カメラが見る位置の、足元からの高さ
    float zoomRate = 3.0f; ///< カメラの寄りを目標へ近づける速さ（毎秒）
    Vector4 slashMarkColor = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 画面を横切る線の色
    float slashMarkThickness = 3.0f; ///< 画面を横切る線の太さ（ピクセル）
    float slashMarkSeconds = 0.6f; ///< 画面を横切る線が残る秒数
    std::string defaultStyle; ///< ステージに割り当てが無い時の型
    std::vector<ExecutionBeat> intro; ///< どの型でも最初に行う拍（歩み寄って武器を抜き取る）
    std::unordered_map<std::string, std::string> stages; ///< ステージのファイル名（拡張子なし）→ 型の名前
    std::string gunDefaultStyle; ///< 銃で倒した時の、ステージに割り当てが無い時の型
    std::vector<ExecutionBeat> gunIntro; ///< 銃で倒した時に最初に行う拍（歩み寄って見下ろす。武器は抜き取らない）
    std::unordered_map<std::string, std::string> gunStages; ///< 銃で倒した時の、ステージのファイル名（拡張子なし）→ 型の名前
    std::string bombStyle; ///< ボスに爆弾がめり込んだまま倒した時の隠しの型（空か見つからなければ使わない）
    std::vector<ExecutionBeat> bombIntro; ///< 爆弾の型で最初に行う拍（武器は抜き取らずに歩み寄る）
    std::unordered_map<std::string, ExecutionStyle> styles; ///< 型の名前 → 型

    /** @brief ステージのレベルのパスに割り当てた型を返す（無ければ既定の型、それも無ければ nullptr） */
    const ExecutionStyle* FindStyle(const std::string& levelPath) const;
    /** @brief 銃で倒した時の型を返す（ステージの割り当てが無ければ銃の既定の型、それも無ければ nullptr） */
    const ExecutionStyle* FindGunStyle(const std::string& levelPath) const;
    /** @brief ボスに爆弾がめり込んだまま倒した時の型を返す（無ければ nullptr） */
    const ExecutionStyle* FindBombStyle() const;
};

/** @brief boss_execution.json の読み込み済みの値を持つ置き場 */
class BossExecutionTuningStore {
public:
    /** @brief 読み込み済みの値を返す（初回に読み込む） */
    static const BossExecutionTuning& Get();
    /** @brief ファイルから読み直す */
    static void Reload();

private:
    /** @brief 値の実体 */
    static BossExecutionTuning& Mutable();
};

} // namespace engine::game

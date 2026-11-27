/**
 * @file PostProcessStack.h
 * @brief 画面全体へ掛けるエフェクトを、設定ファイルで決めた順番に掛けるクラスのファイル
 */
#pragma once
#include "json.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace engine::graphics {
class SrvManager;

/**
 * @brief 3D を描き終えた後・UI の前に、画面全体へ掛けるエフェクトを順に掛ける列
 * @note エフェクトは名前を付けて Register で登録し、掛ける順番と止める物は
 *       Resources/Config/post_process.json の stacks.<列の名前> で決める（順番をコードに書かない）。
 *       1つ掛けるごとに、渡された復帰処理で描画先とパイプラインを戻す（自前の PSO を使うエフェクトの後に
 *       戻し忘れると、次の描画が違うルートシグネチャで走って落ちるため）
 */
class PostProcessStack {
public:
    /** @brief エフェクトを今掛けるかを返す関数 */
    using ActiveFunction = std::function<bool()>;
    /** @brief エフェクトを掛ける関数 */
    using ApplyFunction = std::function<void()>;

    /** @brief エンジンが持つエフェクトの名前（設定ファイルの order に書く名前） */
    static constexpr const char* kBloomName = "bloom";
    static constexpr const char* kToneMapName = "toneMap";

    /**
     * @brief 列の名前を決めて、設定ファイルから順番を読む
     * @param stackName 設定ファイルの stacks の下の名前（場面ごとに分ける）
     */
    void Initialize(const std::string& stackName);

    /**
     * @brief エフェクトを登録する（同じ名前をもう一度登録すると差し替える）
     * @param name 設定ファイルの order に書く名前
     * @param isActive 今掛けるかを返す関数（false の時は飛ばす）
     * @param apply 掛ける関数
     */
    void Register(const std::string& name, ActiveFunction isActive, ApplyFunction apply);

    /**
     * @brief エンジンが持つエフェクト（ブルーム・トーンマップ）を登録する
     * @param srvManager ブルームが使うデスクリプタの管理
     */
    void RegisterBuiltins(SrvManager* srvManager);

    /**
     * @brief 決めた順番で、今掛けるエフェクトを順に掛ける
     * @param restoreTarget 1つ掛けるごとに呼ぶ、描画先とパイプラインを戻す処理
     */
    void Execute(const std::function<void()>& restoreTarget);

    /** @brief 設定ファイルを読み直す（エディタで順番を変えた後に呼ぶ） */
    void ReloadOrder();

    /** @brief 掛ける順番の名前の並び（止めた物も含む。設定に無い登録済みの物は後ろに足してある） */
    const std::vector<std::string>& GetOrder() const {
        return order_;
    }

    /** @brief 名前のエフェクトを設定で止めているか */
    bool IsDisabled(const std::string& name) const;

    /** @brief 設定ファイルの場所 */
    static constexpr const char* kConfigPath = "Resources/Config/post_process.json";

    /**
     * @brief 設定ファイルの代わりに使う中身を渡す（エディタで変えた順番を、保存の前からゲームに効かせる）
     * @note 渡した後、どの列も次の Execute で読み直す
     */
    static void SetConfigOverride(const nlohmann::json& config);
    /** @brief 渡した中身をやめて、設定ファイルを読み直させる */
    static void ClearConfigOverride();

    /**
     * @brief 名前にエディタで見せる表示名を付ける
     * @param name 設定ファイルに書く名前
     * @param displayName エディタに出す名前
     */
    static void SetDisplayName(const std::string& name, const std::string& displayName);
    /** @brief エディタに出す名前を返す（付けていなければ設定ファイルの名前のまま） */
    static std::string GetDisplayName(const std::string& name);
    /** @brief どこかの列に一度でも登録された名前の一覧（エディタで足す候補にする） */
    static std::vector<std::string> GetKnownNames();

    /**
     * @brief 場面の列にエディタで見せる表示名を付ける
     * @param stackName 設定ファイルの stacks の下の名前
     * @param displayName エディタに出す名前（本編・訓練場など）
     */
    static void SetStackDisplayName(const std::string& stackName, const std::string& displayName);
    /** @brief 場面の列の表示名を返す（付けていなければ設定ファイルの名前のまま） */
    static std::string GetStackDisplayName(const std::string& stackName);
    /** @brief 一番最近に掛けた場面の列の名前（今の画面がどの場面か。まだ無ければ空） */
    static std::string GetActiveStackName();

private:
    /** @brief 登録した1つのエフェクト */
    struct Stage {
        std::string name; ///< 設定ファイルに書く名前
        ActiveFunction isActive; ///< 今掛けるか
        ApplyFunction apply; ///< 掛ける処理
    };

    /** @brief 名前で登録済みのエフェクトを探す（無ければ nullptr） */
    const Stage* FindStage(const std::string& name) const;
    /** @brief 登録済みなのに順番に無い物を後ろに足し、知らせる */
    void AppendUnlistedStages();

    /** @brief 設定ファイルの stacks の下の名前 */
    std::string stackName_;
    /** @brief 登録したエフェクト（登録した順） */
    std::vector<Stage> stages_;
    /** @brief 掛ける順番 */
    std::vector<std::string> order_;
    /** @brief 設定で止めている名前 */
    std::vector<std::string> disabled_;
    /** @brief 順番に書いてあるのに登録されていない名前（同じ知らせを毎フレーム出さないために覚える） */
    std::vector<std::string> reportedMissing_;
    /** @brief 読んだ時の設定の版（SetConfigOverride などで変わったら読み直す） */
    uint32_t loadedConfigVersion_ = 0;
};

} // namespace engine::graphics

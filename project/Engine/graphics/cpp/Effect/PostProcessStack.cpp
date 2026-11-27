/**
 * @file PostProcessStack.cpp
 * @brief 画面全体へ掛けるエフェクトを、設定ファイルで決めた順番に掛けるクラスの実装
 */
#include "PostProcessStack.h"
#include "BloomEffect.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "PipelineStateGuard.h"
#include "ToneMapEffect.h"
#include <algorithm>
namespace engine::graphics {
namespace {

/** @brief 名前の並びに name が入っているか */
bool Contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

/** @brief 設定の差し替えと名前の表（どの列からも共通。エディタと描画はどちらもメインのスレッドから触る） */
struct SharedConfig {
    bool hasOverride = false; ///< 設定ファイルの代わりの中身があるか
    nlohmann::json overrideConfig; ///< 設定ファイルの代わりの中身
    uint32_t version = 1; ///< 差し替えるたびに増やす（列はこれが変わったら読み直す）
    std::vector<std::string> knownNames; ///< 一度でも登録された名前（登録した順）
    std::vector<std::pair<std::string, std::string>> displayNames; ///< 名前と、エディタに出す名前
    std::vector<std::pair<std::string, std::string>> stackDisplayNames; ///< 場面の列の名前と、エディタに出す名前
    std::string activeStackName; ///< 一番最近に掛けた場面の列の名前
};

/** @brief 共通の設定を返す */
SharedConfig& Shared() {
    static SharedConfig sShared;
    return sShared;
}

/** @brief 名前と表示名の表へ、表示名を付ける（同じ名前があれば付け替える） */
void SetNamedLabel(std::vector<std::pair<std::string, std::string>>& labels, const std::string& name, const std::string& label) {
    for (auto& [key, value] : labels) {
        if (key == name) {
            value = label;
            return;
        }
    }
    labels.emplace_back(name, label);
}

/** @brief 名前と表示名の表から表示名を引く（無ければ名前のまま） */
std::string FindNamedLabel(const std::vector<std::pair<std::string, std::string>>& labels, const std::string& name) {
    for (const auto& [key, value] : labels) {
        if (key == name) {
            return value;
        }
    }
    return name;
}

/** @brief JSON の文字列の配列を読む（文字列でない要素は飛ばす） */
std::vector<std::string> ReadNames(const nlohmann::json& value) {
    std::vector<std::string> names;
    if (!value.is_array()) {
        return names;
    }
    for (const auto& item : value) {
        if (item.is_string()) {
            names.push_back(item.get<std::string>());
        }
    }
    return names;
}

}

/** @brief 列の名前を覚えて、設定ファイルから順番を読む */
void PostProcessStack::Initialize(const std::string& stackName) {
    stackName_ = stackName;
    ReloadOrder();
}

/** @brief 同じ名前があれば差し替え、無ければ足す */
void PostProcessStack::Register(const std::string& name, ActiveFunction isActive, ApplyFunction apply) {
    auto found = std::find_if(stages_.begin(), stages_.end(), [&name](const Stage& stage) {
        return stage.name == name;
    });
    if (found != stages_.end()) {
        found->isActive = std::move(isActive);
        found->apply = std::move(apply);
        return;
    }
    stages_.push_back({ name, std::move(isActive), std::move(apply) });
    // エディタで足す候補にするため、名前を覚えておく
    if (!Contains(Shared().knownNames, name)) {
        Shared().knownNames.push_back(name);
    }
    AppendUnlistedStages();
}

/** @brief ブルームとトーンマップを登録する */
void PostProcessStack::RegisterBuiltins(SrvManager* srvManager) {
    SetDisplayName(kBloomName, "ブルーム（明るい所のにじみ）");
    SetDisplayName(kToneMapName, "トーンマップ（明るすぎる所を丸める）");
    // 明るく描いた所をにじませて光らせる
    Register(kBloomName, [] {
        return BloomEffect::GetInstance()->IsEnabled();
    }, [srvManager] {
        BloomEffect::GetInstance()->CaptureAndApply(srvManager);
    });
    // 1 を超えた光をなめらかに丸める
    Register(kToneMapName, [] {
        return ToneMapEffect::GetInstance()->IsEnabled();
    }, [] {
        ToneMapEffect::GetInstance()->CaptureAndApply();
    });
}

/** @brief 決めた順番で、止めていなくて今掛ける物だけを掛ける */
void PostProcessStack::Execute(const std::function<void()>& restoreTarget) {
    // エディタで今の画面がどの場面かを見せるため、掛けた場面を覚えておく
    Shared().activeStackName = stackName_;
    // エディタで順番が変わっていたら、掛ける前に読み直す
    if (loadedConfigVersion_ != Shared().version) {
        ReloadOrder();
    }
    for (const std::string& name : order_) {
        if (Contains(disabled_, name)) {
            continue;
        }
        const Stage* stage = FindStage(name);
        if (stage == nullptr) {
            // 設定に書いてあるのに、この場面では登録されていない（書き間違いか、この場面に無いエフェクト）
            if (!Contains(reportedMissing_, name)) {
                reportedMissing_.push_back(name);
                Logger::LogWarning("post_process.json の " + stackName_ + " にある " + name + " は登録されていません");
            }
            continue;
        }
        if (!stage->isActive || !stage->isActive()) {
            continue;
        }
        // スコープを抜けたら必ず描画先とパイプラインを戻す
        PipelineStateGuard restoreGuard(restoreTarget);
        stage->apply();
    }
}

/** @brief 設定ファイルから順番と止める物を読み直す */
void PostProcessStack::ReloadOrder() {
    // エディタが渡した中身があればそれを、無ければ設定ファイルを読む
    loadedConfigVersion_ = Shared().version;
    const nlohmann::json root = Shared().hasOverride ? Shared().overrideConfig : JsonHelper::Load(kConfigPath);
    const nlohmann::json stacks = root.value("stacks", nlohmann::json::object());
    const nlohmann::json stack = stacks.value(stackName_, nlohmann::json::object());
    order_ = ReadNames(stack.value("order", nlohmann::json::array()));
    disabled_ = ReadNames(stack.value("disabled", nlohmann::json::array()));
    reportedMissing_.clear();
    if (order_.empty()) {
        Logger::LogWarning("post_process.json に " + stackName_ + " の順番が無いので、登録した順に掛けます");
    }
    AppendUnlistedStages();
}

/** @brief 名前で探す */
const PostProcessStack::Stage* PostProcessStack::FindStage(const std::string& name) const {
    for (const Stage& stage : stages_) {
        if (stage.name == name) {
            return &stage;
        }
    }
    return nullptr;
}

/** @brief 順番に無い登録済みの物を後ろに足す（掛け忘れを防ぐ。順番を決めたい時は設定に書く） */
void PostProcessStack::AppendUnlistedStages() {
    for (const Stage& stage : stages_) {
        if (Contains(order_, stage.name)) {
            continue;
        }
        order_.push_back(stage.name);
        // 設定を読んだ後に足した時だけ知らせる（設定の無い列は全部この形になるので知らせない）
        if (!stackName_.empty()) {
            Logger::LogInfo("post_process.json の " + stackName_ + " に " + stage.name + " が無いので、最後に掛けます");
        }
    }
}

/** @brief 設定ファイルの代わりの中身を覚え、版を進めて全部の列に読み直させる */
void PostProcessStack::SetConfigOverride(const nlohmann::json& config) {
    Shared().overrideConfig = config;
    Shared().hasOverride = true;
    ++Shared().version;
}

/** @brief 代わりの中身をやめ、版を進めて設定ファイルを読み直させる */
void PostProcessStack::ClearConfigOverride() {
    Shared().overrideConfig = nlohmann::json::object();
    Shared().hasOverride = false;
    ++Shared().version;
}

/** @brief 表示名を付ける（同じ名前があれば付け替える） */
void PostProcessStack::SetDisplayName(const std::string& name, const std::string& displayName) {
    SetNamedLabel(Shared().displayNames, name, displayName);
}

/** @brief 表示名を引く（無ければ名前のまま） */
std::string PostProcessStack::GetDisplayName(const std::string& name) {
    return FindNamedLabel(Shared().displayNames, name);
}

/** @brief 場面の列の表示名を付ける */
void PostProcessStack::SetStackDisplayName(const std::string& stackName, const std::string& displayName) {
    SetNamedLabel(Shared().stackDisplayNames, stackName, displayName);
}

/** @brief 場面の列の表示名を引く（無ければ名前のまま） */
std::string PostProcessStack::GetStackDisplayName(const std::string& stackName) {
    return FindNamedLabel(Shared().stackDisplayNames, stackName);
}

/** @brief 一番最近に掛けた場面の列の名前を返す */
std::string PostProcessStack::GetActiveStackName() {
    return Shared().activeStackName;
}

/** @brief 登録された名前の一覧を返す */
std::vector<std::string> PostProcessStack::GetKnownNames() {
    return Shared().knownNames;
}

} // namespace engine::graphics

/**
 * @file BugHuntChanges.cpp
 * @brief バグ探しで、前回の記録から変わった所を調べる処理の実装
 */
#include "BugHuntChanges.h"
#include "JsonHelper.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>

namespace engine::game {
namespace {
namespace fs = std::filesystem;

constexpr const char* kStatePath = "smoke_test/bug_hunt_state.json"; // 前回の記録（起動役が置き換える）
constexpr const char* kNextStatePath = "smoke_test/bug_hunt_state_next.json"; // 今回の様子（最後まで回った時だけ書く）
constexpr const char* kStateDirectory = "smoke_test";
constexpr const char* kGlobalKey = "global"; // 記録の中の、全部に効くファイルの印
constexpr const char* kStagesKey = "stages"; // 記録の中の、ステージごとの様子
constexpr const char* kFilesKey = "files";
constexpr const char* kObjectsKey = "objects";
constexpr const char* kExeKey = "exe"; // 記録の中で exe の印に使う名前
constexpr const char* kUnnamedPrefix = "#"; // 名前の無い配置物は並びの番号で見分ける
constexpr const char* kMissingMark = "none"; // ファイルが無い時の印
constexpr int kJsonIndent = 1; // 記録の JSON の字下げ
// 中身が変わると、どのステージにも効くフォルダ（設定とシェーダー）
constexpr const char* kGlobalFolders[] = { "Resources/Config", "Resources/shaders" };

/** @brief ファイルの更新時刻と大きさを1つの印にする（無ければ none） */
std::string FileMark(const fs::path& path) {
    std::error_code error;
    const auto time = fs::last_write_time(path, error);
    if (error) {
        return kMissingMark;
    }
    const auto size = fs::file_size(path, error);
    return std::to_string(time.time_since_epoch().count()) + ":" + std::to_string(error ? 0 : size);
}

/** @brief 文字列を短い印にする */
std::string HashMark(const std::string& text) {
    return std::to_string(std::hash<std::string> {}(text));
}

/** @brief json の key がオブジェクトならそれを、違えば空のオブジェクトを返す（壊れた記録で落ちないように） */
nlohmann::json ObjectAt(const nlohmann::json& json, const std::string& key) {
    if (!json.is_object()) {
        return nlohmann::json::object();
    }
    const auto found = json.find(key);
    return found != json.end() && found->is_object() ? *found : nlohmann::json::object();
}

/** @brief json の key が文字列ならそれを、違えば空を返す */
std::string StringAt(const nlohmann::json& json, const std::string& key) {
    if (!json.is_object()) {
        return std::string();
    }
    const auto found = json.find(key);
    return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
}

/** @brief この exe の場所 */
fs::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer);
}
} // namespace

/** @brief レベルの JSON とそのグラフから、ステージ1つ分の様子を作る */
BugHuntChanges::StageState BugHuntChanges::ReadStage(const std::string& levelPath) {
    StageState state;
    state.files = FileMark(levelPath);
    const nlohmann::json level = JsonHelper::Load(levelPath);
    if (!level.is_object()) {
        return state;
    }
    // レベルが使うグラフも、そのステージの中身として数える
    std::vector<std::string> graphs;
    const std::string graphPath = level.value("graphPath", std::string());
    if (!graphPath.empty()) {
        graphs.push_back(graphPath);
    }
    for (const nlohmann::json& binding : level.value("flagGraphs", nlohmann::json::array())) {
        if (binding.is_object() && !binding.value("graphPath", std::string()).empty()) {
            graphs.push_back(binding.value("graphPath", std::string()));
        }
    }
    for (const std::string& graph : graphs) {
        state.files += "|" + graph + "=" + FileMark(graph);
    }
    // 配置物は1つずつ中身の印を取る（増えた・変わった物を近くで試すため）
    const nlohmann::json objects = level.value("objects", nlohmann::json::array());
    int index = 0;
    for (const nlohmann::json& object : objects) {
        if (object.is_object()) {
            const std::string name = object.value("name", std::string());
            state.objects[name.empty() ? kUnnamedPrefix + std::to_string(index) : name] = HashMark(object.dump());
        }
        ++index;
    }
    return state;
}

/** @brief 全部のステージに効くファイル（exe・設定・シェーダー・手順の表）の印を、場所ごとに集める */
std::map<std::string, std::string> BugHuntChanges::ReadGlobalFiles(const std::string& scriptPath) {
    std::map<std::string, std::string> files;
    files[kExeKey] = FileMark(ExecutablePath());
    files[scriptPath] = FileMark(scriptPath);
    for (const char* folder : kGlobalFolders) {
        std::error_code error;
        for (const auto& entry : fs::recursive_directory_iterator(folder, error)) {
            if (entry.is_regular_file(error)) {
                files[entry.path().generic_string()] = FileMark(entry.path());
            }
        }
    }
    return files;
}

/** @brief 今のファイルの様子を集め、前回の記録と比べて遊ぶステージを決める */
void BugHuntChanges::Prepare(const std::vector<Stage>& stages, bool forceAll, const std::string& scriptPath) {
    prepared_ = true;
    stages_ = stages;
    current_.clear();
    for (const Stage& stage : stages_) {
        current_[stage.levelPath] = ReadStage(stage.levelPath);
    }
    currentGlobal_ = ReadGlobalFiles(scriptPath);
    previous_ = JsonHelper::Load(kStatePath);
    selected_.clear();

    // 全部遊ぶのは、頼まれた時・記録が無い時・どのステージにも効くファイルが変わった時
    selectAll_ = true;
    if (forceAll) {
        allReason_ = "--all で全部";
        return;
    }
    if (!previous_.is_object()) {
        allReason_ = "前回の記録が無いので全部";
        return;
    }
    const nlohmann::json previousGlobal = ObjectAt(previous_, kGlobalKey);
    for (const auto& [path, mark] : currentGlobal_) {
        if (StringAt(previousGlobal, path) != mark) {
            allReason_ = (path == kExeKey ? std::string("ゲームを作り直した") : path + " が変わった") + "ので全部";
            return;
        }
    }
    selectAll_ = false;
    const nlohmann::json previousStages = ObjectAt(previous_, kStagesKey);
    for (const Stage& stage : stages_) {
        const bool recorded = previousStages.contains(stage.levelPath);
        const nlohmann::json previousStage = ObjectAt(previousStages, stage.levelPath);
        const bool changed = !recorded || StringAt(previousStage, kFilesKey) != current_[stage.levelPath].files;
        if (changed) {
            selected_.push_back(stage.levelPath);
        }
    }
}

/** @brief このステージを遊ぶか（Prepare していなければ全部遊ぶ） */
bool BugHuntChanges::IsStageSelected(const std::string& levelPath) const {
    if (!prepared_ || selectAll_) {
        return true;
    }
    return std::find(selected_.begin(), selected_.end(), levelPath) != selected_.end();
}

/** @brief 前回から増えた・変わった配置物の名前（前回の記録にこのステージが無ければ空） */
std::vector<std::string> BugHuntChanges::ChangedObjects(const std::string& levelPath) const {
    std::vector<std::string> names;
    const auto found = current_.find(levelPath);
    if (!prepared_ || !previous_.is_object() || found == current_.end()) {
        return names;
    }
    const nlohmann::json previousStages = ObjectAt(previous_, kStagesKey);
    if (!previousStages.contains(levelPath)) {
        return names;
    }
    const nlohmann::json previousObjects = ObjectAt(ObjectAt(previousStages, levelPath), kObjectsKey);
    for (const auto& [name, mark] : found->second.objects) {
        if (StringAt(previousObjects, name) != mark) {
            names.push_back(name);
        }
    }
    return names;
}

/** @brief 報告の 回した所 の行に出す説明 */
std::string BugHuntChanges::Summary() const {
    if (!prepared_) {
        return "全部";
    }
    if (selectAll_) {
        return "全部（" + allReason_ + "）";
    }
    if (selected_.empty()) {
        return "前回から変えたステージが無い";
    }
    std::string labels;
    for (const Stage& stage : stages_) {
        if (IsStageSelected(stage.levelPath)) {
            labels += (labels.empty() ? "" : "・") + stage.label;
        }
    }
    return "変えた所だけ（" + labels + "）";
}

/** @brief 今の様子を記録の JSON にする */
nlohmann::json BugHuntChanges::ToJson() const {
    nlohmann::json state = nlohmann::json::object();
    state[kGlobalKey] = currentGlobal_;
    nlohmann::json stages = nlohmann::json::object();
    for (const auto& [path, stage] : current_) {
        stages[path] = { { kFilesKey, stage.files }, { kObjectsKey, stage.objects } };
    }
    state[kStagesKey] = stages;
    return state;
}

/** @brief 今のファイルの様子を次の記録として書き出す（最後まで回った時に呼ぶ） */
void BugHuntChanges::WriteNextState() const {
    if (!prepared_) {
        return;
    }
    std::error_code error;
    fs::create_directories(kStateDirectory, error);
    std::ofstream file(kNextStatePath);
    if (file) {
        file << ToJson().dump(kJsonIndent);
    }
}

} // namespace engine::game

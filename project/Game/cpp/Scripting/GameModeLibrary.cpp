/**
 * @file GameModeLibrary.cpp
 * @brief ゲームの型の表の読み込みと検索を実装するファイル
 */
#include "GameModeLibrary.h"
#include "JsonHelper.h"
#include <algorithm>
#include <filesystem>

namespace engine::game {
namespace {
constexpr const char* kModeDirectory = "Resources/Config/GameModes"; ///< 型のファイルを置くフォルダ
constexpr const char* kModeIndexPath = "Resources/Config/GameModes/index.json"; ///< 型のファイルの一覧（配布版はフォルダを一覧できないのでこれを読む）
constexpr const char* kIndexFileName = "index.json"; ///< 一覧のファイル自身の名前（型として読まない）

/** @brief 文字列の配列を読む（配列でなければ空） */
std::vector<std::string> ReadStrings(const nlohmann::json& root, const char* key) {
    std::vector<std::string> result;
    const auto found = root.find(key);
    if (found == root.end() || !found->is_array()) {
        return result;
    }
    for (const auto& item : *found) {
        if (item.is_string()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

/** @brief 種類の配列を読む（"種類名" か {"kind":..., "label":...} のどちらでも書ける） */
std::vector<GameModeKind> ReadKinds(const nlohmann::json& root, const char* key) {
    std::vector<GameModeKind> result;
    const auto found = root.find(key);
    if (found == root.end() || !found->is_array()) {
        return result;
    }
    for (const auto& item : *found) {
        if (item.is_string()) {
            result.push_back({ item.get<std::string>(), item.get<std::string>() });
        } else if (item.is_object() && item.contains("kind")) {
            const std::string kind = item.value("kind", std::string());
            result.push_back({ kind, item.value("label", kind) });
        }
    }
    return result;
}

/** @brief 1つのファイルから型を読む（id が無ければファイル名を id にする） */
GameModeDesc ReadMode(const nlohmann::json& root, const std::string& fileStem) {
    GameModeDesc mode;
    mode.id = root.value("id", fileStem);
    mode.name = root.value("name", mode.id);
    mode.description = root.value("description", std::string());
    // カメラの付き方
    if (const auto camera = root.find("camera"); camera != root.end() && camera->is_object()) {
        mode.cameraType = camera->value("type", mode.cameraType);
        mode.useStageBoundsCamera = camera->value("useStageBounds", mode.useStageBoundsCamera);
        mode.camera.distance = camera->value("distance", mode.camera.distance);
        mode.camera.height = camera->value("height", mode.camera.height);
        mode.camera.targetHeight = camera->value("targetHeight", mode.camera.targetHeight);
        mode.camera.lookAhead = camera->value("lookAhead", mode.camera.lookAhead);
    }
    // 道に沿って進む決まり
    if (const auto path = root.find("path"); path != root.end() && path->is_object()) {
        mode.path.follow = path->value("follow", mode.path.follow);
        mode.path.playerHalfWidth = path->value("playerHalfWidth", mode.path.playerHalfWidth);
        mode.path.playerHalfHeight = path->value("playerHalfHeight", mode.path.playerHalfHeight);
        mode.path.endFlag = path->value("endFlag", mode.path.endFlag);
    }
    // プレイヤーの動き方
    if (const auto player = root.find("player"); player != root.end() && player->is_object()) {
        mode.player.depthMovement = player->value("depthMovement", mode.player.depthMovement);
        mode.player.depthSpeed = player->value("depthSpeed", mode.player.depthSpeed);
        mode.player.depthMin = player->value("depthMin", mode.player.depthMin);
        mode.player.depthMax = player->value("depthMax", mode.player.depthMax);
        mode.player.enemiesFollowDepth = player->value("enemiesFollowDepth", mode.player.enemiesFollowDepth);
        mode.player.enemyDepthSpeed = player->value("enemyDepthSpeed", mode.player.enemyDepthSpeed);
        mode.player.hitHalfDepth = player->value("hitHalfDepth", mode.player.hitHalfDepth);
    }
    mode.requiredKinds = ReadKinds(root, "requiredKinds");
    mode.recommendedKinds = ReadKinds(root, "recommendedKinds");
    mode.tips = ReadStrings(root, "tips");
    // 操作の表（keys と label の組）
    if (const auto controls = root.find("controls"); controls != root.end() && controls->is_array()) {
        for (const auto& item : *controls) {
            if (item.is_object()) {
                mode.controls.push_back({ item.value("keys", std::string()), item.value("label", std::string()) });
            }
        }
    }
    return mode;
}
} // namespace

/** @brief 唯一のインスタンスを返す */
GameModeLibrary* GameModeLibrary::GetInstance() {
    static GameModeLibrary sInstance;
    return &sInstance;
}

/** @brief 組み込みの横スクロールの型を用意してから、フォルダを読む */
GameModeLibrary::GameModeLibrary() {
    fallback_.id = kDefaultId;
    fallback_.name = "横スクロールアクション";
    Reload();
}

/** @brief 一覧のファイルに書いた型と、フォルダに置いた型をまとめて読み直す（壊れたファイルは飛ばす） */
void GameModeLibrary::Reload() {
    modes_.clear();
    // 一覧のファイルに書いた順を先にし、フォルダにだけある物はファイル名の順で後ろに足す
    std::vector<std::filesystem::path> files;
    const auto addFile = [&files](const std::filesystem::path& file) {
        if (file.filename() != kIndexFileName && std::find(files.begin(), files.end(), file) == files.end()) {
            files.push_back(file);
        }
    };
    const nlohmann::json index = JsonHelper::Load(kModeIndexPath);
    for (const std::string& name : index.is_object() ? ReadStrings(index, "files") : std::vector<std::string>()) {
        addFile(std::filesystem::path(kModeDirectory) / name);
    }
    std::vector<std::filesystem::path> scanned;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(kModeDirectory, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            scanned.push_back(entry.path());
        }
    }
    std::sort(scanned.begin(), scanned.end());
    for (const auto& file : scanned) {
        addFile(file);
    }
    for (const auto& file : files) {
        const nlohmann::json root = JsonHelper::Load(file.generic_string());
        if (!root.is_object()) {
            continue;
        }
        try {
            modes_.push_back(ReadMode(root, file.stem().string()));
        } catch (const nlohmann::json::exception&) {
            // 型の違う項目があるファイルだけ飛ばす
        }
    }
}

namespace {
/** @brief 種類の一覧を JSON の配列にする */
nlohmann::json KindsToJson(const std::vector<GameModeKind>& kinds) {
    nlohmann::json list = nlohmann::json::array();
    for (const GameModeKind& kind : kinds) {
        list.push_back({ { "kind", kind.kind }, { "label", kind.label } });
    }
    return list;
}
} // namespace

/** @brief 型を読み込みと同じ形の JSON にして書き、一覧のファイルにも足してから読み直す */
bool GameModeLibrary::Save(const GameModeDesc& mode) {
    if (mode.id.empty()) {
        return false;
    }
    nlohmann::json root = nlohmann::json::object();
    root["_comment"] = "エディタの型を作る欄で作った型（書き方は side_action.json の _comment）";
    root["id"] = mode.id;
    root["name"] = mode.name;
    root["description"] = mode.description;
    root["camera"] = { { "type", mode.cameraType }, { "useStageBounds", mode.useStageBoundsCamera },
        { "distance", mode.camera.distance }, { "height", mode.camera.height },
        { "targetHeight", mode.camera.targetHeight }, { "lookAhead", mode.camera.lookAhead } };
    root["path"] = { { "follow", mode.path.follow }, { "playerHalfWidth", mode.path.playerHalfWidth },
        { "playerHalfHeight", mode.path.playerHalfHeight }, { "endFlag", mode.path.endFlag } };
    root["player"] = { { "depthMovement", mode.player.depthMovement }, { "depthSpeed", mode.player.depthSpeed },
        { "depthMin", mode.player.depthMin }, { "depthMax", mode.player.depthMax },
        { "enemiesFollowDepth", mode.player.enemiesFollowDepth }, { "enemyDepthSpeed", mode.player.enemyDepthSpeed },
        { "hitHalfDepth", mode.player.hitHalfDepth } };
    root["requiredKinds"] = KindsToJson(mode.requiredKinds);
    root["recommendedKinds"] = KindsToJson(mode.recommendedKinds);
    root["tips"] = mode.tips;
    nlohmann::json controls = nlohmann::json::array();
    for (const GameModeControl& control : mode.controls) {
        controls.push_back({ { "keys", control.keys }, { "label", control.label } });
    }
    root["controls"] = controls;
    const std::string fileName = mode.id + ".json";
    if (!JsonHelper::Save(std::string(kModeDirectory) + "/" + fileName, root)) {
        return false;
    }
    // 配布版でも読めるよう、一覧のファイルにも名前を足す（もう書いてあれば足さない）
    nlohmann::json index = JsonHelper::Load(kModeIndexPath);
    if (!index.is_object()) {
        index = nlohmann::json::object();
    }
    std::vector<std::string> files = ReadStrings(index, "files");
    if (std::find(files.begin(), files.end(), fileName) == files.end()) {
        files.push_back(fileName);
        index["files"] = files;
        if (!JsonHelper::Save(kModeIndexPath, index)) {
            return false;
        }
    }
    Reload();
    return true;
}

/** @brief この id の型がもうあるか */
bool GameModeLibrary::Contains(const std::string& id) const {
    return std::any_of(modes_.begin(), modes_.end(), [&id](const GameModeDesc& mode) {
        return mode.id == id;
    });
}

/** @brief id の型を返す（無ければ既定の型、それも無ければ組み込みの型） */
const GameModeDesc& GameModeLibrary::Find(const std::string& id) const {
    const std::string& wanted = id.empty() ? std::string(kDefaultId) : id;
    // 探す名前と既定の名前の順に、一覧から探す
    for (const std::string& candidate : { wanted, std::string(kDefaultId) }) {
        const auto found = std::find_if(modes_.begin(), modes_.end(), [&candidate](const GameModeDesc& mode) {
            return mode.id == candidate;
        });
        if (found != modes_.end()) {
            return *found;
        }
    }
    return fallback_;
}

} // namespace engine::game

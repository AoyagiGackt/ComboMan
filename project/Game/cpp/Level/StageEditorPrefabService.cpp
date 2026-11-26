/**
 * @file StageEditorPrefabService.cpp
 * @brief ステージエディタのプレハブ保存と読み込み、選んだ物をまとめてプレハブにする・置く StageEditor の操作を実装するファイル
 */
#include "StageEditorPrefabService.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#ifdef USE_IMGUI
#include "StageEditor.h"
#include "ObjectKind.h"
#include "Object3d.h"
#include "EnemyEntity.h"
#include "KnightEnemy.h"
#endif

namespace engine::game {

/** @brief 配置物を原点基準のプレハブとして保存し、保存先を返す */
std::string StageEditorPrefabService::Save(const std::string& name, const std::vector<ObjectDesc>& objects) {
    // 基準位置からの相対座標をまとめて保存する
    LevelData prefab;
    prefab.objects = objects;
    const std::string path = MakePath(name);
    if (!levelLoader::Save(path, prefab)) {
        return {};
    }
    return path;
}

/** @brief 単独の配置物を原点に合わせて保存する */
std::string StageEditorPrefabService::Save(const std::string& name, const ObjectDesc& object) {
    std::vector<ObjectDesc> parts = { object };
    parts.front().position = {};
    parts.front().parent.clear();
    return Save(name, parts);
}

/** @brief 名前に対応するプレハブ内の配置物を読み込む */
std::vector<ObjectDesc> StageEditorPrefabService::Load(const std::string& name) {
    return levelLoader::Load(MakePath(name)).objects;
}

/** @brief ファイル名に利用できる安全なプレハブ名へ変換する */
std::string StageEditorPrefabService::SanitizeName(const std::string& name) {
    std::string safeName = name;
    safeName.erase(std::remove_if(safeName.begin(), safeName.end(), [](unsigned char character) {
        return !(std::isalnum(character) || character == '_' || character == '-');
    }),
        safeName.end());
    return safeName.empty() ? "stage_part" : safeName;
}

/** @brief プレハブ名から保存先の相対パスを構築する */
std::string StageEditorPrefabService::MakePath(const std::string& name) {
    return "Resources/Prefabs/" + SanitizeName(name) + ".json";
}

#ifdef USE_IMGUI
/** @brief 選択中の配置物を名前付きプレハブへ保存する */
void StageEditor::SaveSelectedPrefab() {
    if (selKind_ != SelKind::kObject || selIndex_ < 0 || selIndex_ >= static_cast<int>(objects_.size())) {
        return;
    }
    // 主選択を原点にし、選択外の親からも独立した座標で控える
    const Vector3 origin = WorldPositionOf(objects_[selIndex_].desc);
    std::vector<ObjectDesc> parts;
    // 選択内の親子関係だけを残す
    std::map<std::string, const ObjectDesc*> selected;
    const auto appendPart = [&](int index) {
        ObjectDesc part = objects_[index].desc;
        selected[part.name] = &objects_[index].desc;
        parts.push_back(std::move(part));
    };
    appendPart(selIndex_);
    for (int index : selectedObjectIndices_) {
        if (index >= 0 && index < static_cast<int>(objects_.size()) && index != selIndex_) {
            appendPart(index);
        }
    }
    const auto selectedParent = [&](const std::string& name) -> const ObjectDesc* {
        const auto found = selected.find(name);
        return found == selected.end() ? nullptr : found->second;
    };
    for (auto& part : parts) {
        const Vector3 world = WorldPositionOf(part);
        if (const ObjectDesc* parent = selectedParent(part.parent)) {
            float angle = 0.0f;
            VisitParentChain(part, selectedParent, [&](const ObjectDesc& ancestor) {
                angle += ancestor.rotation.z;
            });
            part.position = RotateZAndOffset(Subtract(world, WorldPositionOf(*parent)), -angle, {});
        } else {
            part.parent.clear();
            part.position = Subtract(world, origin);
        }
    }
    const std::string path = StageEditorPrefabService::Save(prefabName_, parts);
    if (path.empty()) {
        statusMessage_ = "プレハブの保存に失敗しました";
        statusTimer_ = kStatusLongSeconds;
        return;
    }
    statusMessage_ = "プレハブを保存しました: " + path;
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

/** @brief 名前付きプレハブを画面中央へ生成する */
void StageEditor::InstantiatePrefab() {
    const std::string path = StageEditorPrefabService::MakePath(prefabName_);
    std::vector<ObjectDesc> prefabObjects = StageEditorPrefabService::Load(prefabName_);
    if (prefabObjects.empty()) {
        statusMessage_ = "プレハブが見つかりません: " + path;
        statusTimer_ = StageEditor::kStatusShortSeconds;
        return;
    }

    RecordUndoSnapshotNow();
    Vector3 center = ViewCenterOnGround();
    // 元の部品同士を結んでいた合図は、新しい部品名に読み替える
    std::map<std::string, std::string> names;
    std::map<std::string, std::string> flags;
    for (auto& part : prefabObjects) {
        std::string name;
        do {
            name = "prefab_" + std::to_string(nextSerial_++);
        } while (FindDescByName(name));
        if (const char* prefix = ObjectKind::Of(part.kind).RuntimeFlagPrefix()) {
            flags[std::string(prefix) + part.name] = std::string(prefix) + name;
        }
        names[part.name] = name;
        if (part.parent.empty()) {
            part.position = center + part.position;
        }
    }
    const float shift = GroupFloorShift(prefabObjects);
    const size_t first = objects_.size();
    for (auto& desc : prefabObjects) {
        ObjectEntry entry;
        entry.desc = std::move(desc);
        if (const auto flag = flags.find(entry.desc.activationFlag); flag != flags.end()) {
            entry.desc.activationFlag = flag->second;
        }
        entry.desc.name = names.at(entry.desc.name);
        if (const auto parent = names.find(entry.desc.parent); !entry.desc.parent.empty() && parent != names.end()) {
            entry.desc.parent = parent->second;
        } else {
            entry.desc.parent.clear();
            entry.desc.position.y += shift;
        }
        entry.authoredPosition = entry.desc.position;
        objects_.push_back(std::move(entry));
    }
    // 親が後ろに入っていても参照できるよう、全部品がそろってから生成する
    selectedObjectIndices_.clear();
    for (size_t index = first; index < objects_.size(); ++index) {
        RegenerateInstances(objects_[index]);
        selectedObjectIndices_.push_back(static_cast<int>(index));
    }
    selKind_ = SelKind::kObject;
    selIndex_ = static_cast<int>(first);
    statusMessage_ = "プレハブを配置しました";
    statusTimer_ = StageEditor::kStatusShortSeconds;
}

#endif
} // namespace engine::game

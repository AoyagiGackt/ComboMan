/**
 * @file StageEditorGraphLink.cpp
 * @brief ノードエディタから、今のレベルとグラフをつなぐ・グラフを動かし直す・すぐ遊んで確かめるための入口
 * @note クラスは StageEditor のまま。ノードエディタ側はこの関数だけを使い、レベルの中身には直接触らない
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include <algorithm>
#include <cstring>

namespace engine::game {

/** @brief このグラフが今のレベルでどう使われているかを普通の言葉で返す（使われていなければ空） */
std::string StageEditor::DescribeGraphUse(const std::string& path) const {
    if (path.empty()) {
        return {};
    }
    if (path == graphPath_) {
        return "レベルを始めた時から動く常駐のグラフです";
    }
    const auto bound = std::find_if(flagGraphs_.begin(), flagGraphs_.end(), [&path](const FlagGraphBinding& binding) {
        return binding.graphPath == path;
    });
    if (bound != flagGraphs_.end()) {
        return "フラグ " + bound->flag + " が立った時に動くグラフです";
    }
    return {};
}

/** @brief このグラフを今のレベルの常駐のグラフにして、動かし直す（元に戻せる） */
void StageEditor::UseAsResidentGraph(const std::string& path) {
    if (path == graphPath_) {
        return;
    }
    RecordUndoSnapshotNow();
    graphPath_ = path;
    strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);
    RestartLevelGraphs();
    statusMessage_ = "このレベルの常駐のグラフにしました: " + path + "（レベルの保存で残ります）";
    statusTimer_ = kStatusLongSeconds;
}

/** @brief 今の設定でレベルのグラフを最初から動かし直す（保存したグラフの中身が反映される） */
void StageEditor::RestartLevelGraphs() {
    LevelData data;
    data.graphPath = graphPath_;
    data.flagGraphs = flagGraphs_;
    levelGraphs_.Start(data);
}

/** @brief プレイヤーの出る位置からテストを始める（ステージエディタを開いていない時は、ゲームをそのまま続ける） */
void StageEditor::PlayTestFromSpawn() {
    if (visible_) {
        StartPlayTestOrWarn(playerSpawn_);
    }
}

} // namespace engine::game
#endif

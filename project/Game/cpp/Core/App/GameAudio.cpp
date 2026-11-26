/**
 * @file GameAudio.cpp
 * @brief このゲームの曲の表とメニュー音をエンジンの音へ設定する処理を実装するファイル
 */
#include "GameAudio.h"

namespace engine::game {
namespace {
constexpr const char* kBgmTablePath = "Resources/Config/bgm.json"; // シーンのキーから曲を引く表
constexpr const char* kMenuChoiceSound = "Resources/SE/choice.mp3"; // メニューの選択移動音
constexpr const char* kMenuSelectSound = "Resources/SE/select.mp3"; // メニューの決定音
} // namespace

/** @brief このゲームの曲の表とメニュー音をエンジンの音に設定する（起動時に1回呼ぶ） */
void ConfigureGameAudio(engine::Audio& audio) {
    engine::Audio::SetBgmTablePath(kBgmTablePath);
    audio.SetMenuSounds(kMenuChoiceSound, kMenuSelectSound);
}

} // namespace engine::game

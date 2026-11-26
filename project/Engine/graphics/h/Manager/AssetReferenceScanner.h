/**
 * @file AssetReferenceScanner.h
 * @brief 設定ファイルに書かれた素材が、ディスクと pak に揃っているかを調べるクラスのファイル
 */
#pragma once

namespace engine {
class AssetPack;
class DirectXCommon;
}

namespace engine::graphics {
class ModelCommon;

/**
 * @brief 素材を参照する設定をたどり、ディスクとパックの不足を調べる
 * @note pak には全シーンを1回 Init した時に読んだ素材しか入らないので、途中で初めて読む素材が漏れる。
 *       Resources の JSON に書かれた素材を全部集め、pak を作る最後に足りない物を読み込んで足す
 */
class AssetReferenceScanner {
public:
    /** @brief 開発用の素材フォルダに実在しない参照を警告する */
    static void CheckDisk();
    /** @brief 未収録のモデルと画像を読み込み、残った参照を報告する */
    static void CompleteCook(AssetPack& pack, DirectXCommon* device, ModelCommon* modelCommon);
};
} // namespace engine::graphics

/**
 * @file GamePch.h
 * @brief ゲームのプリコンパイル済みヘッダー（全部の cpp が最初に読む、重くてほとんど変わらないヘッダーの集まり）
 * @note Game.vcxproj の設定で全部の cpp に強制で先頭に読ませる（cpp に #include を書かなくてよい）。
 *       中身はエンジンと同じ（標準ライブラリ・Windows・DirectX・外部ライブラリだけ）。
 *       プリコンパイル済みヘッダーはプロジェクトごとに作るので、ゲーム用にもう1つ用意している
 */
#pragma once
#include "EnginePch.h"

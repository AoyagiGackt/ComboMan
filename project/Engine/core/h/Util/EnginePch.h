/**
 * @file EnginePch.h
 * @brief プリコンパイル済みヘッダー（全部の cpp が最初に読む、重くてほとんど変わらないヘッダーの集まり）
 * @note EngineLib.vcxproj の設定で全部の cpp に強制で先頭に読ませる（cpp に #include を書かなくてよい）。
 *       ここには標準ライブラリ・Windows・DirectX・外部ライブラリのヘッダーだけを入れる。
 *       エンジンやゲームのヘッダーを入れると、それを書き換えるたびに全部をコンパイルし直すことになる
 */
#pragma once

// Windows.h の min / max のマクロを止める（std::min を括弧なしで書いている所があるため）
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 標準ライブラリ
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Windows と DirectX
#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>

// 外部ライブラリ
#include "json.hpp"
#ifdef USE_IMGUI
#include <imgui.h>
#endif

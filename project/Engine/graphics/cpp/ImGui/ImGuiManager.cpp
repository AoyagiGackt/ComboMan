/**
 * @file ImGuiManager.cpp
 * @brief ImGui の初期化・フレームの開始と終了・描画（ImGuiManager）の実装
 */
#include "ImGuiManager.h"
#include <SrvManager.h>

namespace engine::graphics {
namespace {

// 日本語フォントの表示サイズ
constexpr float kJapaneseFontSize = 16.0f;

#ifdef USE_IMGUI
// 日本語の範囲に入っていない記号（矢印・数学記号・罫線・図形・その他の記号）。0 で終える
constexpr ImWchar kSymbolGlyphRanges[] = {
    0x2000, 0x206F, // 句読点の仲間（… ※ など）
    0x2100, 0x218F, // 文字のような記号と数の形（℃ Ⅰ など）
    0x2190, 0x21FF, // 矢印（→ ← ↑ ↓ など）
    0x2200, 0x22FF, // 数学記号（× ≦ ≠ など）
    0x2300, 0x23FF, // 技術用の記号（⌘ ⏎ など）
    0x2460, 0x24FF, // 囲み文字（① ② など）
    0x2500, 0x257F, // 罫線
    0x25A0, 0x25FF, // 図形（■ □ ▲ ▼ ○ ● など）
    0x2600, 0x27BF, // その他の記号（★ ☆ ✓ など）
    0,
};
// 日本語の範囲（よく使う漢字だけ）に入っていないが、エディタの文字に使っている漢字。足りない字はここへ足す
constexpr const char* kExtraKanji = "掴閾";

/**
 * @brief 日本語と記号の文字の範囲をまとめて返す（フォントの画像を作り終えるまで残すので、作った範囲は覚えておく）
 * @note DX12 を古い形の ImGui_ImplDX12_Init で初期化しているので、ImGui は使う文字をその場で読み込まず、
 *       ここで決めた範囲の文字だけを最初に読み込む。範囲に無い文字は ? になる
 */
const ImWchar* JapaneseAndSymbolGlyphRanges(ImFontAtlas* atlas) {
    static ImVector<ImWchar> sRanges;
    if (sRanges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(atlas->GetGlyphRangesJapanese());
        builder.AddRanges(kSymbolGlyphRanges);
        builder.AddText(kExtraKanji);
        builder.BuildRanges(&sRanges);
    }
    return sRanges.Data;
}
#endif

}

/** @brief ImGuiの初期化処理コンテキストの作成やDX12・Win32への紐づけを行う */
void ImGuiManager::Initialize([[maybe_unused]] WinApp* winApp, [[maybe_unused]] DirectXCommon* dxCommon) {
#ifdef USE_IMGUI

    // ImGui生成
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    // ウィンドウはタイトルバーをつかんだ時だけ動かす（中身のクリックやドラッグでずれないように）
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    // 日本語フォント読み込み（Windows 標準の游ゴシック）
    // ※ Win32Init より前に登録しないとアトラスに反映されない
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(
        "C:/Windows/Fonts/YuGothM.ttc",
        kJapaneseFontSize,
        nullptr,
        JapaneseAndSymbolGlyphRanges(io.Fonts));

    // Win32初期化
    ImGui_ImplWin32_Init(winApp->GetHwnd());

    // DirectX12初期化
    uint32_t index = SrvManager::GetInstance()->Allocate();

    ImGui_ImplDX12_Init(
        dxCommon->GetDevice(),
        dxCommon->GetBufferCount(),
        dxCommon->GetBackBufferFormat(),
        SrvManager::GetInstance()->GetSrvDescriptorHeap(), // ヒープを渡す
        SrvManager::GetInstance()->GetCPUDescriptorHandle(index), // 確保したCPUハンドル
        SrvManager::GetInstance()->GetGPUDescriptorHandle(index) // 確保したGPUハンドル
    );

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

#endif
}

/** @brief ImGuiの新しいフレーム開始処理 */
void ImGuiManager::Begin() {
#ifdef USE_IMGUI

    // フレーム開始
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

#endif
}

/** @brief ImGuiのフレーム終了（UI構築完了）処理 */
void ImGuiManager::End() {
#ifdef USE_IMGUI

    // 内部コマンド生成
    ImGui::Render();

#endif
}

/** @brief 描かずにImGuiのフレームを閉じる（次の Begin の前に呼ぶ） */
void ImGuiManager::EndFrameWithoutRender() {
#ifdef USE_IMGUI

    ImGui::EndFrame();

#endif
}

/** @brief ImGuiの実際の描画コマンドを積む */
void ImGuiManager::Draw(DirectXCommon* dxCommon) {
#ifdef USE_IMGUI

    // コマンドリストをdxCommonから取得する
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    ID3D12DescriptorHeap* ppHeaps[] = { SrvManager::GetInstance()->GetSrvDescriptorHeap() };
    commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // 描画コマンドを発行
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

#endif
}

/** @brief ImGuiの終了処理とリソース解放 */
void ImGuiManager::Finalize() {
#ifdef USE_IMGUI

    // 終了処理
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

#endif
}
} // namespace engine::graphics

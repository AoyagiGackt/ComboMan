/**
 * @file StageEditorPlaytest.cpp
 * @brief ステージエディタのテストプレイで、被弾・やられた位置と区間ごとにいた時間を記録し、保存して編集画面に重ねる
 */
#ifdef USE_IMGUI
#include "StageEditorPlaytest.h"
#include "StageEditor.h"
#include "DiagnosticsDraw.h"
#include "JsonHelper.h"
#include "RunData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace engine::game::stagePlaytest {
namespace {
constexpr const char* kPlaytestDirectory = "Resources/Playtest"; // 記録を置くフォルダ
constexpr float kSectionWidth = 10.0f; // いた時間を数える区間の横幅（ワールド単位）
constexpr float kBandHeight = 16.0f; // ビューの下に出す帯の高さ（ピクセル）
constexpr float kHitPointRadius = 4.0f; // 被弾の点の半径（ピクセル）
constexpr float kDefeatPointRadius = 6.0f; // やられた点の半径（被弾より大きく目立たせる）
constexpr float kHoverRadius = 7.0f; // 点に乗ったとみなすマウスとの距離（ピクセル）
constexpr int kBandRed = 255; // 帯の色の赤
constexpr int kBandGreen = 155; // 帯の色の緑
constexpr int kMinAlpha = 48; // いた時間が一番短い区間の帯の濃さ
constexpr int kAlphaRange = 176; // 一番長い区間までに足す濃さ
constexpr size_t kMinRunsForAdvice = 3; // 気づきを言い切るのに要るテストの回数
constexpr int kManyDefeats = 2; // この回数以上やられた区間を知らせる
constexpr int kManyHits = 5; // この回数以上攻撃を受けた区間を知らせる
constexpr float kSlowRatio = 2.0f; // 区間の平均のこの倍以上いた区間を知らせる
constexpr size_t kMinSectionsForSlow = 3; // 長くいすぎを比べるのに要る区間の数
constexpr size_t kMaxInsights = 5; // 一度に出す気づきの数
constexpr float kDefaultFocusY = 2.0f; // 点の無い区間へカメラを寄せる時の高さ
constexpr float kHalf = 0.5f; // 区間の真ん中を出す割合
/** @brief 一回の被弾または敗北とその時刻 */
struct Point {
    Vector3 position;
    float seconds;
    bool defeat;
};
/** @brief 一回のテストで通った区間と出来事 */
struct Run {
    float seconds = 0;
    std::map<int, float> sections;
    std::vector<Point> points;
};
/** @brief レベルごとに保存するテストの一覧 */
struct History {
    std::vector<Run> runs;
    float sectionWidth = kSectionWidth;
    bool clearFailed = false;
    bool dirty = false;
    bool readable = true;
};
/** @brief レベルを切り替えても未保存の記録を保持する */
std::map<std::string, History> sHistories;
const StageEditor* sOwner = nullptr;
std::string sRecordingPath;
const StageEditor* sObservedEditor = nullptr;
bool sDefeated = false;
int sPreviousHp = 0;
bool sShow = true;

/** @brief レベル名から記録の保存先を組み立てる */
std::string PathFor(const StageEditor& editor) {
    auto name = std::filesystem::path(editor.GetLevelPath()).filename();
    name.replace_extension(".json");
    return (std::filesystem::path(kPlaytestDirectory) / name).string();
}

/** @brief 保存済みの区間と出来事を読み戻す */
History Load(const std::string& path) {
    History history;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        history.readable = !error;
        return history;
    }
    try {
        const auto root = JsonHelper::Load(path);
        history.sectionWidth = root.at("sectionWidth").get<float>();
        if (!std::isfinite(history.sectionWidth) || history.sectionWidth <= 0) {
            history.readable = false;
            return history;
        }
        const auto& runs = root.at("runs").get_ref<const nlohmann::json::array_t&>();
        for (const auto& saved : runs) {
            Run run;
            saved.at("seconds").get_to(run.seconds);
            saved.at("sections").get_to(run.sections);
            for (const auto& p : saved.at("points").get_ref<const nlohmann::json::array_t&>()) {
                run.points.push_back({ { p.at("x").get<float>(), p.at("y").get<float>(), p.at("z").get<float>() },
                    p.at("seconds").get<float>(), p.at("defeat").get<bool>() });
            }
            history.runs.push_back(std::move(run));
        }
    } catch (const nlohmann::json::exception&) {
        // 読めない記録を空の一覧で上書きしない
        history.runs.clear();
        history.readable = false;
    }
    return history;
}

/** @brief 初めて開くレベルだけ記録を読み込む */
History& HistoryFor(const std::string& path) {
    auto [entry, inserted] = sHistories.try_emplace(path);
    if (inserted) {
        entry->second = Load(path);
    }
    return entry->second;
}

/** @brief 完了した回を含む一覧を一時ファイル経由で保存する */
void Save(const std::string& path, History& history) {
    auto runs = nlohmann::json::array();
    for (const Run& run : history.runs) {
        auto points = nlohmann::json::array();
        for (const Point& p : run.points) {
            points.push_back({ { "x", p.position.x }, { "y", p.position.y }, { "z", p.position.z },
                { "seconds", p.seconds }, { "defeat", p.defeat } });
        }
        runs.push_back({ { "seconds", run.seconds }, { "sections", run.sections }, { "points", std::move(points) } });
    }
    history.dirty = !JsonHelper::Save(path, { { "runCount", history.runs.size() }, { "sectionWidth", history.sectionWidth },
        { "runs", std::move(runs) } });
}

/** @brief 記録中の回に時刻と位置を追加する */
void AddPoint(const Vector3& position, bool defeat) {
    auto& run = sHistories.at(sRecordingPath).runs.back();
    run.points.push_back({ position, run.seconds, defeat });
}

/** @brief 記録から分かる直す所の気づき1件（文と、見るボタンでカメラを寄せる所） */
struct Insight {
    int priority; ///< 大きいほど先に出す（やられた回数など）
    std::string text; ///< 普通の言葉の気づき
    Vector3 focus; ///< 見るボタンでカメラを寄せる所
};

/** @brief 区間の番号から、その区間の横の範囲を「x 20〜30」の形の文にする */
std::string SectionRangeText(int section, float width) {
    const int from = static_cast<int>(std::floor(static_cast<float>(section) * width));
    const int to = static_cast<int>(std::floor(static_cast<float>(section + 1) * width));
    return "x " + std::to_string(from) + "〜" + std::to_string(to);
}

/** @brief 記録を区間ごとに数え、やられた所・攻撃を受けた所・長くいすぎた所の気づきを、目立つ順に返す */
std::vector<Insight> CollectInsights(const History& history) {
    // 区間ごとのやられた回数・受けた回数・いた秒数・点の高さの合計
    std::map<int, int> defeats;
    std::map<int, int> hits;
    std::map<int, float> seconds;
    std::map<int, float> heightSum;
    std::map<int, int> heightCount;
    for (const Run& run : history.runs) {
        for (const auto& [section, time] : run.sections) {
            seconds[section] += time;
        }
        for (const Point& point : run.points) {
            const int section = static_cast<int>(std::floor(point.position.x / history.sectionWidth));
            ++(point.defeat ? defeats : hits)[section];
            heightSum[section] += point.position.y;
            ++heightCount[section];
        }
    }
    // 区間の真ん中（点があればその高さ）へカメラを寄せる
    const auto focusOf = [&](int section) {
        const float x = (static_cast<float>(section) + kHalf) * history.sectionWidth;
        const float y = heightCount[section] > 0 ? heightSum[section] / static_cast<float>(heightCount[section]) : kDefaultFocusY;
        return Vector3 { x, y, 0.0f };
    };
    std::vector<Insight> insights;
    for (const auto& [section, count] : defeats) {
        if (count >= kManyDefeats) {
            insights.push_back({ count * kManyHits, SectionRangeText(section, history.sectionWidth) + " で " + std::to_string(count)
                + " 回やられています。敵を減らすか、手前に回復を置くと遊びやすくなります", focusOf(section) });
        }
    }
    for (const auto& [section, count] : hits) {
        if (count >= kManyHits) {
            insights.push_back({ count, SectionRangeText(section, history.sectionWidth) + " で " + std::to_string(count)
                + " 回攻撃を受けています。敵の並びがきつすぎないか見てみましょう", focusOf(section) });
        }
    }
    // 長くいすぎた区間は、いた区間の平均と比べる
    if (seconds.size() >= kMinSectionsForSlow) {
        float total = 0.0f;
        for (const auto& [section, time] : seconds) {
            total += time;
        }
        const float average = total / static_cast<float>(seconds.size());
        for (const auto& [section, time] : seconds) {
            if (average > 0.0f && time >= average * kSlowRatio) {
                char ratio[textBufferCapacity::kShortText];
                std::snprintf(ratio, sizeof(ratio), "%.1f", time / average);
                insights.push_back({ static_cast<int>(time / average), SectionRangeText(section, history.sectionWidth) + " に平均の "
                    + ratio + " 倍の時間いました。道に迷っているか、敵が固すぎるかもしれません", focusOf(section) });
            }
        }
    }
    // 目立つ順に並べ、出しすぎない
    std::stable_sort(insights.begin(), insights.end(), [](const Insight& a, const Insight& b) {
        return a.priority > b.priority;
    });
    if (insights.size() > kMaxInsights) {
        insights.resize(kMaxInsights);
    }
    return insights;
}

/** @brief 記録から分かる気づきを、見るボタン付きで折り返して出す */
void DrawInsights(StageEditor& editor, const History& history) {
    ImGui::SeparatorText("記録から分かること");
    if (history.runs.empty()) {
        ImGui::TextWrapped("まだ記録がありません。テストで遊ぶと、ここに直すとよい所が出ます。");
        return;
    }
    // 1回のテストの平均の長さ
    float total = 0.0f;
    for (const Run& run : history.runs) {
        total += run.seconds;
    }
    ImGui::TextWrapped("1回のテストは平均 %.0f 秒でした。", total / static_cast<float>(history.runs.size()));
    if (history.runs.size() < kMinRunsForAdvice) {
        ImGui::TextWrapped("記録が %zu 回だけなので、もう少し遊ぶと分かりやすくなります。", history.runs.size());
    }
    const std::vector<Insight> insights = CollectInsights(history);
    if (insights.empty()) {
        ImGui::TextWrapped("目立って難しい所や、長くいすぎる所はありませんでした。");
        return;
    }
    for (size_t index = 0; index < insights.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        // 見るボタンを先に置き、文はその横から折り返す
        if (ImGui::SmallButton("見る")) {
            editor.FocusEditorCamera(insights[index].focus);
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", insights[index].text.c_str());
        ImGui::PopID();
    }
}

/** @brief 区間の滞在時間を合計し、長いほど濃い帯をビューの下端に描く */
void DrawBands(const History& history, ImDrawList& draw) {
    std::map<int, float> totals;
    float longest = 0;
    for (const auto& run : history.runs) {
        for (const auto& [section, seconds] : run.sections) {
            totals[section] += seconds;
            longest = (std::max)(longest, totals[section]);
        }
    }
    if (longest <= 0) {
        return;
    }
    const float bottom = draw.GetClipRectMax().y;
    for (const auto& [section, seconds] : totals) {
        const float x = static_cast<float>(section) * history.sectionWidth;
        ImVec2 left, right;
        if (diagnosticsDraw::WorldToScreen({ x, 0, 0 }, left)
            && diagnosticsDraw::WorldToScreen({ x + history.sectionWidth, 0, 0 }, right)) {
            const int alpha = kMinAlpha + static_cast<int>(kAlphaRange * seconds / longest);
            draw.AddRectFilled({ (std::min)(left.x, right.x), bottom - kBandHeight },
                { (std::max)(left.x, right.x), bottom }, IM_COL32(kBandRed, kBandGreen, 0, alpha));
        }
    }
}
}

/** @brief 編集からテストに入る時に一回分を追加する */
void Begin(const StageEditor& editor) {
    if (sOwner || editor.GetLevelPath().empty()) {
        return;
    }
    auto path = PathFor(editor);
    auto& history = HistoryFor(path);
    if (history.readable) {
        history.runs.emplace_back();
        sRecordingPath = std::move(path);
        sOwner = &editor;
        sPreviousHp = RunData::GetInstance()->GetHp();
        if (sObservedEditor != &editor) {
            sDefeated = false;
        }
    }
}

/** @brief 終了したテストを元のレベル名で保存する */
void End(const StageEditor& editor) {
    if (sOwner == &editor) {
        Save(sRecordingPath, sHistories.at(sRecordingPath));
        sOwner = nullptr;
    }
}

/** @brief 横位置を負の側も含めて区間に分ける */
void Tick(const StageEditor& editor, const Vector3& position, float seconds) {
    if (sOwner == &editor && seconds > 0) {
        auto& history = sHistories.at(sRecordingPath);
        auto& run = history.runs.back();
        run.seconds += seconds;
        run.sections[static_cast<int>(std::floor(position.x / history.sectionWidth))] += seconds;
    }
}

/** @brief 命中処理からプレイヤーの現在位置を受け取る */
void Hit(const StageEditor& editor, const Vector3& position) {
    if (sOwner == &editor) {
        AddPoint(position, false);
        sPreviousHp = RunData::GetInstance()->GetHp();
    }
}

/** @brief グラフによる体力減少と、敗北状態への切り替わりを拾う */
void ObservePlayer(const StageEditor& editor, const Vector3& position, bool defeated) {
    if (sOwner == &editor) {
        const int hp = RunData::GetInstance()->GetHp();
        if (hp < sPreviousHp) {
            AddPoint(position, false);
        }
        sPreviousHp = hp;
        if (defeated && !sDefeated) {
            AddPoint(position, true);
        }
    }
    sDefeated = defeated;
    sObservedEditor = &editor;
}

/** @brief レベル単位の表示と保存操作をまとめる */
void Controls(StageEditor& editor) {
    if (editor.GetLevelPath().empty()) {
        ImGui::TextUnformatted("記録にはレベル名が必要です。");
        return;
    }
    const auto path = PathFor(editor);
    auto& history = HistoryFor(path);
    ImGui::Checkbox("テスト記録を表示", &sShow);
    ImGui::Text("記録 %zu 回　黄は被弾、赤は敗北", history.runs.size());
    ImGui::TextWrapped("下の帯が濃いほど、その区間に長くいました。");
    DrawInsights(editor, history);
    ImGui::BeginDisabled(sOwner != nullptr);
    if (history.dirty) {
        ImGui::TextWrapped("記録を保存できませんでした。");
        if (ImGui::Button("保存をやり直す")) {
            Save(path, history);
        }
    }
    if (!history.readable) {
        ImGui::TextWrapped("記録を読み込めません。消去すると新しく記録できます。");
    }
    if (ImGui::Button("このレベルの記録を消す")) {
        history.clearFailed = false;
        ImGui::OpenPopup("記録の消去");
    }
    if (ImGui::BeginPopupModal("記録の消去", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("このレベルのテスト記録をすべて消しますか？");
        if (ImGui::Button("消す")) {
            History empty;
            Save(path, empty);
            if (!empty.dirty) {
                history = std::move(empty);
                ImGui::CloseCurrentPopup();
            } else {
                history.clearFailed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("取り消す")) {
            ImGui::CloseCurrentPopup();
        }
        if (history.clearFailed) {
            ImGui::TextUnformatted("消去できませんでした。");
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
}

/** @brief 中央ビューの切り抜き内に記録を重ね、重なった点も一覧で示す */
void Draw(const StageEditor& editor, bool hovered) {
    if (!sShow || editor.GetLevelPath().empty()) {
        return;
    }
    const auto& history = HistoryFor(PathFor(editor));
    auto& draw = *diagnosticsDraw::GetDrawList();
    DrawBands(history, draw);
    const ImVec2 mouse = ImGui::GetMousePos();
    bool tip = false;
    for (size_t i = 0; i < history.runs.size(); ++i) {
        for (const auto& point : history.runs[i].points) {
            ImVec2 center;
            if (!diagnosticsDraw::WorldToScreen(point.position, center)) {
                continue;
            }
            draw.AddCircleFilled(center, point.defeat ? kDefeatPointRadius : kHitPointRadius,
                point.defeat ? diagnosticsDraw::kColorRed : diagnosticsDraw::kColorYellow);
            const float dx = mouse.x - center.x;
            const float dy = mouse.y - center.y;
            if (hovered && dx * dx + dy * dy <= kHoverRadius * kHoverRadius) {
                if (!tip) {
                    tip = ImGui::BeginTooltip();
                    if (!tip) {
                        continue;
                    }
                }
                ImGui::Text("%zu 回目　%.1f 秒　%s", i + 1, point.seconds, point.defeat ? "敗北" : "被弾");
            }
        }
    }
    if (tip) {
        ImGui::EndTooltip();
    }
}
}
#endif

/**
 * @file AssetReferenceScanner.cpp
 * @brief 設定に記載された素材の収集と収録漏れの確認
 */
#include "AssetReferenceScanner.h"
#include "AssetPack.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "ModelManager.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace engine::graphics {
namespace {
namespace fs = std::filesystem;
/** @brief 素材を探すフォルダ */
constexpr char kRoot[] = "Resources";
/** @brief 調べる拡張子と除くフォルダの設定 */
constexpr char kSettings[] = "Resources/Config/asset_check.json";
/** @brief pak の設定（元ファイルのまま入れない場所を読む） */
constexpr char kPackSettings[] = "Resources/Config/asset_pack.json";
/** @brief pak に入らなかった素材の一覧を書き出す先 */
constexpr char kReport[] = "log/asset_check.txt";
/** @brief エディタの自動保存のファイル名に入る印 */
constexpr char kAutoSaveMarker[] = ".autosave.";
/** @brief モデルだけを読み込む時に仮に貼るテクスチャ */
constexpr char kModelTexture[] = "Resources/white.png";

/** @brief 素材の種類（pak に入る時の名前の付け方が違う） */
enum class Kind { kTexture, kModel, kShader, kRaw };

/** @brief 素材の種類と、参照が書かれた設定の位置 */
struct Reference {
    Kind kind = Kind::kRaw;
    std::set<std::string> locations;
};
using References = std::map<std::string, Reference>;

/** @brief 拡張子と走査から除外するパスの設定 */
struct Rules {
    std::map<std::string, Kind> extensions;
    std::vector<std::string> excluded;
};

/** @brief 英字の大小をそろえて比較用の文字列を返す */
std::string Lower(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

/** @brief 区切りをそろえたパスをパック用の文字列にする */
std::string PathText(const fs::path& path) {
    return StringUtility::ConvertString(path.lexically_normal().generic_wstring());
}

/** @brief 設定の文字列配列を読み、項目が無いときは既定値を使う */
std::vector<std::string> Strings(const nlohmann::json& config, const char* key,
    const std::vector<std::string>& fallback = {}) {
    const auto it = config.find(key);
    if (it == config.end() || !it->is_array()) {
        return fallback;
    }
    std::vector<std::string> result;
    for (const auto& value : *it) {
        if (value.is_string() && !value.get_ref<const std::string&>().empty()) {
            result.push_back(value.get<std::string>());
        }
    }
    return result;
}

/** @brief 種類ごとの拡張子と二つの設定の除外指定をまとめる */
Rules LoadRules() {
    Rules rules;
    const auto settings = JsonHelper::Load(kSettings);
    const auto add = [&](Kind kind, const char* key, const std::vector<std::string>& defaults) {
        for (const auto& ext : Strings(settings, key, defaults)) {
            rules.extensions[Lower(ext)] = kind;
        }
    };
    add(Kind::kTexture, "textureExtensions", {".png", ".jpg", ".jpeg", ".dds", ".bmp", ".tga"});
    add(Kind::kModel, "modelExtensions", {".obj", ".gltf", ".glb", ".fbx"});
    add(Kind::kShader, "shaderExtensions", {".hlsl"});
    add(Kind::kRaw, "audioExtensions", {".wav", ".mp3"});
    rules.excluded = Strings(settings, "excludePrefixes", {"Resources/music/"});
    const auto raw = Strings(JsonHelper::Load(kPackSettings), "excludeRawPrefixes");
    rules.excluded.insert(rules.excluded.end(), raw.begin(), raw.end());
    for (auto& prefix : rules.excluded) {
        prefix = Lower(PathText(StringUtility::ConvertString(prefix)));
    }
    return rules;
}

/** @brief 除外する接頭辞と一致するかを調べる */
bool IsExcluded(const fs::path& path, const Rules& rules) {
    const std::string name = Lower(PathText(path));
    return std::any_of(rules.excluded.begin(), rules.excluded.end(), [&](const std::string& prefix) {
        return name.starts_with(prefix) || name + "/" == prefix;
    });
}

/** @brief 通常のファイルとして存在するかを調べる */
bool OnDisk(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

/** @brief 素材ルート、設定の隣の順で相対参照を解決する */
fs::path Resolve(const fs::path& path, const fs::path& json, const Rules& rules) {
    if (path.has_root_path() || *path.begin() == kRoot) {
        return path;
    }
    const fs::path rootCandidate = fs::path(kRoot) / path;
    const fs::path localCandidate = json.parent_path() / path;
    for (const auto& candidate : {rootCandidate, localCandidate}) {
        if (OnDisk(candidate)) {
            return candidate;
        }
    }
    // 未配置の参照にも除外フォルダを適用する。
    if (IsExcluded(rootCandidate, rules)) {
        return rootCandidate;
    }
    return path;
}

/** @brief 値の位置を JSON ポインタで保持し、同じ素材の参照をまとめる */
void Collect(const nlohmann::json& value, const nlohmann::json::json_pointer& pointer,
    const fs::path& file, const Rules& rules, References& result) {
    if (value.is_structured()) {
        for (const auto& item : value.items()) {
            Collect(item.value(), pointer / item.key(), file, rules, result);
        }
        return;
    }
    if (!value.is_string()) {
        return;
    }
    const fs::path path(StringUtility::ConvertString(value.get_ref<const std::string&>()));
    const auto found = rules.extensions.find(Lower(PathText(path.extension())));
    if (found == rules.extensions.end()) {
        return;
    }
    const fs::path resolved = Resolve(path.lexically_normal(), file, rules);
    if (!IsExcluded(resolved, rules)) {
        auto& reference = result[PathText(resolved)];
        reference.kind = found->second;
        reference.locations.insert(PathText(file) + "#" + pointer.to_string());
    }
}

/** @brief 素材フォルダの設定を走査する */
References Scan() {
    References references;
    const Rules rules = LoadRules();
    std::error_code error;
    fs::recursive_directory_iterator it(kRoot, fs::directory_options::skip_permission_denied, error), end;
    while (!error && it != end) {
        const auto& file = it->path();
        if (IsExcluded(file, rules)) {
            it.disable_recursion_pending();
        } else if (Lower(PathText(file.extension())) == ".json" && OnDisk(file)
            && PathText(file).find(kAutoSaveMarker) == std::string::npos) {
            // エディタの自動保存は作業中の写しなので調べない（pak にも入らない）
            std::ifstream input(file);
            const auto document = nlohmann::json::parse(input, nullptr, false);
            if (document.is_discarded()) {
                Logger::LogWarning("設定を解析できませんでした: " + PathText(file));
            } else {
                Collect(document, nlohmann::json::json_pointer(), file, rules, references);
            }
        }
        it.increment(error);
    }
    if (error) {
        Logger::LogWarning("素材フォルダを走査できませんでした: " + error.message());
    }
    return references;
}

/** @brief 実際の記録キーで収録状態を調べる */
bool Packed(const AssetPack& pack, Kind kind, const std::string& path) {
    switch (kind) {
    case Kind::kTexture: return pack.Contains("tex:" + path);
    case Kind::kModel: return pack.Contains("model:" + path) || pack.Contains("skin:" + path);
    case Kind::kShader: return pack.ContainsShader(path);
    case Kind::kRaw: return pack.Contains("raw:" + path);
    }
    return false;
}

/** @brief パスと参照元の全箇所を一つの診断にまとめる */
std::string Describe(const std::string& path, const Reference& reference) {
    std::string text = path;
    for (const auto& location : reference.locations) {
        text += "\n  " + location;
    }
    return text;
}
} // namespace

/** @brief ディスクに存在しない参照を起動時に警告する */
void AssetReferenceScanner::CheckDisk() {
    std::error_code error;
    if (!fs::is_directory(kRoot, error)) {
        return;
    }
    for (const auto& [path, reference] : Scan()) {
        if (!OnDisk(StringUtility::ConvertString(path))) {
            Logger::LogWarning("素材が見つかりません: " + Describe(path, reference));
        }
    }
}

/** @brief シーンで未使用だった素材を取り込み、最後に未収録の参照を書き出す */
void AssetReferenceScanner::CompleteCook(AssetPack& pack, DirectXCommon* device, ModelCommon* modelCommon) {
    // 設定に書かれた素材のうち、全シーンを回してもまだ pak に入っていない物を集める
    auto missing = Scan();
    std::erase_if(missing, [&](const auto& item) {
        return Packed(pack, item.second.kind, item.first);
    });
    for (const auto& [path, reference] : missing) {
        Logger::LogInfo("追加確認する素材: " + Describe(path, reference));
    }
    // CI には GPU が無いので、読み込んで足すのはやめて一覧と警告だけにする
    const bool ci = GetEnvironmentVariableW(L"GITHUB_ACTIONS", nullptr, 0) != 0;
    if (!ci && device && modelCommon) {
        // モデルの読み込みに要る共通の設定は、モデルが1つでもある時だけ作る
        bool initialized = false;
        for (const auto& [path, reference] : missing) {
            if (!OnDisk(StringUtility::ConvertString(path))) {
                continue;
            }
            try {
                if (reference.kind == Kind::kTexture) {
                    TextureManager::GetInstance()->LoadTexture(path);
                } else if (reference.kind == Kind::kModel) {
                    if (!initialized) {
                        modelCommon->Initialize(device);
                        initialized = true;
                    }
                    ModelManager::GetInstance()->GetOrLoad(modelCommon, path, kModelTexture);
                }
            } catch (const std::exception& failure) {
                Logger::LogWarning("素材の追加に失敗しました: " + path + " / " + failure.what());
            }
        }
        TextureManager::GetInstance()->FlushUploads();
    }

    // 読み込んだ素材が使う別の素材も入るので、入ったかどうかはここでもう一度調べる
    std::error_code error;
    fs::create_directories(fs::path(kReport).parent_path(), error);
    std::ofstream report(kReport, std::ios::trunc);
    for (const auto& [path, reference] : missing) {
        if (!Packed(pack, reference.kind, path)) {
            const std::string message = "パックに未収録: " + Describe(path, reference);
            Logger::LogWarning(message);
            report << message << '\n';
        }
    }
    report.close();
    if (!report) {
        Logger::LogWarning("素材の確認結果を書き出せませんでした: " + std::string(kReport));
    }
}
} // namespace engine::graphics

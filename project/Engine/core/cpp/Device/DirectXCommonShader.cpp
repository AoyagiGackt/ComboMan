/**
 * @file DirectXCommonShader.cpp
 * @brief シェーダーのコンパイル（pak のバイトコードの再利用・ファイルからの読み直し・元のファイルの記録）の実装
 * @note クラスは DirectXCommon のまま、シェーダーの処理だけをこのファイルに分けている
 */
#include "DirectXCommon.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "Logger.h"
#include "StringUtility.h"
#include <format>
#include <string>
#include <vector>

namespace engine {
namespace {
/** @brief キャッシュと pak のキーにする、ファイルとプロファイルの組み合わせ */
std::wstring MakeShaderCacheKey(const std::wstring& filePath, const wchar_t* profile) {
    return filePath + L"|" + profile;
}
} // namespace

/** @brief シェーダーをコンパイルする（同じファイルとプロファイルの結果はキャッシュから返す） */
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShader(const std::wstring& filePath, const wchar_t* profile) {
    // 同じファイル・同じプロファイルを一度コンパイルしていれば、その結果を使い回す
    const std::wstring cacheKey = MakeShaderCacheKey(filePath, profile);
    {
        std::scoped_lock lock(shaderCacheMutex_);
        const auto cached = shaderCache_.find(cacheKey);
        if (cached != shaderCache_.end()) {
            return cached->second;
        }
    }

    // pakにコンパイル済みのバイトコードがあれば、DXCでのコンパイルを省く
    const std::string packKey = "shader:" + StringUtility::ConvertString(cacheKey);
    std::vector<uint8_t> packed;
    if (AssetPack::GetInstance()->Read(packKey, packed)) {
        Microsoft::WRL::ComPtr<IDxcBlobEncoding> packedBlob;
        if (SUCCEEDED(dxcUtils_->CreateBlob(packed.data(), static_cast<UINT32>(packed.size()), DXC_CP_ACP, &packedBlob))) {
            Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
            packedBlob.As(&shaderBlob);
            RememberShader(cacheKey, filePath, profile, shaderBlob);
            return shaderBlob;
        }
    }

    // ファイルからコンパイルする（起動の時に失敗したら先へ進めないので止める）
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = CompileShaderSource(filePath, profile);
    ENGINE_ASSERT(shaderBlob);

    // pak を作っている最中なら、次回からコンパイルを省けるようバイトコードを記録する
    if (AssetPack::GetInstance()->IsCooking()) {
        const auto* begin = static_cast<const uint8_t*>(shaderBlob->GetBufferPointer());
        AssetPack::GetInstance()->Record(packKey, std::vector<uint8_t>(begin, begin + shaderBlob->GetBufferSize()));
    }

    // 次に同じシェーダーを頼まれた時のためにキャッシュへ入れる
    RememberShader(cacheKey, filePath, profile, shaderBlob);
    return shaderBlob;
}

/** @brief pak とキャッシュを通さずにファイルからコンパイルし直し、成功したらキャッシュを置き換える */
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShaderFromSource(const std::wstring& filePath, const wchar_t* profile) {
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = CompileShaderSource(filePath, profile);
    if (!shaderBlob) {
        return nullptr;
    }
    RememberShader(MakeShaderCacheKey(filePath, profile), filePath, profile, shaderBlob);
    return shaderBlob;
}

/** @brief バイトコードの先頭のアドレスから、元のファイルとプロファイルを引く */
bool DirectXCommon::FindShaderSource(const void* bytecode, std::wstring& outPath, std::wstring& outProfile) const {
    std::scoped_lock lock(shaderCacheMutex_);
    const auto found = shaderSources_.find(bytecode);
    if (found == shaderSources_.end()) {
        return false;
    }
    outPath = found->second.path;
    outProfile = found->second.profile;
    return true;
}

/** @brief コンパイルの結果をキャッシュへ入れ、バイトコードから元のファイルを引けるようにする */
void DirectXCommon::RememberShader(const std::wstring& cacheKey, const std::wstring& filePath, const wchar_t* profile,
    const Microsoft::WRL::ComPtr<IDxcBlob>& shaderBlob) {
    std::scoped_lock lock(shaderCacheMutex_);
    // 置き換える前の結果は解放されてアドレスが使い回されるかもしれないので、逆引きの表からも外す
    const auto previous = shaderCache_.find(cacheKey);
    if (previous != shaderCache_.end() && previous->second) {
        shaderSources_.erase(previous->second->GetBufferPointer());
    }
    shaderCache_[cacheKey] = shaderBlob;
    shaderSources_[shaderBlob->GetBufferPointer()] = { filePath, profile };
}

/** @brief ファイルを読んでコンパイルする（読めない・エラーや警告が出た時はログに出して nullptr を返す） */
Microsoft::WRL::ComPtr<IDxcBlob> DirectXCommon::CompileShaderSource(const std::wstring& filePath, const wchar_t* profile) {
    // hlslファイルを読む
    Logger::Log(StringUtility::ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}", filePath, profile)));

    Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
    HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    if (FAILED(hr)) {
        Logger::LogError(StringUtility::ConvertString(std::format(L"シェーダーのファイルを読めませんでした: {}", filePath)));
        return nullptr;
    }

    // 読んだソースを UTF-8 としてコンパイラへ渡す
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // Compileする
    // 入口は main、行優先の行列で出す。
    // デバッグ版はシェーダーの中を追えるようデバッグ情報を埋め込んで最適化なし、
    // それ以外の版はデバッグ情報を付けず一番強い最適化をかける
#ifdef _DEBUG
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E",
        L"main",
        L"-T",
        profile,
        L"-Zi",
        L"-Qembed_debug",
        L"-Od",
        L"-Zpr",
    };
#else
    LPCWSTR arguments[] = {
        filePath.c_str(),
        L"-E",
        L"main",
        L"-T",
        profile,
        L"-O3",
        L"-Zpr",
    };
#endif

    Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
    hr = dxcCompiler_->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler_.Get(),
        IID_PPV_ARGS(&shaderResult));
    if (FAILED(hr)) {
        Logger::LogError(StringUtility::ConvertString(std::format(L"シェーダーのコンパイルを始められませんでした: {}", filePath)));
        return nullptr;
    }

    // 警告・エラー確認（どちらも出たら失敗として扱う）
    Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        Logger::LogError(shaderError->GetStringPointer());
        return nullptr;
    }

    // 結果を受け取る
    Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (FAILED(hr) || !shaderBlob) {
        Logger::LogError(StringUtility::ConvertString(std::format(L"シェーダーの結果を受け取れませんでした: {}", filePath)));
        return nullptr;
    }

    Logger::Log(StringUtility::ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}", filePath, profile)));
    return shaderBlob;
}

} // namespace engine

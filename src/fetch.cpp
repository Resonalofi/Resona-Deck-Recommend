#include "fetch.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <chrono>
#include <httplib.h>
#endif

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("failed to open " + path.string());
    const auto size = file.tellg();
    if (size < 0)
        throw std::runtime_error("failed to determine size of " + path.string());
    std::string data(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(data.data(), size);
    if (!file)
        throw std::runtime_error("failed to read " + path.string());
    return data;
}

#ifdef _WIN32

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET value = nullptr) : value(value) {}
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    ~WinHttpHandle() {
        if (value != nullptr)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const { return value; }

private:
    HINTERNET value;
};

std::wstring toWide(const std::string& value) {
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0
    );
    if (size == 0)
        throw std::runtime_error("invalid UTF-8 URL");
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        wide.data(), size
    );
    return wide;
}

std::string download(const std::string& url) {
    auto wideUrl = toWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
        throw std::runtime_error("invalid URL: " + url);

    const WinHttpHandle session(WinHttpOpen(
        L"Resona-Deck-Recommend/2.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
    ));
    if (!session)
        throw std::runtime_error("WinHttpOpen failed");

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const WinHttpHandle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    if (!connection)
        throw std::runtime_error("WinHttpConnect failed");

    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength != 0)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    const WinHttpHandle request(WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags
    ));
    if (!request)
        throw std::runtime_error("WinHttpOpenRequest failed");

    WinHttpSetTimeouts(request, 10000, 10000, 10000, 60000);
    if (!WinHttpSendRequest(
            request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0
        ) || !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("HTTP request failed: " + url);

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX
        ))
        throw std::runtime_error("failed to read HTTP status: " + url);
    if (status < 200 || status >= 300)
        throw std::runtime_error("HTTP " + std::to_string(status) + ": " + url);

    std::string data;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            throw std::runtime_error("failed to read HTTP response: " + url);
        if (available == 0)
            break;
        const auto offset = data.size();
        data.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, data.data() + offset, available, &read))
            throw std::runtime_error("failed to read HTTP response: " + url);
        data.resize(offset + read);
    }
    return data;
}

#else

std::string download(const std::string& url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        throw std::runtime_error("invalid URL: " + url);
    const auto pathStart = url.find('/', schemeEnd + 3);
    const auto origin = url.substr(0, pathStart);
    const auto path = pathStart == std::string::npos ? std::string("/") : url.substr(pathStart);

    httplib::Client client(origin);
    client.set_follow_location(true);
    client.set_connection_timeout(std::chrono::seconds(10));
    client.set_read_timeout(std::chrono::seconds(60));
    const auto response = client.Get(path, {{"User-Agent", "Resona-Deck-Recommend/2.0"}});
    if (!response)
        throw std::runtime_error(
            "HTTP request failed: " + std::string(httplib::to_string(response.error()))
        );
    if (response->status < 200 || response->status >= 300)
        throw std::runtime_error("HTTP " + std::to_string(response->status) + ": " + url);
    return response->body;
}

#endif

}

std::string fetchWithFallback(const FetchSource& source, const std::string& name) {
    const auto path = source.location / (name + ".json");
    std::string localError;
    try {
        return readFile(path);
    }
    catch (const std::exception& error) {
        localError = error.what();
    }

    for (const auto& base : source.fallback) {
        try {
            const auto url = base + (base.ends_with('/') ? "" : "/") + name + ".json";
            auto data = download(url);
            std::filesystem::create_directories(source.location);
            const auto temporary = path.string() + ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                if (!file)
                    throw std::runtime_error("failed to write " + temporary);
            }
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            std::filesystem::rename(temporary, path);
            return data;
        }
        catch (const std::exception&) {
        }
    }
    throw std::runtime_error(localError);
}

#include "update_check.h"

#include <winhttp.h>

#include <cctype>
#include <ctime>
#include <format>
#include <mutex>
#include <vector>
#include <string>
#include <string_view>
#include <thread>

static constexpr wchar_t kDownloadUrl[] =
	L"https://buildbot.nefarius.at/builds/MultiPadTester/latest/MultiPadTester.zip";

namespace
{
constexpr wchar_t kJsonHost[] = L"buildbot.nefarius.at";
constexpr wchar_t kJsonPath[] =
	L"/builds/MultiPadTester/latest/.MultiPadTester/.MultiPadTester.exe.json";
constexpr DWORD kResolveMs = 10'000;
constexpr DWORD kConnectMs = 10'000;
constexpr DWORD kSendMs = 30'000;
constexpr DWORD kReceiveMs = 30'000;

constexpr int64_t kSuppressSeconds = 24 * 60 * 60;

struct VersionQuad
{
	uint16_t a = 0, b = 0, c = 0, d = 0;
};

std::mutex g_resultMutex;
std::string g_pendingLocal;
std::string g_pendingRemote;
bool g_havePending = false;

bool ParseVersionString(std::string_view s, VersionQuad& out)
{
	out = {};
	int part = 0;
	uint32_t cur = 0;
	for (char ch : s)
	{
		if (ch == '.')
		{
			if (part >= 4)
				return false;
			if (part == 0)
				out.a = static_cast<uint16_t>(cur);
			else if (part == 1)
				out.b = static_cast<uint16_t>(cur);
			else if (part == 2)
				out.c = static_cast<uint16_t>(cur);
			++part;
			cur = 0;
			continue;
		}
		if (ch < '0' || ch > '9')
			return false;
		cur = cur * 10u + static_cast<uint32_t>(ch - '0');
		if (cur > 65535u)
			return false;
	}
	if (part == 0)
		out.a = static_cast<uint16_t>(cur);
	else if (part == 1)
		out.b = static_cast<uint16_t>(cur);
	else if (part == 2)
		out.c = static_cast<uint16_t>(cur);
	else if (part == 3)
		out.d = static_cast<uint16_t>(cur);
	else
		return false;
	return true;
}

int CompareVersionQuad(const VersionQuad& l, const VersionQuad& r)
{
	if (l.a != r.a)
		return (l.a < r.a) ? -1 : 1;
	if (l.b != r.b)
		return (l.b < r.b) ? -1 : 1;
	if (l.c != r.c)
		return (l.c < r.c) ? -1 : 1;
	if (l.d != r.d)
		return (l.d < r.d) ? -1 : 1;
	return 0;
}

bool GetLocalExeVersion(VersionQuad& quad, std::string& displayUtf8)
{
	wchar_t path[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
		return false;
	DWORD handle = 0;
	const DWORD verSize = GetFileVersionInfoSizeW(path, &handle);
	if (verSize == 0)
		return false;
	std::vector<BYTE> buf(verSize);
	if (!GetFileVersionInfoW(path, 0, verSize, buf.data()))
		return false;
	VS_FIXEDFILEINFO* ffi = nullptr;
	UINT ffiLen = 0;
	if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&ffi), &ffiLen) || !ffi ||
	    ffiLen < sizeof(VS_FIXEDFILEINFO))
		return false;
	const uint32_t ms = ffi->dwFileVersionMS;
	const uint32_t ls = ffi->dwFileVersionLS;
	quad.a = HIWORD(ms);
	quad.b = LOWORD(ms);
	quad.c = HIWORD(ls);
	quad.d = LOWORD(ls);
	displayUtf8 = std::format("{}.{}.{}.{}", quad.a, quad.b, quad.c, quad.d);
	return true;
}

bool ExtractJsonFileVersion(std::string_view json, std::string& outVer)
{
	outVer.clear();
	const std::string_view key = "\"FileVersion\"";
	const size_t pos = json.find(key);
	if (pos == std::string_view::npos)
		return false;
	size_t i = pos + key.size();
	while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
		++i;
	if (i >= json.size() || json[i] != ':')
		return false;
	++i;
	while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
		++i;
	if (i >= json.size() || json[i] != '"')
		return false;
	++i;
	const size_t start = i;
	while (i < json.size() && json[i] != '"')
		++i;
	if (i >= json.size())
		return false;
	outVer.assign(json.data() + start, i - start);
	return !outVer.empty();
}

bool HttpGetUtf8(const wchar_t* host, INTERNET_PORT port, const wchar_t* path, std::string& bodyOut)
{
	bodyOut.clear();
	HINTERNET hSession = WinHttpOpen(
		L"MultiPadTester/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);
	if (!hSession)
		return false;
	auto closeSession = [hSession]() { WinHttpCloseHandle(hSession); };
	(void)WinHttpSetTimeouts(hSession, static_cast<int>(kResolveMs), static_cast<int>(kConnectMs),
	                         static_cast<int>(kSendMs), static_cast<int>(kReceiveMs));

	HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
	if (!hConnect)
	{
		closeSession();
		return false;
	}
	HINTERNET hRequest = WinHttpOpenRequest(
		hConnect,
		L"GET",
		path,
		nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		WINHTTP_FLAG_SECURE);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		closeSession();
		return false;
	}

	const BOOL sent = WinHttpSendRequest(
		hRequest,
		WINHTTP_NO_ADDITIONAL_HEADERS,
		0,
		WINHTTP_NO_REQUEST_DATA,
		0,
		0,
		0);
	if (!sent)
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		closeSession();
		return false;
	}
	if (!WinHttpReceiveResponse(hRequest, nullptr))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		closeSession();
		return false;
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!WinHttpQueryHeaders(
		    hRequest,
		    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		    WINHTTP_HEADER_NAME_BY_INDEX,
		    &status,
		    &statusSize,
		    WINHTTP_NO_HEADER_INDEX) ||
	    status != 200)
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		closeSession();
		return false;
	}

	for (;;)
	{
		char buf[8192];
		DWORD read = 0;
		if (!WinHttpReadData(hRequest, buf, sizeof(buf), &read))
		{
			bodyOut.clear();
			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			closeSession();
			return false;
		}
		if (read == 0)
			break;
		bodyOut.append(buf, read);
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	closeSession();
	return !bodyOut.empty();
}

void RunUpdateCheck(HWND notifyHwnd, std::atomic<HWND>* hwndSlot, int64_t dismissedUnix)
{
	const std::time_t now = std::time(nullptr);
	if (dismissedUnix > 0)
	{
		const int64_t elapsed = static_cast<int64_t>(now) - dismissedUnix;
		if (elapsed >= 0 && elapsed < kSuppressSeconds)
			return;
	}

	std::string json;
	if (!HttpGetUtf8(kJsonHost, INTERNET_DEFAULT_HTTPS_PORT, kJsonPath, json))
		return;

	std::string remoteVerStr;
	if (!ExtractJsonFileVersion(json, remoteVerStr))
		return;

	VersionQuad remote{};
	if (!ParseVersionString(remoteVerStr, remote))
		return;

	VersionQuad local{};
	std::string localDisplay;
	if (!GetLocalExeVersion(local, localDisplay))
		return;

	if (CompareVersionQuad(remote, local) <= 0)
		return;

	const HWND slotHwnd = hwndSlot ? hwndSlot->load(std::memory_order_acquire) : notifyHwnd;
	if (slotHwnd != notifyHwnd || !IsWindow(notifyHwnd))
		return;

	{
		std::lock_guard lock(g_resultMutex);
		g_pendingLocal = std::move(localDisplay);
		g_pendingRemote = std::move(remoteVerStr);
		g_havePending = true;
	}

	const HWND slot2 = hwndSlot ? hwndSlot->load(std::memory_order_acquire) : notifyHwnd;
	if (slot2 != notifyHwnd || !IsWindow(notifyHwnd))
	{
		std::lock_guard lock(g_resultMutex);
		g_havePending = false;
		g_pendingLocal.clear();
		g_pendingRemote.clear();
		return;
	}

	PostMessageW(notifyHwnd, WM_UPDATE_CHECK_READY, 0, 0);
}
}  // namespace

const wchar_t* UpdateCheck_GetLatestDownloadUrlW()
{
	return kDownloadUrl;
}

void StartBackgroundUpdateCheck(HWND notifyHwnd, std::atomic<HWND>* hwndSlot, int64_t dismissedUnix)
{
	std::thread([notifyHwnd, hwndSlot, dismissedUnix]() {
		RunUpdateCheck(notifyHwnd, hwndSlot, dismissedUnix);
	}).detach();
}

bool UpdateCheck_PopResultForUi(std::string& localVersionOut, std::string& remoteVersionOut)
{
	std::lock_guard lock(g_resultMutex);
	if (!g_havePending)
		return false;
	localVersionOut = g_pendingLocal;
	remoteVersionOut = g_pendingRemote;
	g_havePending = false;
	g_pendingLocal.clear();
	g_pendingRemote.clear();
	return true;
}

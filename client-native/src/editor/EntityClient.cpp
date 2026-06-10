// WinHTTP implementation — isolated here so windows.h macros don't
// contaminate glaze template instantiation in headers.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "EntityClient.hpp"
#include <stdexcept>
#include <string>

namespace editor {

std::wstring entityToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

std::string entityHttpRequest(const std::wstring& method,
                              const std::wstring& path,
                              const std::string&  body)
{
  HINTERNET session = WinHttpOpen(L"SnookEditor/1.0",
    WINHTTP_ACCESS_TYPE_NO_PROXY,
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) throw std::runtime_error("WinHttpOpen failed");

  // Short timeouts (ms): resolve, connect, send, receive. This is a localhost
  // dev-convenience API — if the server is down or its event loop is wedged we
  // must fail fast, NOT block the app's main thread on WinHTTP's ~30s defaults
  // (which froze startup for tens of seconds). Callers fall back gracefully
  // (the client uses server-supplied init defs; the editor shows the error).
  WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000);

  HINTERNET conn = WinHttpConnect(session, L"localhost", 8080, 0);
  if (!conn) {
    WinHttpCloseHandle(session);
    throw std::runtime_error("WinHttpConnect failed");
  }

  HINTERNET req = WinHttpOpenRequest(conn, method.c_str(), path.c_str(),
    nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  if (!req) {
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    throw std::runtime_error("WinHttpOpenRequest failed");
  }

  const wchar_t* headers  = body.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                          : L"Content-Type: application/json\r\n";
  DWORD           hdrLen  = body.empty() ? 0 : static_cast<DWORD>(-1);
  const void*     pBody   = body.empty() ? WINHTTP_NO_REQUEST_DATA
                                         : static_cast<const void*>(body.data());
  DWORD           bodyLen = body.empty() ? 0 : static_cast<DWORD>(body.size());

  BOOL ok = WinHttpSendRequest(req, headers, hdrLen,
                               const_cast<void*>(pBody), bodyLen, bodyLen, 0);
  if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    throw std::runtime_error("HTTP request failed (could not reach localhost:8080)");
  }

  // Query the HTTP status code so server-side errors (4xx/5xx) are not
  // silently treated as success.
  DWORD statusCode = 0;
  DWORD scSize = sizeof(statusCode);
  WinHttpQueryHeaders(req,
      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &scSize, WINHTTP_NO_HEADER_INDEX);

  std::string result;
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
    std::string chunk(static_cast<size_t>(avail), '\0');
    DWORD read = 0;
    WinHttpReadData(req, chunk.data(), avail, &read);
    result.append(chunk.data(), read);
  }

  WinHttpCloseHandle(req);
  WinHttpCloseHandle(conn);
  WinHttpCloseHandle(session);

  if (statusCode >= 400) {
    std::string msg = "HTTP " + std::to_string(statusCode);
    if (!result.empty()) msg += ": " + result;
    throw std::runtime_error(msg);
  }
  return result;
}

}  // namespace editor

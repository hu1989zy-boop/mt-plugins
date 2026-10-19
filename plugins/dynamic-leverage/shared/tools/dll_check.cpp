// Build-time native Windows loader check. No Server API or account calls.
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    std::wcerr << L"Usage: DLLCheck absolute-dll-path required-export [required-export ...]\n";
    return 2;
  }
  if (!std::filesystem::path(argv[1]).is_absolute()) {
    std::wcerr << L"DLL path must be absolute\n";
    return 2;
  }
  // Resolve only alongside the DLL and in System32, independent of cwd/PATH.
  const HMODULE module = LoadLibraryExW(argv[1], nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) {
    std::wcerr << L"LoadLibraryExW failed: Windows error " << GetLastError() << L" for " << argv[1] << L'\n';
    return 1;
  }
  bool valid = true;
  for (int i = 2; i < argc; ++i) {
    std::string symbol;
    for (const wchar_t c : std::wstring(argv[i])) {
      if (c > 0x7f) { valid = false; break; }
      symbol.push_back(static_cast<char>(c));
    }
    if (!valid || !GetProcAddress(module, symbol.c_str())) {
      std::wcerr << L"Missing export: " << argv[i] << L'\n';
      valid = false;
      break;
    }
  }
  if (!FreeLibrary(module)) {
    std::wcerr << L"FreeLibrary failed: Windows error " << GetLastError() << L'\n';
    valid = false;
  }
  if (valid) std::wcout << L"DLL load/unload and required exports passed: " << argv[1] << L'\n';
  return valid ? 0 : 1;
}

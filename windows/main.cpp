#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "tradecopier/config.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
using tradecopier::CopierConfig;
using tradecopier::Id;
using tradecopier::RuleConfig;
using tradecopier::SizingMode;

constexpr wchar_t kWindowClass[] = L"MT5TradeCopierLocalConfigEditor";
constexpr wchar_t kTitle[] = L"MT5 跟单配置管理 — 本地离线编辑器";
constexpr UINT kRestoreSelection = WM_APP + 1;
enum ControlId {
    NewFile = 100, OpenFile, SaveFile, SaveAsFile, GlobalEnabled, RuleList,
    NewRule, DeleteRule, SourceLogin, FollowerLogin, Sizing, Parameter,
    RuleEnabled, ApplyRule, ResetRule
};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    bool loading = false;
    bool dirty = false;
    bool form_dirty = false;
    int editing_index = -1;
    CopierConfig config;
    std::wstring path;
    HWND banner = nullptr, path_label = nullptr, status = nullptr;
    HWND new_file = nullptr, open_file = nullptr, save_file = nullptr, save_as = nullptr;
    HWND global_enabled = nullptr, list = nullptr, new_rule = nullptr, delete_rule = nullptr;
    HWND editor_label = nullptr, id_label = nullptr, source_label = nullptr, follower_label = nullptr;
    HWND mode_label = nullptr, parameter_label = nullptr;
    HWND source = nullptr, follower = nullptr, mode = nullptr, parameter = nullptr;
    HWND rule_enabled = nullptr, apply_rule = nullptr, reset_rule = nullptr;
    HWND formula = nullptr, policies = nullptr;
} g;

int Scaled(int value) { return MulDiv(value, g.dpi, 96); }

std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"无法显示错误文本（不是有效 UTF-8）";
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), output.data(), count);
    return output;
}

std::wstring WindowsError(DWORD code) {
    wchar_t* raw = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                      reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    std::wstring message = count && raw ? std::wstring(raw, count) : L"未知 Windows 错误";
    if (raw) LocalFree(raw);
    return message + L"（错误 " + std::to_wstring(code) + L"）";
}

void Error(const std::wstring& message) {
    MessageBoxW(g.window, message.c_str(), L"配置未完成", MB_OK | MB_ICONERROR);
}

std::wstring ReadText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring output(static_cast<std::size_t>(length) + 1, L'\0');
    const int read = GetWindowTextW(control, output.data(), length + 1);
    output.resize(static_cast<std::size_t>(read));
    return output;
}

bool ReadId(HWND control, Id& output) {
    const std::wstring text = ReadText(control);
    if (text.empty()) return false;
    Id parsed = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        const Id digit = static_cast<Id>(c - L'0');
        if (parsed > (std::numeric_limits<Id>::max() - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed == 0) return false;
    output = parsed;
    return true;
}

const wchar_t* ModeName(SizingMode mode) {
    switch (mode) {
    case SizingMode::LotRatio: return L"手数比例";
    case SizingMode::EquityRatio: return L"净值比例";
    case SizingMode::FixedLots: return L"固定手数";
    }
    return L"未知模式";
}

int ModeIndex(SizingMode mode) {
    switch (mode) {
    case SizingMode::LotRatio: return 0;
    case SizingMode::EquityRatio: return 1;
    case SizingMode::FixedLots: return 2;
    }
    return 0;
}

SizingMode SelectedMode() {
    const LRESULT index = SendMessageW(g.mode, CB_GETCURSEL, 0, 0);
    if (index == 1) return SizingMode::EquityRatio;
    if (index == 2) return SizingMode::FixedLots;
    return SizingMode::LotRatio;
}

void UpdateFormula() {
    const SizingMode mode = SelectedMode();
    const wchar_t* label = L"倍数";
    const wchar_t* text = L"手数 = 源开仓手数 × 倍数。实际手数还须通过目标品种最小量 / 最大量 / 步长检查。";
    if (mode == SizingMode::EquityRatio) {
        label = L"净值系数";
        text = L"手数 = 源手数 × 跟单净值 ÷ 源净值 × 系数。源成交后、提交跟单前查询并冻结双方 USD 净值。";
    } else if (mode == SizingMode::FixedLots) {
        label = L"固定手数（手）";
        text = L"每笔新源交易使用固定手数。部分平仓按源本次平仓占原剩余手数的比例，减少跟单实际剩余手数。";
    }
    SetWindowTextW(g.parameter_label, label);
    SetWindowTextW(g.formula, text);
}

void RefreshStatus() {
    std::wstring title = kTitle;
    if (g.dirty || g.form_dirty) title += L" *";
    SetWindowTextW(g.window, title.c_str());
    const std::wstring path = g.path.empty() ? L"文件：未命名配置" : L"文件：" + g.path;
    SetWindowTextW(g.path_label, path.c_str());
    std::wstring status = L"离线配置 · 未发送服务器  |  关系 " +
                          std::to_wstring(g.config.relationships.size()) + L" 条  |  ";
    status += g.form_dirty ? L"关系字段尚未更新到列表" : (g.dirty ? L"文件有未保存修改" : L"本地文件无待保存修改");
    status += g.config.enabled ? L"  |  全局草稿：启用" : L"  |  全局草稿：停用";
    SetWindowTextW(g.status, status.c_str());
}

void MarkFormDirty() {
    if (g.loading) return;
    g.form_dirty = true;
    RefreshStatus();
}

void LoadForm(int index) {
    g.loading = true;
    g.editing_index = index;
    const bool existing = index >= 0 && static_cast<std::size_t>(index) < g.config.relationships.size();
    RuleConfig empty;
    empty.parameter = "1";
    const RuleConfig& rule = existing ? g.config.relationships[static_cast<std::size_t>(index)] : empty;
    SetWindowTextW(g.editor_label, existing ? L"编辑选中关系（修改后请更新到列表）" : L"新增关系（默认停用，加入列表后才会导出）");
    const std::wstring id = existing ? L"关系 ID：" + std::to_wstring(rule.id) : L"关系 ID：加入列表时自动生成";
    SetWindowTextW(g.id_label, id.c_str());
    SetWindowTextW(g.source, existing ? std::to_wstring(rule.source_login).c_str() : L"");
    SetWindowTextW(g.follower, existing ? std::to_wstring(rule.follower_login).c_str() : L"");
    SendMessageW(g.mode, CB_SETCURSEL, static_cast<WPARAM>(ModeIndex(rule.mode)), 0);
    SetWindowTextW(g.parameter, FromUtf8(rule.parameter).c_str());
    SendMessageW(g.rule_enabled, BM_SETCHECK, rule.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(g.delete_rule, existing ? TRUE : FALSE);
    g.form_dirty = false;
    g.loading = false;
    UpdateFormula();
    RefreshStatus();
}

void FillList(int selected) {
    g.loading = true;
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    for (std::size_t i = 0; i < g.config.relationships.size(); ++i) {
        const RuleConfig& rule = g.config.relationships[i];
        std::wstring values[] = {
            std::to_wstring(rule.id), rule.enabled ? L"启用（草稿）" : L"停用（草稿）",
            std::to_wstring(rule.source_login), std::to_wstring(rule.follower_login),
            ModeName(rule.mode), FromUtf8(rule.parameter)
        };
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = values[0].data();
        const int row = ListView_InsertItem(g.list, &item);
        for (int column = 1; column < 6; ++column)
            ListView_SetItemText(g.list, row, column, values[column].data());
    }
    if (selected >= 0) {
        ListView_SetItemState(g.list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g.list, selected, FALSE);
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    g.loading = false;
    LoadForm(selected);
}

bool MayDiscardForm() {
    if (!g.form_dirty) return true;
    return MessageBoxW(g.window, L"关系字段尚未更新到列表。放弃这些字段修改吗？\n\n列表中已更新的配置会保留。",
                       L"未提交的关系编辑", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

Id FreshId() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    Id candidate = (static_cast<Id>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    if (candidate == 0) candidate = 1;
    const auto used = [](Id value) {
        return std::any_of(g.config.relationships.begin(), g.config.relationships.end(),
                           [value](const RuleConfig& rule) { return rule.id == value; });
    };
    while (used(candidate)) {
        candidate = candidate == std::numeric_limits<Id>::max() ? 1 : candidate + 1;
    }
    return candidate;
}

bool ApplyForm() {
    RuleConfig rule;
    if (!ReadId(g.source, rule.source_login) || !ReadId(g.follower, rule.follower_login)) {
        Error(L"源账号和跟单账号必须是大于零的十进制 64 位整数，不能包含空格或其他字符。");
        return false;
    }
    const std::wstring parameter = ReadText(g.parameter);
    if (parameter.empty() || std::any_of(parameter.begin(), parameter.end(), [](wchar_t c) {
            return (c < L'0' || c > L'9') && c != L'.';
        })) {
        Error(L"参数必须是正十进制数，例如 1、0.5 或 2.25。不能包含符号、空格或科学计数法。");
        return false;
    }
    for (wchar_t character : parameter) rule.parameter.push_back(static_cast<char>(character));
    rule.mode = SelectedMode();
    rule.enabled = SendMessageW(g.rule_enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool existing = g.editing_index >= 0 &&
                          static_cast<std::size_t>(g.editing_index) < g.config.relationships.size();
    bool changed_accounts = false;
    if (existing) {
        const RuleConfig& original = g.config.relationships[static_cast<std::size_t>(g.editing_index)];
        changed_accounts = original.source_login != rule.source_login || original.follower_login != rule.follower_login;
        rule.id = changed_accounts ? FreshId() : original.id;
        if (changed_accounts) rule.enabled = false;
    } else {
        rule.id = FreshId();
    }
    CopierConfig candidate = g.config;
    int index = g.editing_index;
    if (existing) candidate.relationships[static_cast<std::size_t>(index)] = rule;
    else {
        index = static_cast<int>(candidate.relationships.size());
        candidate.relationships.push_back(std::move(rule));
    }
    std::string error;
    if (!tradecopier::ValidateConfig(candidate, error)) {
        Error(L"关系未更新。请检查账号组合、参数或循环跟单：\n\n" + FromUtf8(error));
        return false;
    }
    g.config = std::move(candidate);
    g.dirty = true;
    FillList(index);
    if (changed_accounts) {
        MessageBoxW(g.window, L"源账号或跟单账号已改变，已分配新的关系 ID，并将该关系设为停用。\n\n这只是本地配置编辑，没有向服务器发送任何操作。",
                    L"已创建新的关系标识", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

bool ChooseFile(bool save, std::wstring& path) {
    std::vector<wchar_t> buffer(32768, L'\0');
    const std::wstring initial = save ? (g.path.empty() ? L"tradecopier.json" : g.path) : L"";
    if (initial.size() >= buffer.size()) { Error(L"文件路径过长。"); return false; }
    std::copy(initial.begin(), initial.end(), buffer.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g.window;
    dialog.lpstrFilter = L"跟单配置 JSON (*.json)\0*.json\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"json";
    dialog.lpstrTitle = save ? L"保存本地配置（不会发送服务器）" : L"打开本地配置";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) {
        const DWORD error = CommDlgExtendedError();
        if (error != 0) Error(L"文件对话框失败，错误 " + std::to_wstring(error));
        return false;
    }
    path = buffer.data();
    return true;
}

bool ReadConfigFile(const std::wstring& path, CopierConfig& output) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) { Error(L"无法打开文件：\n" + WindowsError(GetLastError())); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        Error(L"无法读取文件大小：\n" + WindowsError(error));
        return false;
    }
    if (size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(tradecopier::kMaxConfigBytes)) {
        CloseHandle(file);
        Error(L"文件为空或超过 1 MiB 配置大小限制。");
        return false;
    }
    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL success = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    const DWORD error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!success || read != text.size()) {
        Error(L"文件读取不完整：\n" + WindowsError(success ? ERROR_HANDLE_EOF : error));
        return false;
    }
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::string parse_error;
    if (!tradecopier::ParseConfigJson(text, output, parse_error)) {
        Error(L"配置未打开，当前内容保留。\n\n" + FromUtf8(parse_error));
        return false;
    }
    return true;
}

bool WriteConfigFile(const std::wstring& path, const std::string& text) {
    // A uniquely-created sibling file keeps replacement on the same filesystem.
    // Never truncate the destination before the complete replacement is ready.
    std::wstring temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned int attempt = 0; attempt < 32; ++attempt) {
        temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            Error(L"无法创建保存用临时文件，原文件未改动：\n" + WindowsError(error));
            return false;
        }
    }
    if (file == INVALID_HANDLE_VALUE) { Error(L"无法创建唯一临时文件，原文件未改动。"); return false; }
    DWORD written = 0;
    bool success = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != FALSE;
    DWORD error = success ? ERROR_SUCCESS : GetLastError();
    if (success && written != text.size()) { success = false; error = ERROR_WRITE_FAULT; }
    if (success && !FlushFileBuffers(file)) { success = false; error = GetLastError(); }
    if (!CloseHandle(file) && success) { success = false; error = GetLastError(); }
    if (success && !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        success = false;
        error = GetLastError();
    }
    if (!success) {
        std::wstring message = L"保存失败，无法完成文件替换：\n" + WindowsError(error);
        if (!DeleteFileW(temporary.c_str())) {
            const DWORD cleanup_error = GetLastError();
            if (cleanup_error != ERROR_FILE_NOT_FOUND) {
                message += L"\n\n临时文件未能清理，可确认原文件完整后手工删除：\n" +
                           temporary + L"\n" + WindowsError(cleanup_error);
            }
        }
        Error(message);
        return false;
    }
    return true;
}

bool Save(bool save_as) {
    if (g.form_dirty) {
        const int decision = MessageBoxW(g.window, L"关系字段尚未更新到列表。先更新该关系，再保存整个文件吗？",
                                         L"保存前更新关系", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
        if (decision != IDYES || !ApplyForm()) return false;
    }
    std::string error;
    if (!tradecopier::ValidateConfig(g.config, error)) { Error(FromUtf8(error)); return false; }
    std::wstring path = g.path;
    if ((save_as || path.empty()) && !ChooseFile(true, path)) return false;
    const std::string json = tradecopier::WriteConfigJson(g.config);
    if (!WriteConfigFile(path, json)) return false;
    g.path = std::move(path);
    g.dirty = false;
    RefreshStatus();
    return true;
}

bool MayReplaceDocument() {
    if (!g.dirty && !g.form_dirty) return true;
    const int result = MessageBoxW(g.window, L"本地配置或关系字段有未保存修改。是否先保存？\n\n“是”：保存后继续；“否”：放弃修改；“取消”：留在当前配置。",
                                   L"未保存的配置", MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON3);
    if (result == IDYES) return Save(false);
    return result == IDNO;
}

void Open() {
    std::wstring path;
    if (!ChooseFile(false, path)) return;
    if (!MayReplaceDocument()) return;
    CopierConfig candidate;
    if (!ReadConfigFile(path, candidate)) return;
    g.config = std::move(candidate);
    g.path = std::move(path);
    g.dirty = false;
    SendMessageW(g.global_enabled, BM_SETCHECK, g.config.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    FillList(-1);
}

HWND Control(const wchar_t* class_name, const wchar_t* text, DWORD style, int id = 0, DWORD extended = 0) {
    HWND control = CreateWindowExW(extended, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                    0, 0, 1, 1, g.window,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.instance, nullptr);
    if (!control) throw std::runtime_error("CreateWindowExW failed for a child control");
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    return control;
}

void Move(HWND control, int x, int y, int width, int height) {
    MoveWindow(control, Scaled(x), Scaled(y), Scaled(width), Scaled(height), TRUE);
}

void Layout() {
    RECT client{};
    GetClientRect(g.window, &client);
    const int width = MulDiv(client.right, 96, g.dpi);
    const int height = MulDiv(client.bottom, 96, g.dpi);
    const int table_top = 117;
    const int table_bottom = std::max(table_top + 140, height - 286);
    Move(g.banner, 18, 16, width - 36, 24);
    Move(g.new_file, 18, 47, 90, 28);
    Move(g.open_file, 116, 47, 100, 28);
    Move(g.save_file, 224, 47, 90, 28);
    Move(g.save_as, 322, 47, 100, 28);
    Move(g.global_enabled, 452, 47, 315, 28);
    Move(g.path_label, 18, 87, width - 36, 23);
    Move(g.list, 18, table_top, width - 36, table_bottom - table_top);
    const int row = table_bottom + 10;
    Move(g.new_rule, 18, row, 100, 28);
    Move(g.delete_rule, 126, row, 100, 28);
    Move(g.id_label, 247, row + 5, width - 265, 24);
    Move(g.editor_label, 18, row + 43, width - 36, 23);
    const int field_y = row + 73;
    Move(g.source_label, 18, field_y, 180, 21);
    Move(g.follower_label, 219, field_y, 180, 21);
    Move(g.mode_label, 420, field_y, 160, 21);
    Move(g.parameter_label, 601, field_y, 160, 21);
    Move(g.source, 18, field_y + 24, 185, 28);
    Move(g.follower, 219, field_y + 24, 185, 28);
    Move(g.mode, 420, field_y + 24, 165, 140);
    Move(g.parameter, 601, field_y + 24, 155, 28);
    Move(g.rule_enabled, 780, field_y + 24, 220, 28);
    Move(g.apply_rule, 18, field_y + 62, 156, 30);
    Move(g.reset_rule, 184, field_y + 62, 105, 30);
    Move(g.formula, 306, field_y + 63, width - 324, 42);
    Move(g.policies, 18, height - 66, width - 36, 28);
    Move(g.status, 18, height - 30, width - 36, 23);
    const int columns[] = {196, 106, 174, 174, 120};
    int used = 0;
    for (int i = 0; i < 5; ++i) {
        ListView_SetColumnWidth(g.list, i, Scaled(columns[i]));
        used += columns[i];
    }
    ListView_SetColumnWidth(g.list, 5, Scaled(std::max(112, width - 60 - used)));
}

void CreateControls() {
    HDC dc = GetDC(g.window);
    if (dc) { g.dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(g.window, dc); }
    g.font = CreateFontW(-Scaled(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (!g.font) throw std::runtime_error("CreateFontW failed");
    g.banner = Control(L"STATIC", L"离线配置编辑器：只编辑本地 JSON，未连接 MT5，也未向服务器发送配置。", SS_LEFT);
    g.new_file = Control(L"BUTTON", L"新建文件", WS_TABSTOP | BS_PUSHBUTTON, NewFile);
    g.open_file = Control(L"BUTTON", L"打开 JSON", WS_TABSTOP | BS_PUSHBUTTON, OpenFile);
    g.save_file = Control(L"BUTTON", L"保存", WS_TABSTOP | BS_PUSHBUTTON, SaveFile);
    g.save_as = Control(L"BUTTON", L"另存为", WS_TABSTOP | BS_PUSHBUTTON, SaveAsFile);
    g.global_enabled = Control(L"BUTTON", L"全局启用（仅配置草稿）", WS_TABSTOP | BS_AUTOCHECKBOX, GlobalEnabled);
    g.path_label = Control(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS);
    g.list = Control(WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, RuleList);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const wchar_t* columns[] = {L"关系 ID", L"配置状态", L"信号源账号", L"跟单账号", L"手数方式", L"参数"};
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.fmt = LVCFMT_LEFT;
        column.cx = Scaled(150);
        column.pszText = const_cast<wchar_t*>(columns[i]);
        ListView_InsertColumn(g.list, i, &column);
    }
    g.new_rule = Control(L"BUTTON", L"新增关系", WS_TABSTOP | BS_PUSHBUTTON, NewRule);
    g.delete_rule = Control(L"BUTTON", L"删除关系", WS_TABSTOP | BS_PUSHBUTTON, DeleteRule);
    g.id_label = Control(L"STATIC", L"", SS_LEFT);
    g.editor_label = Control(L"STATIC", L"", SS_LEFT);
    g.source_label = Control(L"STATIC", L"信号源账号", SS_LEFT);
    g.follower_label = Control(L"STATIC", L"跟单账号", SS_LEFT);
    g.mode_label = Control(L"STATIC", L"手数方式", SS_LEFT);
    g.parameter_label = Control(L"STATIC", L"参数", SS_LEFT);
    g.source = Control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, SourceLogin, WS_EX_CLIENTEDGE);
    g.follower = Control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER, FollowerLogin, WS_EX_CLIENTEDGE);
    g.mode = Control(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, Sizing);
    g.parameter = Control(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Parameter, WS_EX_CLIENTEDGE);
    SendMessageW(g.source, EM_SETLIMITTEXT, 20, 0);
    SendMessageW(g.follower, EM_SETLIMITTEXT, 20, 0);
    SendMessageW(g.parameter, EM_SETLIMITTEXT, 64, 0);
    SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"手数比例"));
    SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"净值比例"));
    SendMessageW(g.mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"固定手数"));
    g.rule_enabled = Control(L"BUTTON", L"启用此关系（草稿）", WS_TABSTOP | BS_AUTOCHECKBOX, RuleEnabled);
    g.apply_rule = Control(L"BUTTON", L"更新 / 加入列表", WS_TABSTOP | BS_PUSHBUTTON, ApplyRule);
    g.reset_rule = Control(L"BUTTON", L"撤销字段编辑", WS_TABSTOP | BS_PUSHBUTTON, ResetRule);
    g.formula = Control(L"STATIC", L"", SS_LEFT);
    g.policies = Control(L"STATIC", L"固定策略：MT5 同服 · USD · 对冲 · 只跟新仓 · 不复制挂单 / SL / TP · 手动全平后停止该仓位跟随", SS_LEFT);
    g.status = Control(L"STATIC", L"", SS_LEFT);
    FillList(-1);
    Layout();
}

void Command(int id, int notification) {
    if (g.loading) return;
    if ((id == SourceLogin || id == FollowerLogin || id == Parameter) && notification == EN_CHANGE) {
        MarkFormDirty();
        return;
    }
    if (id == Sizing && notification == CBN_SELCHANGE) { UpdateFormula(); MarkFormDirty(); return; }
    if (notification != BN_CLICKED) return;
    switch (id) {
    case NewFile:
        if (!MayReplaceDocument()) break;
        g.config = {};
        g.path.clear();
        g.dirty = false;
        SendMessageW(g.global_enabled, BM_SETCHECK, BST_UNCHECKED, 0);
        FillList(-1);
        break;
    case OpenFile: Open(); break;
    case SaveFile: Save(false); break;
    case SaveAsFile: Save(true); break;
    case GlobalEnabled:
        g.config.enabled = SendMessageW(g.global_enabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.dirty = true;
        RefreshStatus();
        break;
    case RuleEnabled: MarkFormDirty(); break;
    case NewRule:
        if (MayDiscardForm()) { FillList(-1); SetFocus(g.source); }
        break;
    case ResetRule:
        if (MayDiscardForm()) LoadForm(g.editing_index);
        break;
    case ApplyRule: ApplyForm(); break;
    case DeleteRule:
        if (g.editing_index < 0) break;
        if (MessageBoxW(g.window, L"从本地配置中删除选中的关系及其未提交编辑吗？\n\n这不会操作服务器上的持仓。",
                        L"删除本地关系", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) break;
        g.config.relationships.erase(g.config.relationships.begin() + g.editing_index);
        g.dirty = true;
        FillList(-1);
        break;
    }
}

LRESULT HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g.window = window;
        CreateControls();
        return 0;
    case WM_SIZE:
        if (g.status && wparam != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = Scaled(1060);
        info->ptMinTrackSize.y = Scaled(690);
        return 0;
    }
    case WM_COMMAND:
        Command(LOWORD(wparam), HIWORD(wparam));
        return 0;
    case kRestoreSelection:
        // A list view may deselect the old row before asking to select a new one.
        // Restore the previous row after a cancelled field-discard prompt.
        g.loading = true;
        ListView_SetItemState(g.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        if (g.editing_index >= 0)
            ListView_SetItemState(g.list, g.editing_index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        g.loading = false;
        return 0;
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (!g.loading && header->hwndFrom == g.list) {
            auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
            if (header->code == LVN_ITEMCHANGING && (change->uChanged & LVIF_STATE) &&
                (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                if (!MayDiscardForm()) {
                    PostMessageW(window, kRestoreSelection, 0, 0);
                    return TRUE;
                }
                return FALSE;
            }
            if (header->code == LVN_ITEMCHANGED && (change->uChanged & LVIF_STATE) &&
                (change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED)) {
                LoadForm(change->iItem);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (MayReplaceDocument()) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (g.font) { DeleteObject(g.font); g.font = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try { return HandleMessage(window, message, wparam, lparam); }
    catch (const std::exception& exception) {
        Error(L"本地编辑器遇到错误：\n" + FromUtf8(exception.what()));
    } catch (...) { Error(L"本地编辑器遇到未知错误。"); }
    return message == WM_CREATE ? -1 : 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    g.instance = instance;
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    if (screen) { g.dpi = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(nullptr, screen); }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    if (!InitCommonControlsEx(&controls)) return 1;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) return 1;
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int available_width = work_area.right - work_area.left;
    const int available_height = work_area.bottom - work_area.top;
    // Fit the preferred initial size to the work area when it meets the minimum.
    // The layout requires at least 1060 x 690 logical pixels for all fields.
    const int initial_width = available_width > 0 ? std::min(Scaled(1180), available_width) : Scaled(1180);
    const int initial_height = available_height > 0 ? std::min(Scaled(790), available_height) : Scaled(790);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, kTitle, WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, initial_width, initial_height,
                                   nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

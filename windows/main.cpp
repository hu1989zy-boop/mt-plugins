#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>

#include "tradecopier/config.hpp"
#include "local_pipe.hpp"
#include "management_requests.hpp"
#include <future>
#include <chrono>

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
constexpr wchar_t kTitle[] = L"MT5 跟单管理";
enum class Page { General, Runtime, Information };
enum ControlId {
    NewFile = 100, OpenConfig, SaveFile, SaveAsFile, GlobalEnabled, RuleList,
    NewRule, DeleteRule, SourceLogin, FollowerLogin, Sizing, Parameter,
    RuleEnabled, ServerId, AppId, Connect,
    ReadActive, ApplyActive, PauseActive, ResumeActive, InspectActive, RuntimeLinks, RuntimeIntents,
    MainTabs, RuntimeTabs, EditRule, FileMenu, CloseManager, SymbolFilter, SourceSuffix, AutoSymbol
};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HFONT font = nullptr;
    HFONT heading_font = nullptr;
    int dpi = 96;
    bool loading = false;
    bool dirty = false;
    bool form_dirty = false;
    int editing_index = -1;
    CopierConfig config;
    std::wstring path;
    HWND tabs=nullptr, brand=nullptr, heading=nullptr, introduction=nullptr, file_menu=nullptr, close=nullptr;
    HWND path_label = nullptr, status = nullptr, apply_hint = nullptr;
    HWND global_enabled = nullptr, list = nullptr, new_rule = nullptr, edit_rule=nullptr, delete_rule = nullptr;
    HWND editor=nullptr, id_label = nullptr, source_label = nullptr, follower_label = nullptr;
    HWND mode_label = nullptr, parameter_label = nullptr;
    HWND source = nullptr, follower = nullptr, mode = nullptr, parameter = nullptr;
    HWND rule_enabled = nullptr;
    HWND symbol_mask=nullptr, source_suffix=nullptr, auto_symbol=nullptr;
    HWND formula = nullptr, policies = nullptr;
    std::vector<HWND> config_controls, runtime_controls, information_controls;
    bool collecting_config = false, collecting_runtime = false, collecting_information = false;
    Page page=Page::General;
    bool requests_page=false, rendering_runtime=false;
    HWND server_label=nullptr, server_id=nullptr, app_label=nullptr, app_id=nullptr, connect=nullptr;
    HWND connection=nullptr, connection_group=nullptr, connection_help=nullptr;
    HWND read_active=nullptr, apply_active=nullptr, pause_active=nullptr, resume_active=nullptr, inspect_active=nullptr;
    HWND summary=nullptr, runtime_paths=nullptr, runtime_links=nullptr, runtime_intents=nullptr, details=nullptr;
    HWND runtime_tabs=nullptr, runtime_hint=nullptr, details_label=nullptr, paths_label=nullptr, information_help=nullptr;
    tradecopier::management::Response live;
    tradecopier::management::Rows shown_links, shown_intents;
    bool connected=false;
    bool details_connection_error=false;
    Id connected_server=0, connected_app=0;
    ULONGLONG last_response=0, last_poll=0;
    std::future<tradecopier::management::Response> pending;
    tradecopier::manager::Requests requests;
    bool polling=false;
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

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if (n<=0) throw std::runtime_error("invalid symbol text");
    std::string result(static_cast<std::size_t>(n),'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),n,nullptr,nullptr))
        throw std::runtime_error("symbol text conversion failed");
    return result;
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
    MessageBoxW(g.editor ? g.editor : g.window, message.c_str(), L"管理操作未完成", MB_OK | MB_ICONERROR);
}

std::wstring ReadText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring output(static_cast<std::size_t>(length) + 1, L'\0');
    const int read = GetWindowTextW(control, output.data(), length + 1);
    output.resize(static_cast<std::size_t>(read));
    return output;
}
void SetTextIfChanged(HWND control, const std::wstring& text) {
    if (ReadText(control)!=text) SetWindowTextW(control,text.c_str());
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
    std::wstring status = std::to_wstring(g.config.relationships.size()) + L" 条关系   ·   ";
    status += g.dirty ? L"本地文件未保存" : L"本地文件无修改";
    if (g.connected && !g.form_dirty) {
        status += tradecopier::WriteConfigJson(g.config)==g.live.config_json ? L"   ·   与最近生效配置一致" : L"   ·   草稿与最近生效配置不同";
    }
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
    empty.auto_symbol=true;
    const RuleConfig& rule = existing ? g.config.relationships[static_cast<std::size_t>(index)] : empty;
    SetWindowTextW(g.editor, existing ? L"跟单关系 — 编辑" : L"跟单关系 — 添加");
    const std::wstring id = existing ? L"关系 ID：" + std::to_wstring(rule.id) : L"关系 ID：加入列表时自动生成";
    SetWindowTextW(g.id_label, id.c_str());
    SetWindowTextW(g.source, existing ? std::to_wstring(rule.source_login).c_str() : L"");
    SetWindowTextW(g.follower, existing ? std::to_wstring(rule.follower_login).c_str() : L"");
    SendMessageW(g.mode, CB_SETCURSEL, static_cast<WPARAM>(ModeIndex(rule.mode)), 0);
    SetWindowTextW(g.parameter, FromUtf8(rule.parameter).c_str());
    SendMessageW(g.rule_enabled, BM_SETCHECK, rule.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g.symbol_mask,FromUtf8(rule.symbol_mask).c_str());
    SetWindowTextW(g.source_suffix,FromUtf8(rule.source_suffix).c_str());
    SendMessageW(g.auto_symbol,BM_SETCHECK,rule.auto_symbol?BST_CHECKED:BST_UNCHECKED,0);
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
            std::to_wstring(rule.source_login), std::to_wstring(rule.follower_login),
            ModeName(rule.mode), FromUtf8(rule.parameter), FromUtf8(rule.symbol_mask),
            rule.auto_symbol?(L"自动匹配"+(rule.source_suffix.empty()?std::wstring{}:L"（源后缀 "+FromUtf8(rule.source_suffix)+L"）")):L"同名品种",
            rule.enabled ? L"启用" : L"停用",
            std::to_wstring(rule.id)
        };
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = values[0].data();
        const int row = ListView_InsertItem(g.list, &item);
        for (int column = 1; column < 8; ++column)
            ListView_SetItemText(g.list, row, column, values[column].data());
    }
    if (selected >= 0) {
        ListView_SetItemState(g.list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g.list, selected, FALSE);
    }
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    g.loading = false;
    g.editing_index=selected;
    EnableWindow(g.edit_rule,selected>=0);
    EnableWindow(g.delete_rule,selected>=0);
    RefreshStatus();
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
    rule.symbol_mask=ToUtf8(ReadText(g.symbol_mask));
    rule.source_suffix=ToUtf8(ReadText(g.source_suffix));
    rule.auto_symbol=SendMessageW(g.auto_symbol,BM_GETCHECK,0,0)==BST_CHECKED;
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
        MessageBoxW(g.editor, L"源账号或跟单账号已改变，已分配新的关系 ID，并将该关系设为停用。\n\n这只是本地配置编辑，没有向服务器发送任何操作。",
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
    const int result = MessageBoxW(g.window, L"配置草稿有未保存修改。是否先保存到文件？\n\n“是”：保存后继续；“否”：放弃修改；“取消”：留在当前配置。",
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

HWND Child(HWND parent, const wchar_t* class_name, const wchar_t* text, DWORD style,
           int id = 0, DWORD extended = 0) {
    HWND control=CreateWindowExW(extended,class_name,text,WS_CHILD|WS_VISIBLE|style,
        0,0,1,1,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g.instance,nullptr);
    if (!control) throw std::runtime_error("CreateWindowExW failed for a child control");
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(g.font),FALSE);
    return control;
}
HWND Control(const wchar_t* class_name, const wchar_t* text, DWORD style, int id = 0, DWORD extended = 0) {
    HWND control=Child(g.window,class_name,text,style,id,extended);
    if (g.collecting_config) g.config_controls.push_back(control);
    if (g.collecting_runtime) g.runtime_controls.push_back(control);
    if (g.collecting_information) g.information_controls.push_back(control);
    return control;
}
void Move(HWND control, int x, int y, int width, int height) {
    MoveWindow(control,Scaled(x),Scaled(y),Scaled(width),Scaled(height),TRUE);
}
void CreateManagementControls();
void ShowPage(Page page);
void ShowRuntimeTable();
void UpdateRuntimeButtons();
void PollManagement();
void StartManagement(tradecopier::management::Operation op, bool load_config=false, bool background=false);
void LoadActive();
void ApplyRuntime();
void InspectRuntime();

void AddColumn(HWND table, int index, const wchar_t* name, int width) {
    LVCOLUMNW column{};
    column.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
    column.fmt=LVCFMT_LEFT;
    column.cx=Scaled(width);
    column.pszText=const_cast<wchar_t*>(name);
    ListView_InsertColumn(table,index,&column);
}
void StyleTable(HWND table) {
    SetWindowTheme(table,L"Explorer",nullptr);
    ListView_SetExtendedListViewStyle(table,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);
}

// A native modal editor keeps unfinished fields out of the main draft. The
// dialog manager handles focus, Tab, Enter, Escape and disabling the owner.
INT_PTR CALLBACK RuleDialogProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try {
        switch (message) {
        case WM_INITDIALOG: {
            g.editor=window;
            const auto field=[window](const wchar_t* cls,const wchar_t* text,DWORD style,int id,
                                      int x,int y,int w,int h,DWORD ex=0) {
                HWND control=Child(window,cls,text,style,id,ex);
                Move(control,x,y,w,h); return control;
            };
            field(L"STATIC",L"设置账号、手数与品种。确定后写入草稿，应用配置后生效。",SS_LEFT,0,22,20,460,32);
            g.source_label=field(L"STATIC",L"信号源账号：",SS_RIGHT,0,20,65,108,22);
            g.source=field(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,SourceLogin,140,61,338,25,WS_EX_CLIENTEDGE);
            g.follower_label=field(L"STATIC",L"跟单账号：",SS_RIGHT,0,20,101,108,22);
            g.follower=field(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,FollowerLogin,140,97,338,25,WS_EX_CLIENTEDGE);
            g.mode_label=field(L"STATIC",L"手数方式：",SS_RIGHT,0,20,137,108,22);
            g.mode=field(L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,Sizing,140,133,338,140);
            g.parameter_label=field(L"STATIC",L"倍数",SS_RIGHT,0,20,173,108,22);
            g.parameter=field(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,Parameter,140,169,338,25,WS_EX_CLIENTEDGE);
            field(L"STATIC",L"源品种掩码：",SS_RIGHT,0,20,209,108,22);
            g.symbol_mask=field(L"EDIT",L"*",WS_TABSTOP|ES_AUTOHSCROLL,SymbolFilter,140,205,338,25,WS_EX_CLIENTEDGE);
            field(L"STATIC",L"源后缀（可选）：",SS_RIGHT,0,20,245,108,22);
            g.source_suffix=field(L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,SourceSuffix,140,241,338,25,WS_EX_CLIENTEDGE);
            g.auto_symbol=field(L"BUTTON",L"自动匹配目标品种后缀（多个候选时报冲突）",WS_TABSTOP|BS_AUTOCHECKBOX,AutoSymbol,140,277,338,24);
            g.rule_enabled=field(L"BUTTON",L"启用此关系",WS_TABSTOP|BS_AUTOCHECKBOX,RuleEnabled,140,309,338,24);
            field(L"STATIC",L"掩码：* 全部，XAUUSD* 黄金，*,!EUR* 排除 EUR。\r\n源后缀留空时按服务器基础品种/货币信息识别；也可填 p、.p。\r\n仅选择该账号可交易且合约一致的唯一品种；未勾选则按同名跟随。",SS_LEFT,0,22,347,456,60);
            g.formula=field(L"STATIC",L"",SS_LEFT,0,22,413,456,48);
            g.id_label=field(L"STATIC",L"",SS_LEFT,0,22,467,456,24);
            field(L"STATIC",L"",SS_ETCHEDHORZ,0,0,498,500,1);
            field(L"BUTTON",L"确定",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,294,514,88,28);
            field(L"BUTTON",L"取消",WS_TABSTOP|BS_PUSHBUTTON,IDCANCEL,390,514,88,28);
            SendMessageW(g.symbol_mask,EM_SETLIMITTEXT,4096,0);
            SendMessageW(g.source_suffix,EM_SETLIMITTEXT,64,0);
            SendMessageW(g.source,EM_SETLIMITTEXT,20,0);
            SendMessageW(g.follower,EM_SETLIMITTEXT,20,0);
            SendMessageW(g.parameter,EM_SETLIMITTEXT,64,0);
            for (const wchar_t* name:{L"手数比例",L"净值比例",L"固定手数"})
                SendMessageW(g.mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            RECT bounds{0,0,Scaled(500),Scaled(562)};
            AdjustWindowRectEx(&bounds,static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,
                               static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE)));
            RECT owner{}; GetWindowRect(g.window,&owner);
            MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(g.window,MONITOR_DEFAULTTONEAREST),&monitor);
            const int w=bounds.right-bounds.left, h=bounds.bottom-bounds.top;
            const int x=std::max(static_cast<int>(monitor.rcWork.left),
                std::min(static_cast<int>((owner.left+owner.right-w)/2),static_cast<int>(monitor.rcWork.right)-w));
            const int y=std::max(static_cast<int>(monitor.rcWork.top),
                std::min(static_cast<int>((owner.top+owner.bottom-h)/2),static_cast<int>(monitor.rcWork.bottom)-h));
            SetWindowPos(window,nullptr,x,y,w,h,SWP_NOZORDER);
            LoadForm(static_cast<int>(lparam));
            SetFocus(g.source);
            return FALSE;
        }
        case WM_COMMAND:
            if (LOWORD(wparam)==IDOK) {
                if (ApplyForm()) EndDialog(window,IDOK);
                return TRUE;
            }
            if (LOWORD(wparam)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
            if (!g.loading) {
                if (LOWORD(wparam)==Sizing && HIWORD(wparam)==CBN_SELCHANGE) { UpdateFormula(); MarkFormDirty(); }
                else if ((LOWORD(wparam)==SourceLogin || LOWORD(wparam)==FollowerLogin || LOWORD(wparam)==Parameter ||
                         LOWORD(wparam)==SymbolFilter || LOWORD(wparam)==SourceSuffix) &&
                         HIWORD(wparam)==EN_CHANGE) MarkFormDirty();
                else if ((LOWORD(wparam)==RuleEnabled || LOWORD(wparam)==AutoSymbol) && HIWORD(wparam)==BN_CLICKED) MarkFormDirty();
            }
            return TRUE;
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wparam),GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CLOSE: EndDialog(window,IDCANCEL); return TRUE;
        }
    } catch (const std::exception& error) { Error(FromUtf8(error.what())); if (message==WM_INITDIALOG) EndDialog(window,-1); }
      catch (...) { Error(L"无法编辑关系。"); if (message==WM_INITDIALOG) EndDialog(window,-1); }
    return FALSE;
}
void EditRelationship(int index) {
    if (g.requests.loading_config() || g.editor) return;
    struct alignas(DWORD) EmptyDialog { DLGTEMPLATE dialog; WORD menu=0, window_class=0, title=0; } resource{};
    static_assert(offsetof(EmptyDialog,menu)==sizeof(DLGTEMPLATE),"Dialog template fields must be contiguous");
    resource.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    resource.dialog.dwExtendedStyle=WS_EX_CONTROLPARENT;
    resource.dialog.cx=320; resource.dialog.cy=250;
    const auto result=DialogBoxIndirectParamW(g.instance,&resource.dialog,g.window,RuleDialogProcedure,index);
    g.editor=nullptr;
    g.source=g.follower=g.mode=g.parameter=g.rule_enabled=nullptr;
    g.symbol_mask=g.source_suffix=g.auto_symbol=nullptr;
    g.source_label=g.follower_label=g.mode_label=g.parameter_label=g.formula=g.id_label=nullptr;
    g.form_dirty=false;
    g.editing_index=ListView_GetNextItem(g.list,-1,LVNI_SELECTED);
    RefreshStatus(); UpdateRuntimeButtons();
    if (result==-1) Error(L"无法打开关系编辑窗口。");
}

void Layout() {
    RECT client{}; GetClientRect(g.window,&client);
    const int width=MulDiv(client.right,96,g.dpi), height=MulDiv(client.bottom,96,g.dpi);
    Move(g.tabs,12,12,width-24,height-72);
    Move(g.brand,32,58,40,42);
    Move(g.heading,86,58,240,25);
    Move(g.introduction,86,86,350,22);
    Move(g.summary,width-374,59,338,47);
    // General: small command column, table, and two quiet draft/apply notes.
    Move(g.global_enabled,32,124,240,26);
    Move(g.read_active,width-302,123,126,28);
    Move(g.file_menu,width-164,123,128,28);
    Move(g.path_label,32,163,width-64,22);
    Move(g.new_rule,32,198,80,28); Move(g.edit_rule,32,234,80,28); Move(g.delete_rule,32,270,80,28);
    Move(g.list,126,194,width-162,height-358);
    Move(g.status,126,height-153,width-162,23);
    Move(g.apply_hint,32,height-119,width-64,44);
    // Runtime: show a single table, keeping operation feedback available below.
    Move(g.pause_active,32,123,126,28); Move(g.resume_active,168,123,126,28);
    Move(g.inspect_active,width-216,123,180,28);
    Move(g.runtime_hint,32,158,width-68,42);
    Move(g.runtime_tabs,32,204,width-68,height-411);
    Move(g.runtime_links,42,237,width-88,height-454);
    Move(g.runtime_intents,42,237,width-88,height-454);
    Move(g.details_label,32,height-195,width-68,22);
    Move(g.details,32,height-170,width-68,94);
    // Information: connection IDs and technical details stay out of daily work.
    Move(g.connection_group,32,122,width-68,106);
    Move(g.server_label,48,151,78,22); Move(g.server_id,128,146,100,26);
    Move(g.app_label,256,151,70,22); Move(g.app_id,330,146,100,26);
    Move(g.connection_help,48,190,width-100,23);
    Move(g.paths_label,32,246,width-68,22);
    Move(g.runtime_paths,32,274,width-68,std::max(96,height-490));
    Move(g.policies,32,height-201,width-68,49);
    Move(g.information_help,32,height-140,width-68,62);
    Move(g.connect,18,height-44,112,28);
    Move(g.connection,144,height-40,width-384,26);
    Move(g.apply_active,width-222,height-44,106,28);
    Move(g.close,width-106,height-44,88,28);
}

void CreateControls() {
    NONCLIENTMETRICSW metrics{}; metrics.cbSize=sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0)) {
        metrics.lfMessageFont.lfHeight=-Scaled(14);
        lstrcpyW(metrics.lfMessageFont.lfFaceName,L"Microsoft YaHei UI");
    }
    g.font=CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfWeight=FW_SEMIBOLD;
    metrics.lfMessageFont.lfHeight=-Scaled(17);
    g.heading_font=CreateFontIndirectW(&metrics.lfMessageFont);
    if (!g.font || !g.heading_font) throw std::runtime_error("CreateFontIndirectW failed");
    g.tabs=Control(WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS,MainTabs);
    const wchar_t* pages[]={L"常规",L"运行状态",L"信息"};
    for (int i=0;i<3;++i) { TCITEMW item{}; item.mask=TCIF_TEXT;
        item.pszText=const_cast<wchar_t*>(pages[i]); TabCtrl_InsertItem(g.tabs,i,&item); }
    TabCtrl_SetPadding(g.tabs,Scaled(14),Scaled(5));
    g.brand=Control(L"STATIC",L"",SS_OWNERDRAW);
    g.heading=Control(L"STATIC",L"MT5 跟单管理",SS_LEFT);
    SendMessageW(g.heading,WM_SETFONT,reinterpret_cast<WPARAM>(g.heading_font),FALSE);
    g.introduction=Control(L"STATIC",L"配置账号与手数规则",SS_LEFT);
    g.summary=Control(L"STATIC",L"未连接\r\n连接后查看执行器状态",SS_LEFT);
    g.collecting_config=true;
    g.global_enabled=Control(L"BUTTON",L"启用跟单配置（草稿）",WS_TABSTOP|BS_AUTOCHECKBOX,GlobalEnabled);
    g.read_active=Control(L"BUTTON",L"读取生效配置",WS_TABSTOP|BS_PUSHBUTTON,ReadActive);
    g.file_menu=Control(L"BUTTON",L"配置文件 ▾",WS_TABSTOP|BS_PUSHBUTTON,FileMenu);
    g.path_label=Control(L"STATIC",L"",SS_LEFT|SS_PATHELLIPSIS);
    g.new_rule=Control(L"BUTTON",L"添加…",WS_TABSTOP|BS_PUSHBUTTON,NewRule);
    g.edit_rule=Control(L"BUTTON",L"编辑…",WS_TABSTOP|BS_PUSHBUTTON,EditRule);
    g.delete_rule=Control(L"BUTTON",L"删除",WS_TABSTOP|BS_PUSHBUTTON,DeleteRule);
    g.list=Control(WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,RuleList);
    StyleTable(g.list);
    const wchar_t* columns[]={L"信号源账号",L"跟单账号",L"手数方式",L"参数",L"源品种掩码",L"目标品种",L"状态",L"关系 ID"};
    const int widths[]={94,94,92,60,130,180,62,182};
    for (int i=0;i<8;++i) AddColumn(g.list,i,columns[i],widths[i]);
    g.status=Control(L"STATIC",L"",SS_LEFT|SS_ENDELLIPSIS);
    g.apply_hint=Control(L"STATIC",L"保存只写入本地文件；连接执行器后可应用配置。",SS_LEFT);
    g.collecting_config=false;
    CreateManagementControls();
    g.close=Control(L"BUTTON",L"关闭",WS_TABSTOP|BS_PUSHBUTTON,CloseManager);
    FillList(-1); Layout(); ShowPage(Page::General);
    SetTimer(g.window,1,250,nullptr);
}

void RuntimeTable(HWND table, const tradecopier::management::Rows& rows, int columns) {
    const int top=ListView_GetTopIndex(table);
    const int selected=ListView_GetNextItem(table,-1,LVNI_SELECTED);
    wchar_t old[80]{}, old_position[80]{};
    if (selected>=0) ListView_GetItemText(table,selected,0,old,80);
    if (selected>=0 && table==g.runtime_links) ListView_GetItemText(table,selected,3,old_position,80);
    SendMessageW(table,WM_SETREDRAW,FALSE,0); ListView_DeleteAllItems(table);
    for (std::size_t n=0;n<rows.size();++n) {
        if (rows[n].size()!=static_cast<std::size_t>(columns)) continue;
        auto value=FromUtf8(rows[n][0]); LVITEMW item{}; item.mask=LVIF_TEXT;
        item.iItem=static_cast<int>(n); item.pszText=value.data();
        const int row=ListView_InsertItem(table,&item);
        for (int c=1;c<columns;++c) { value=FromUtf8(rows[n][static_cast<std::size_t>(c)]); ListView_SetItemText(table,row,c,value.data()); }
        if (FromUtf8(rows[n][0])==old && (table!=g.runtime_links || FromUtf8(rows[n][3])==old_position))
            ListView_SetItemState(table,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    }
    RECT row_rect{};
    if (ListView_GetItemRect(table,0,&row_rect,LVIR_BOUNDS))
        ListView_Scroll(table,0,std::min(top,std::max(0,ListView_GetItemCount(table)-1))*(row_rect.bottom-row_rect.top));
    SendMessageW(table,WM_SETREDRAW,TRUE,0); InvalidateRect(table,nullptr,TRUE);
}
void ShowRuntimeTable() {
    const bool runtime=g.page==Page::Runtime;
    ShowWindow(g.runtime_links,runtime && !g.requests_page?SW_SHOW:SW_HIDE);
    ShowWindow(g.runtime_intents,runtime && g.requests_page?SW_SHOW:SW_HIDE);
    ShowWindow(g.inspect_active,runtime && g.requests_page?SW_SHOW:SW_HIDE);
}
void ShowPage(Page page) {
    g.page=page;
    TabCtrl_SetCurSel(g.tabs,static_cast<int>(page));
    for (HWND control:g.config_controls) ShowWindow(control,page==Page::General?SW_SHOW:SW_HIDE);
    for (HWND control:g.runtime_controls) ShowWindow(control,page==Page::Runtime?SW_SHOW:SW_HIDE);
    for (HWND control:g.information_controls) ShowWindow(control,page==Page::Information?SW_SHOW:SW_HIDE);
    const wchar_t* descriptions[]={L"配置账号与手数规则",L"查看仓位、交易请求与操作结果",L"连接设置、配置来源与跟单规则"};
    SetWindowTextW(g.introduction,descriptions[static_cast<int>(page)]);
    ShowRuntimeTable();
}
tradecopier::manager::Connection ManagementConnection() {
    return {g.connected && g.live.ok && GetTickCount64()-g.last_response<6000,
            g.connected_server,g.connected_app,g.live.session,g.live.revision};
}
void UpdateRuntimeButtons() {
    const bool busy=g.requests.busy();
    Id server=0,app=0;
    const bool fresh=ManagementConnection().fresh && ReadId(g.server_id,server) && ReadId(g.app_id,app) &&
        server==g.connected_server && app==g.connected_app;
    EnableWindow(g.connect,!busy);
    for (HWND control:g.config_controls) EnableWindow(control,!g.requests.loading_config());
    if (!g.requests.loading_config()) {
        const bool selected=ListView_GetNextItem(g.list,-1,LVNI_SELECTED)>=0;
        EnableWindow(g.edit_rule,selected); EnableWindow(g.delete_rule,selected);
    }
    EnableWindow(g.server_id,!busy); EnableWindow(g.app_id,!busy);
    EnableWindow(g.read_active,fresh && !busy);
    EnableWindow(g.apply_active,fresh && !busy && g.live.apply_block.empty());
    EnableWindow(g.pause_active,fresh && !busy && !g.live.paused);
    EnableWindow(g.resume_active,fresh && !busy && g.live.paused);
    EnableWindow(g.inspect_active,fresh && !busy && ListView_GetNextItem(g.runtime_intents,-1,LVNI_SELECTED)>=0);
    SetTextIfChanged(g.runtime_hint,FromUtf8(tradecopier::manager::RuntimeGuardHint(g.live,fresh)));
    std::wstring hint=L"保存只写入本地文件；连接执行器后可应用配置。";
    if (fresh) hint=g.live.apply_block.empty()?L"可直接应用：加入只跟新仓；退出会关闭该关系仓位；其他订阅继续运行。":
        L"应用配置前："+FromUtf8(g.live.apply_block)+L"。保存文件只保存本地草稿。";
    if (g.requests.loading_config()) hint=L"正在读取生效配置，请稍候…";
    SetTextIfChanged(g.apply_hint,hint);
}
void RenderRuntime(bool explicit_read) {
    std::wstring phase=g.live.paused?L"新仓已暂停":L"跟单运行中";
    if (!g.live.halt.empty() || g.live.review) phase+=L" · 存在局部异常";
    auto summary=phase+L"\r\n在管 "+std::to_wstring(g.live.active)+L"    待核对 "+std::to_wstring(g.live.review)+
        L"    未决请求 "+std::to_wstring(g.live.pending)+L"    退出中 "+std::to_wstring(g.live.exiting);
    SetTextIfChanged(g.summary,summary);
    const auto paths=FromUtf8(g.live.identity)+L"\r\n初始配置："+FromUtf8(g.live.config_path)+
        L"\r\n运行账本："+FromUtf8(g.live.journal_path)+L"\r\n账本版本："+std::to_wstring(g.live.revision)+
        (g.live.halt.empty()?std::wstring{}:L"\r\n运行告警："+FromUtf8(g.live.halt))+
        (g.live.paused && g.live.message.rfind("新仓因容量自动暂停：",0)==0 ? L"\r\n"+FromUtf8(g.live.message):std::wstring{});
    SetTextIfChanged(g.runtime_paths,paths);
    const std::wstring captions[]={L"仓位映射（"+std::to_wstring(g.live.links.size())+L" / "+std::to_wstring(g.live.links_total)+L"）",
        L"在管请求（"+std::to_wstring(g.live.intents.size())+L" / "+std::to_wstring(g.live.intents_total)+L"）"};
    for (int i=0;i<2;++i) { TCITEMW item{}; item.mask=TCIF_TEXT; item.pszText=const_cast<wchar_t*>(captions[i].c_str());
        TabCtrl_SetItem(g.runtime_tabs,i,&item); }
    struct RenderScope { RenderScope() { g.rendering_runtime=true; } ~RenderScope() { g.rendering_runtime=false; } } scope;
    if (g.shown_links!=g.live.links) { RuntimeTable(g.runtime_links,g.live.links,11); g.shown_links=g.live.links; }
    if (g.shown_intents!=g.live.intents) { RuntimeTable(g.runtime_intents,g.live.intents,9); g.shown_intents=g.live.intents; }
    if (!g.live.message.empty()) SetWindowTextW(g.details,FromUtf8(g.live.message).c_str());
    else if (!g.live.halt.empty()) SetWindowTextW(g.details,(L"运行告警："+FromUtf8(g.live.halt)+
        L"\r\n选中请求后可只读核对服务器成交；其他映射继续运行；只读核对不会重发请求。").c_str());
    else if (g.details_connection_error) {
        // Status replies normally leave the last command/inspection result visible.
        // A successful reconnect must explicitly retire the old connection error.
        const auto previous_error=ReadText(g.details);
        SetWindowTextW(g.details,(L"连接已恢复，当前运行状态已更新。\r\n上次连接 / 读取错误（历史记录）：\r\n"+
            previous_error).c_str());
    }
    else if (explicit_read) SetWindowTextW(g.details,L"已读取执行器状态。");
    g.details_connection_error=false;
    RefreshStatus();
}
std::optional<tradecopier::manager::Action> PrepareManagement(
    tradecopier::management::Operation op, bool load_config=false, bool background=false) {
    using namespace tradecopier::management;
    if (g.requests.busy()) return std::nullopt;
    Id server=0,app=0;
    if (!ReadId(g.server_id,server) || !ReadId(g.app_id,app) || app>4095) {
        Error(L"服务器 ID 必须为正整数；应用 ID 范围为 1–4095。"); return std::nullopt;
    }
    Request request; request.operation=op; request.session=g.live.session; request.revision=g.live.revision;
    if (op==Operation::Apply || op==Operation::UpdateSubscriptions) {
        request.config_json=tradecopier::WriteConfigJson(g.config); request.expected_config=g.live.config_json;
    }
    if (op==Operation::Inspect) {
        const int row=ListView_GetNextItem(g.runtime_intents,-1,LVNI_SELECTED);
        if (row<0 || static_cast<std::size_t>(row)>=g.live.intents.size()) {
            Error(L"请在请求列表选中一条请求。"); return std::nullopt;
        }
        request.intent=std::stoull(g.live.intents[static_cast<std::size_t>(row)][0]);
    }
    return tradecopier::manager::Action{server,app,std::move(request),load_config,background};
}
void ScheduleManagement(tradecopier::manager::Action action) {
    using tradecopier::manager::Requests;
    Id server=0,app=0;
    std::string error;
    if (!ReadId(g.server_id,server) || !ReadId(g.app_id,app) || !tradecopier::manager::SameTarget(action,server,app))
        error="连接目标已修改，本次操作未发送；请重新连接。";
    else error=tradecopier::manager::DispatchError(action,ManagementConnection());
    if (!error.empty()) {
        g.details_connection_error=false;
        SetWindowTextW(g.details,FromUtf8(error).c_str());
        Error(FromUtf8(error)); UpdateRuntimeButtons(); return;
    }
    const auto accepted=g.requests.Schedule(std::move(action));
    if (accepted==Requests::Accepted::Busy) return;
    if (accepted==Requests::Accepted::Deferred) {
        g.details_connection_error=false;
        SetWindowTextW(g.details,L"正在等待本次后台刷新完成，随后执行所选操作。");
    } else {
        const auto active=*g.requests.active();
        if (!active.background) {
            if (active.request.operation==tradecopier::management::Operation::Status) {
                if (!g.details_connection_error) SetWindowTextW(g.details,L"正在读取执行器状态。");
            } else {
                g.details_connection_error=false;
                SetWindowTextW(g.details,L"命令已发送，等待执行器确认。超时不会自动重发。");
            }
        }
        g.last_poll=GetTickCount64();
        try {
            g.pending=std::async(std::launch::async,[active] {
                return tradecopier::management::Exchange(active.server,active.app,active.request);
            });
        } catch (...) { (void)g.requests.Complete(); UpdateRuntimeButtons(); throw; }
    }
    UpdateRuntimeButtons();
}
void StartManagement(tradecopier::management::Operation op, bool load_config, bool background) {
    auto action=PrepareManagement(op,load_config,background);
    if (action) ScheduleManagement(std::move(*action));
}
void PollManagement() {
    // Error/confirmation dialogs run a nested Windows message loop.
    if (g.polling) return;
    struct PollScope { bool& flag; explicit PollScope(bool& v):flag(v) { flag=true; } ~PollScope() { flag=false; } } scope(g.polling);
    if (g.pending.valid() && g.pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        const auto completed=*g.requests.active();
        std::string command_error;
        try {
            auto response=g.pending.get();
            Id server=0,app=0;
            if (!ReadId(g.server_id,server) || !ReadId(g.app_id,app) ||
                !tradecopier::manager::SameTarget(completed,server,app)) {
                g.connected=false;
                SetWindowTextW(g.summary,L"连接目标已修改\r\n请重新连接，旧快照已停用。");
            } else {
                g.live=std::move(response); g.connected=true; g.last_response=GetTickCount64();
                g.connected_server=completed.server; g.connected_app=completed.app;
                RenderRuntime(!completed.background && completed.request.operation==tradecopier::management::Operation::Status);
                if (completed.load_config && g.live.ok) {
                    CopierConfig config; std::string error;
                    if (!tradecopier::ParseConfigJson(g.live.config_json,config,error)) throw std::runtime_error(error);
                    g.config=std::move(config); g.path.clear(); g.dirty=true;
                    SendMessageW(g.global_enabled,BM_SETCHECK,g.config.enabled?BST_CHECKED:BST_UNCHECKED,0);
                    FillList(-1); ShowPage(Page::General);
                }
                if (!completed.background && !completed.load_config &&
                    completed.request.operation!=tradecopier::management::Operation::Status)
                    ShowPage(Page::Runtime);
                if (!g.live.ok && completed.request.operation!=tradecopier::management::Operation::Status)
                    command_error=g.live.message;
            }
        } catch (const std::exception& error) {
            g.connected=false;
            g.details_connection_error=true;
            SetWindowTextW(g.summary,L"连接中断 / 状态未知\r\n显示上次快照，请重新连接。");
            SetWindowTextW(g.details,FromUtf8(error.what()).c_str());
            if (!completed.background) ShowPage(Page::Runtime);
        }
        auto next=g.requests.Complete();
        if (next) ScheduleManagement(std::move(*next));
        if (!command_error.empty()) Error(FromUtf8(command_error));
    }
    const auto age=g.last_response?(GetTickCount64()-g.last_response)/1000:0;
    const auto text=g.connected?(L"本机已连接 · "+std::to_wstring(age)+L" 秒前更新"):
        std::wstring(L"未连接 · 请检查信息页设置");
    SetTextIfChanged(g.connection,text);
    if (g.connected && age>=6) SetTextIfChanged(g.summary,L"状态已过期\r\n显示上次快照，等待刷新。");
    if (g.connected && !g.requests.active() && GetTickCount64()-g.last_poll>=2000) {
        Id server=0,app=0;
        if (ReadId(g.server_id,server) && ReadId(g.app_id,app) && server==g.connected_server && app==g.connected_app)
            StartManagement(tradecopier::management::Operation::Status,false,true);
        else { g.connected=false; SetWindowTextW(g.summary,L"连接目标已修改，请重新连接。"); }
    }
    UpdateRuntimeButtons();
}
void LoadActive() {
    if (!MayReplaceDocument()) return;
    StartManagement(tradecopier::management::Operation::Status,true);
}
void ApplyRuntime() {
    std::string error;
    if (!tradecopier::ValidateConfig(g.config,error)) {
        Error(FromUtf8(error)); return;
    }
    auto action=PrepareManagement(tradecopier::management::Operation::UpdateSubscriptions);
    if (!action) return;
    std::wstring review=L"将把以下草稿应用到已连接的本机执行器：\n"+FromUtf8(g.live.identity)+L"\n\n";
    CopierConfig active; std::string active_error;
    if (tradecopier::ParseConfigJson(g.live.config_json,active,active_error)) {
        for (const auto& old:active.relationships) if (active.enabled && old.enabled &&
            (!g.config.enabled || std::none_of(g.config.relationships.begin(),g.config.relationships.end(),[&](const RuleConfig& rule) {
                return rule.enabled && rule.id==old.id && rule.source_login==old.source_login && rule.follower_login==old.follower_login;
            }))) {
            review+=L"退出并平仓："+std::to_wstring(old.source_login)+L" → "+std::to_wstring(old.follower_login)+
                L"（关系 "+std::to_wstring(old.id)+L"）\n";
            if (review.size()>2000) {review+=L"另有退出关系，请核对草稿。\n";break;}
        }
    }
    for (const auto& rule:g.config.relationships) {
        review+=std::to_wstring(rule.source_login)+L" → "+std::to_wstring(rule.follower_login)+L"  "+ModeName(rule.mode)+
            L" "+FromUtf8(rule.parameter)+L"  品种："+FromUtf8(rule.symbol_mask)+
            (rule.auto_symbol?L" · 自动匹配":L" · 同名")+(rule.enabled?L"（启用）\n":L"（停用）\n");
        if (review.size()>2400) { review+=L"其余关系请见草稿列表。\n"; break; }
    }
    review+=L"\n新增关系只跟生效后的新仓；参数修改只影响新仓，老仓保留原规则。\n删除或停用关系会关闭该关系已有仓位；停用全局配置会退出所有关系并平掉其跟单仓位。\n在途开仓确认后也会平仓。结果不明时显示退出中 / 待核对，不会盲目重发。其他订阅和人工仓位不受影响。\n配置写入运行账本，不改变操作员暂停状态；本地 JSON 不会自动覆盖。";
    if (MessageBoxW(g.window,review.c_str(),L"确认应用配置",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)
        ScheduleManagement(std::move(*action));
}
void InspectRuntime() { StartManagement(tradecopier::management::Operation::Inspect); }
void CreateManagementControls() {
    g.connect=Control(L"BUTTON",L"连接 / 刷新",WS_TABSTOP|BS_PUSHBUTTON,Connect);
    g.connection=Control(L"STATIC",L"未连接 · 本机执行器",SS_LEFT|SS_ENDELLIPSIS);
    g.apply_active=Control(L"BUTTON",L"应用配置",WS_TABSTOP|BS_PUSHBUTTON,ApplyActive);
    g.collecting_runtime=true;
    g.pause_active=Control(L"BUTTON",L"暂停新仓跟随",WS_TABSTOP|BS_PUSHBUTTON,PauseActive);
    g.resume_active=Control(L"BUTTON",L"恢复新仓跟随",WS_TABSTOP|BS_PUSHBUTTON,ResumeActive);
    g.inspect_active=Control(L"BUTTON",L"核对选中请求（只读）",WS_TABSTOP|BS_PUSHBUTTON,InspectActive);
    g.runtime_hint=Control(L"STATIC",L"连接后显示操作条件。",SS_LEFT);
    g.runtime_tabs=Control(WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS,RuntimeTabs);
    for (const wchar_t* name:{L"仓位映射",L"交易请求"}) { TCITEMW item{}; item.mask=TCIF_TEXT;
        item.pszText=const_cast<wchar_t*>(name); TabCtrl_InsertItem(g.runtime_tabs,TabCtrl_GetItemCount(g.runtime_tabs),&item); }
    TabCtrl_SetPadding(g.runtime_tabs,Scaled(12),Scaled(5));
    g.runtime_links=Control(WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,RuntimeLinks);
    g.runtime_intents=Control(WC_LISTVIEWW,L"",WS_TABSTOP|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,RuntimeIntents);
    StyleTable(g.runtime_links); StyleTable(g.runtime_intents);
    const wchar_t* links[]={L"关系 ID",L"源账号",L"跟单账号",L"源仓位",L"目标仓位",L"源 → 目标品种",L"方向",L"源余量",L"目标余量",L"状态",L"开仓规则 / 原因"};
    const int link_widths[]={180,82,82,102,102,195,50,76,76,92,260};
    const wchar_t* intents[]={L"请求 ID",L"跟单账号",L"操作",L"目标仓位",L"请求手数",L"成交手数",L"状态",L"服务器请求",L"返回码"};
    const int intent_widths[]={70,92,60,110,86,86,100,104,86};
    for (int i=0;i<11;++i) AddColumn(g.runtime_links,i,links[i],link_widths[i]);
    for (int i=0;i<9;++i) AddColumn(g.runtime_intents,i,intents[i],intent_widths[i]);
    // Change visual order only; the management protocol keeps its stable fields.
    const int link_order[]={1,2,5,6,7,8,9,3,4,0,10};
    ListView_SetColumnOrderArray(g.runtime_links,11,link_order);
    g.details_label=Control(L"STATIC",L"操作结果",SS_LEFT);
    g.details=Control(L"EDIT",L"连接后自动更新运行状态。\r\n修改订阅：暂停新仓 → 编辑订阅 → 应用配置 → 检查后恢复。老仓保留开仓规则，新仓使用新配置，无需平仓。",
        WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,WS_EX_CLIENTEDGE);
    g.collecting_runtime=false;
    g.collecting_information=true;
    g.connection_group=Control(L"BUTTON",L"本机连接",BS_GROUPBOX);
    g.server_label=Control(L"STATIC",L"服务器 ID：",SS_LEFT);
    g.server_id=Control(L"EDIT",L"1",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,ServerId,WS_EX_CLIENTEDGE);
    g.app_label=Control(L"STATIC",L"应用 ID：",SS_LEFT);
    g.app_id=Control(L"EDIT",L"3101",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,AppId,WS_EX_CLIENTEDGE);
    SendMessageW(g.server_id,EM_SETLIMITTEXT,20,0); SendMessageW(g.app_id,EM_SETLIMITTEXT,4,0);
    g.connection_help=Control(L"STATIC",L"在 MT5 Trade Server 所在电脑以管理员身份运行；插件需启用 ManagementEnabled。",SS_LEFT);
    g.paths_label=Control(L"STATIC",L"执行器信息",SS_LEFT);
    g.runtime_paths=Control(L"EDIT",L"连接后显示执行器身份、配置来源与运行账本。",
        WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY,0,WS_EX_CLIENTEDGE);
    g.policies=Control(L"STATIC",L"跟单规则\r\nMT5 同服 · USD · 对冲 · 只跟新仓；不复制挂单和 SL / TP，跟随源实际平仓；手动全平后停止该仓位跟随。",SS_LEFT);
    g.information_help=Control(L"STATIC",L"保存文件只保存草稿；应用配置才更新执行器。关闭管理界面后，执行器继续运行。\r\n运行页保留在管及待核对记录；已平仓、低于最小手数及明确未成交的保证金不足开仓会清理，拒单详情见服务器日志。\r\nTradeCopierManager 0.6.2 · MT 样式界面 · 本机管理协议 v2",SS_LEFT);
    g.collecting_information=false;
    UpdateRuntimeButtons();
}

void ConfigurationMenu() {
    HMENU menu=CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu,MF_STRING,NewFile,L"新建配置\tCtrl+N");
    AppendMenuW(menu,MF_STRING,OpenConfig,L"打开 JSON…\tCtrl+O");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,SaveFile,L"保存到文件\tCtrl+S");
    AppendMenuW(menu,MF_STRING,SaveAsFile,L"另存为…");
    RECT bounds{}; GetWindowRect(g.file_menu,&bounds);
    const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_RIGHTBUTTON,
        bounds.left,bounds.bottom,0,g.window,nullptr);
    DestroyMenu(menu);
    if (command) PostMessageW(g.window,WM_COMMAND,static_cast<WPARAM>(command),0);
}
void Command(int id, int notification) {
    if (g.loading || notification!=BN_CLICKED) return;
    // Keyboard/menu commands obey the same load protection as disabled buttons.
    if (g.requests.loading_config() && (id==NewFile || id==OpenConfig || id==SaveFile || id==SaveAsFile ||
        id==GlobalEnabled || id==NewRule || id==EditRule || id==DeleteRule || id==FileMenu)) return;
    switch (id) {
    case CloseManager: case IDCANCEL: PostMessageW(g.window,WM_CLOSE,0,0); break;
    case FileMenu: ConfigurationMenu(); break;
    case Connect: StartManagement(tradecopier::management::Operation::Status); break;
    case ReadActive: LoadActive(); break;
    case ApplyActive: ApplyRuntime(); break;
    case PauseActive: StartManagement(tradecopier::management::Operation::Pause); break;
    case ResumeActive: {
        auto action=PrepareManagement(tradecopier::management::Operation::Resume);
        if (!action) break;
        if (MessageBoxW(g.window,L"恢复后只跟随新的源开仓；暂停期间出现的源仓不会补跟。继续吗？",L"恢复新仓跟随",MB_YESNO|MB_ICONQUESTION)==IDYES)
            ScheduleManagement(std::move(*action));
        break;
    }
    case InspectActive: InspectRuntime(); break;
    case NewFile:
        if (!MayReplaceDocument()) break;
        g.config={}; g.path.clear(); g.dirty=false;
        SendMessageW(g.global_enabled,BM_SETCHECK,BST_UNCHECKED,0);
        FillList(-1); ShowPage(Page::General);
        break;
    case OpenConfig: Open(); ShowPage(Page::General); break;
    case SaveFile: Save(false); break;
    case SaveAsFile: Save(true); break;
    case GlobalEnabled:
        g.config.enabled=SendMessageW(g.global_enabled,BM_GETCHECK,0,0)==BST_CHECKED;
        g.dirty=true; RefreshStatus();
        break;
    case NewRule: EditRelationship(-1); break;
    case EditRule: {
        const int selected=ListView_GetNextItem(g.list,-1,LVNI_SELECTED);
        if (selected>=0) EditRelationship(selected);
        break;
    }
    case DeleteRule: {
        const int selected=ListView_GetNextItem(g.list,-1,LVNI_SELECTED);
        if (selected<0) break;
        if (MessageBoxW(g.window,L"从配置草稿中删除选中的关系吗？\n\n应用后，该关系停止接收新仓，并关闭此关系产生的所有跟单仓位。其他关系不受影响。",
                       L"删除关系",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES) break;
        g.config.relationships.erase(g.config.relationships.begin()+selected);
        g.dirty=true; FillList(-1);
        break;
    }
    }
}

LRESULT TableDrawing(NMLVCUSTOMDRAW* draw) {
    if (draw->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
        HIGHCONTRASTW contrast{}; contrast.cbSize=sizeof(contrast);
        SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        if (!(contrast.dwFlags&HCF_HIGHCONTRASTON)) {
            draw->clrTextBk=draw->nmcd.dwItemSpec%2?RGB(243,243,243):RGB(255,255,255);
            draw->clrText=RGB(24,24,24);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}
void DrawBrand(const DRAWITEMSTRUCT& item) {
    FillRect(item.hDC,&item.rcItem,GetSysColorBrush(COLOR_WINDOW));
    HBRUSH blue=CreateSolidBrush(RGB(0,119,183)), light=CreateSolidBrush(RGB(28,174,224));
    const auto old_pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));
    const auto old_brush=SelectObject(item.hDC,light);
    RoundRect(item.hDC,Scaled(12),Scaled(3),Scaled(28),Scaled(19),Scaled(4),Scaled(4));
    SelectObject(item.hDC,GetSysColorBrush(COLOR_WINDOW));
    Rectangle(item.hDC,Scaled(16),Scaled(7),Scaled(24),Scaled(16));
    SelectObject(item.hDC,blue);
    RoundRect(item.hDC,Scaled(2),Scaled(13),Scaled(38),Scaled(40),Scaled(4),Scaled(4));
    SelectObject(item.hDC,light);
    RoundRect(item.hDC,Scaled(2),Scaled(13),Scaled(38),Scaled(25),Scaled(4),Scaled(4));
    SelectObject(item.hDC,old_brush); SelectObject(item.hDC,old_pen);
    DeleteObject(blue); DeleteObject(light);
}
LRESULT HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        g.window=window; CreateControls(); return 0;
    case WM_TIMER: PollManagement(); return 0;
    case WM_SIZE:
        if (g.status && wparam!=SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x=Scaled(860); info->ptMinTrackSize.y=Scaled(640);
        return 0;
    }
    case WM_COMMAND: Command(LOWORD(wparam),HIWORD(wparam)); return 0;
    case WM_ERASEBKGND: {
        RECT bounds{}; GetClientRect(window,&bounds);
        FillRect(reinterpret_cast<HDC>(wparam),&bounds,GetSysColorBrush(COLOR_WINDOW));
        bounds.top=std::max(0L,bounds.bottom-Scaled(58));
        FillRect(reinterpret_cast<HDC>(wparam),&bounds,GetSysColorBrush(COLOR_BTNFACE));
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        const bool footer=reinterpret_cast<HWND>(lparam)==g.connection;
        const int color=footer?COLOR_BTNFACE:COLOR_WINDOW;
        SetTextColor(reinterpret_cast<HDC>(wparam),GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(reinterpret_cast<HDC>(wparam),GetSysColor(color));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(color));
    }
    case WM_DRAWITEM: {
        const auto* item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item && item->hwndItem==g.brand) { DrawBrand(*item); return TRUE; }
        break;
    }
    case WM_NOTIFY: {
        auto* header=reinterpret_cast<NMHDR*>(lparam);
        if (header->hwndFrom==g.tabs && header->code==TCN_SELCHANGE) {
            ShowPage(static_cast<Page>(TabCtrl_GetCurSel(g.tabs))); return 0;
        }
        if (header->hwndFrom==g.runtime_tabs && header->code==TCN_SELCHANGE) {
            g.requests_page=TabCtrl_GetCurSel(g.runtime_tabs)==1;
            ShowRuntimeTable(); return 0;
        }
        const bool table=header->hwndFrom==g.list || header->hwndFrom==g.runtime_links || header->hwndFrom==g.runtime_intents;
        if (table && header->code==NM_CUSTOMDRAW) return TableDrawing(reinterpret_cast<NMLVCUSTOMDRAW*>(lparam));
        if (table && header->code==LVN_ITEMCHANGED && !g.loading && !g.rendering_runtime) {
            if (header->hwndFrom==g.list) g.editing_index=ListView_GetNextItem(g.list,-1,LVNI_SELECTED);
            if (header->hwndFrom==g.runtime_links) {
                const int selected=ListView_GetNextItem(g.runtime_links,-1,LVNI_SELECTED);
                if (selected>=0 && static_cast<std::size_t>(selected)<g.shown_links.size()) {
                    const auto& row=g.shown_links[static_cast<std::size_t>(selected)];
                    if (row.size()==11) SetWindowTextW(g.details,FromUtf8(
                        "映射状态："+row[9]+"\r\n源账号 "+row[1]+" / 仓位 "+row[3]+
                        " → 跟单账号 "+row[2]+" / 仓位 "+row[4]+"\r\n原因 / 开仓规则："+row[10]).c_str());
                }
            }
            UpdateRuntimeButtons(); return 0;
        }
        if (header->hwndFrom==g.list && !g.loading) {
            if (header->code==NM_DBLCLK) PostMessageW(window,WM_COMMAND,EditRule,0);
            if (header->code==LVN_KEYDOWN) {
                const WORD key=reinterpret_cast<NMLVKEYDOWN*>(lparam)->wVKey;
                if (key==VK_RETURN) PostMessageW(window,WM_COMMAND,EditRule,0);
                if (key==VK_INSERT) PostMessageW(window,WM_COMMAND,NewRule,0);
                if (key==VK_DELETE) PostMessageW(window,WM_COMMAND,DeleteRule,0);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (MayReplaceDocument()) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window,1);
        if (g.font) { DeleteObject(g.font); g.font=nullptr; }
        if (g.heading_font) { DeleteObject(g.heading_font); g.heading_font=nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    try { return HandleMessage(window, message, wparam, lparam); }
    catch (const std::exception& exception) {
        Error(L"管理界面遇到错误：\n" + FromUtf8(exception.what()));
    } catch (...) { Error(L"管理界面遇到未知错误。"); }
    return message == WM_CREATE ? -1 : 0;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    g.instance = instance;
    SetProcessDPIAware();
    HDC screen = GetDC(nullptr);
    if (screen) { g.dpi = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(nullptr, screen); }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
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
    // The compact dialog layout requires at least 860 x 640 logical pixels.
    const int initial_width = available_width > 0 ? std::min(Scaled(960), available_width) : Scaled(960);
    const int initial_height = available_height > 0 ? std::min(Scaled(720), available_height) : Scaled(720);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, kTitle, WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, initial_width, initial_height,
                                   nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)) {
            if (message.wParam==VK_TAB) {
                const int direction=(GetKeyState(VK_SHIFT)&0x8000)?2:1;
                ShowPage(static_cast<Page>((static_cast<int>(g.page)+direction)%3));
                SetFocus(g.tabs); continue;
            }
            const int id=message.wParam=='N'?NewFile:message.wParam=='O'?OpenConfig:message.wParam=='S'?SaveFile:0;
            if (id) { Command(id,BN_CLICKED); continue; }
        }
        if (message.message==WM_KEYDOWN && message.wParam==VK_RETURN && GetFocus()==g.list) {
            Command(EditRule,BN_CLICKED); continue;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

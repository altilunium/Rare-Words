// rare_words_cpp_app.cpp (extended version)
// Native Win32 C++ app using SQLite to store rare words with definition, example, and etymology.
// MODIFIED: Fixed update logic (no longer reverts on type), fixed resize flicker (WM_ERASEBKGND).

#include <windows.h>
#include <commctrl.h> // For ListView
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <sqlite3.h>

#pragma comment(lib, "comctl32.lib")

static const wchar_t CLASS_NAME[] = L"RareWordsAppClass";
static const wchar_t WINDOW_TITLE[] = L"Rare Words — Extended";

enum { ID_WORD = 1001, ID_DEFINITION, ID_EXAMPLE, ID_ETYMOLOGY, ID_TAGS, ID_ADD, ID_LIST, ID_AUTOGROUP, ID_FILTER_TAG, ID_DELETE };

sqlite3* g_db = nullptr;
HWND g_list = nullptr;
HWND g_deleteButton = nullptr; // Handle for the new Delete button
static int g_editingId = -1; // Keep track of which ID we are editing, -1 = not editing
// static bool g_isProgrammaticChange = false; // <-- This was part of the buggy logic, now removed.

std::string w2u(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], sz, nullptr, nullptr);
    return out;
}

std::wstring u2w(const std::string& u) {
    if (u.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), &out[0], sz);
    return out;
}

void check_sqlite(int rc, char* errmsg = nullptr) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        MessageBoxA(nullptr, errmsg ? errmsg : "SQLite error", "DB Error", MB_OK | MB_ICONERROR);
    }
}

void init_db() {
    const char* dbfile = "rare_words.db";
    if (sqlite3_open(dbfile, &g_db) != SQLITE_OK) {
        MessageBoxA(nullptr, "Failed to open DB", "Error", MB_OK | MB_ICONERROR);
        return;
    }
    const char* sql =
        "CREATE TABLE IF NOT EXISTS words("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " word TEXT NOT NULL UNIQUE,"
        " definition TEXT,"
        " example TEXT,"
        " etymology TEXT,"
        " tags TEXT"
        ");";
    char* errmsg = nullptr;
    int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        check_sqlite(rc, errmsg);
        sqlite3_free(errmsg);
    }
}

// Returns true on success, false on failure (e.g., constraint)
bool add_word_to_db(const std::wstring& word, const std::wstring& definition, const std::wstring& example, const std::wstring& etymology, const std::wstring& tags) {
    std::string s_word = w2u(word);
    std::string s_def = w2u(definition);
    std::string s_ex = w2u(example);
    std::string s_et = w2u(etymology);
    std::string s_tags = w2u(tags);

    const char* sql = "INSERT INTO words(word, definition, example, etymology, tags) VALUES(?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, s_word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, s_def.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, s_ex.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, s_et.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, s_tags.c_str(), -1, SQLITE_TRANSIENT);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return false; // Failed
        return true; // Success
    }
    sqlite3_finalize(stmt);
    return false;
}

// Returns true on success, false on failure
bool update_word_in_db(int id, const std::wstring& word, const std::wstring& definition, const std::wstring& example, const std::wstring& etymology, const std::wstring& tags) {
    std::string s_word = w2u(word);
    std::string s_def = w2u(definition);
    std::string s_ex = w2u(example);
    std::string s_et = w2u(etymology);
    std::string s_tags = w2u(tags);

    const char* sql = "UPDATE words SET word = ?, definition = ?, example = ?, etymology = ?, tags = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, s_word.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, s_def.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, s_ex.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, s_et.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, s_tags.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, id);

        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) return false; // Failed (e.g., UNIQUE constraint)
        return true; // Success
    }
    sqlite3_finalize(stmt);
    return false;
}

void delete_word_from_db(int id) {
    const char* sql = "DELETE FROM words WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

struct WordRow {
    int id;
    std::wstring word;
    std::wstring definition;
    std::wstring example;
    std::wstring etymology;
    std::wstring tags;
};

std::vector<WordRow> load_words_from_db(const std::wstring& filterTag = L"") {
    std::vector<WordRow> out;
    const char* sql = "SELECT id, word, definition, example, etymology, tags FROM words ORDER BY word COLLATE NOCASE;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            WordRow r;
            r.id = sqlite3_column_int(stmt, 0);
            const unsigned char* w = sqlite3_column_text(stmt, 1);
            const unsigned char* d = sqlite3_column_text(stmt, 2);
            const unsigned char* e = sqlite3_column_text(stmt, 3);
            const unsigned char* y = sqlite3_column_text(stmt, 4);
            const unsigned char* t = sqlite3_column_text(stmt, 5);
            r.word = u2w((char*)(w ? (const char*)w : ""));
            r.definition = u2w((char*)(d ? (const char*)d : ""));
            r.example = u2w((char*)(e ? (const char*)e : ""));
            r.etymology = u2w((char*)(y ? (const char*)y : ""));
            r.tags = u2w((char*)(t ? (const char*)t : ""));
            if (filterTag.empty() || (r.tags.find(filterTag) != std::wstring::npos)) {
                out.push_back(std::move(r));
            }
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

void refresh_list(const std::wstring& filterTag = L"") {
    ListView_DeleteAllItems(g_list);
    auto rows = load_words_from_db(filterTag);
    int idx = 0;
    for (auto& r : rows) {
        LVITEMW li{};
        li.mask = LVIF_TEXT | LVIF_PARAM;
        li.iItem = idx;
        li.iSubItem = 0;
        li.pszText = const_cast<wchar_t*>(r.word.c_str());
        li.lParam = r.id; // Store the database ID here!
        ListView_InsertItem(g_list, &li);
        ListView_SetItemText(g_list, idx, 1, const_cast<wchar_t*>(r.definition.c_str()));
        ListView_SetItemText(g_list, idx, 2, const_cast<wchar_t*>(r.example.c_str()));
        ListView_SetItemText(g_list, idx, 3, const_cast<wchar_t*>(r.etymology.c_str()));
        ListView_SetItemText(g_list, idx, 4, const_cast<wchar_t*>(r.tags.c_str()));
        idx++;
    }
}

void ClearInputFields(HWND hwnd) {
    // We don't need the g_isProgrammaticChange flag here because
    // the EN_CHANGE handler for ID_WORD checks if g_editingId != -1,
    // which is false *after* a successful add/update/delete.
    SendDlgItemMessageW(hwnd, ID_WORD, WM_SETTEXT, 0, (LPARAM)L"");
    SendDlgItemMessageW(hwnd, ID_DEFINITION, WM_SETTEXT, 0, (LPARAM)L"");
    SendDlgItemMessageW(hwnd, ID_EXAMPLE, WM_SETTEXT, 0, (LPARAM)L"");
    SendDlgItemMessageW(hwnd, ID_ETYMOLOGY, WM_SETTEXT, 0, (LPARAM)L"");
    SendDlgItemMessageW(hwnd, ID_TAGS, WM_SETTEXT, 0, (LPARAM)L"");
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex{}; icex.dwSize = sizeof(icex); icex.dwICC = ICC_LISTVIEW_CLASSES; InitCommonControlsEx(&icex);

        // --- All controls in the top "form" area are at fixed positions ---
        CreateWindowW(L"STATIC", L"Word:", WS_CHILD | WS_VISIBLE, 10, 10, 50, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 70, 8, 200, 22, hwnd, (HMENU)ID_WORD, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Definition:", WS_CHILD | WS_VISIBLE, 10, 40, 70, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 90, 38, 400, 50, hwnd, (HMENU)ID_DEFINITION, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Example:", WS_CHILD | WS_VISIBLE, 10, 95, 70, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 90, 93, 400, 50, hwnd, (HMENU)ID_EXAMPLE, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Etymology:", WS_CHILD | WS_VISIBLE, 10, 150, 70, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 90, 148, 400, 22, hwnd, (HMENU)ID_ETYMOLOGY, nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Tags (comma):", WS_CHILD | WS_VISIBLE, 10, 180, 120, 20, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 130, 178, 200, 22, hwnd, (HMENU)ID_TAGS, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE, 350, 176, 80, 26, hwnd, (HMENU)ID_ADD, nullptr, nullptr);

        // Create Delete button, but keep it hidden
        g_deleteButton = CreateWindowW(L"BUTTON", L"Delete", WS_CHILD, 440, 176, 80, 26, hwnd, (HMENU)ID_DELETE, nullptr, nullptr);

        // Adjust layout for new button
        CreateWindowW(L"BUTTON", L"Auto-group", WS_CHILD | WS_VISIBLE, 530, 176, 100, 26, hwnd, (HMENU)ID_AUTOGROUP, nullptr, nullptr);
        CreateWindowW(L"STATIC", L"Filter tag:", WS_CHILD | WS_VISIBLE, 640, 176, 70, 22, hwnd, nullptr, nullptr, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 710, 176, 120, 22, hwnd, (HMENU)ID_FILTER_TAG, nullptr, nullptr);

        // --- List view (will be resized) ---
        g_list = CreateWindowW(WC_LISTVIEW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 10, 210, 760, 260, hwnd, (HMENU)ID_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = (LPWSTR)L"Word"; col.cx = 150; ListView_InsertColumn(g_list, 0, &col);
        col.pszText = (LPWSTR)L"Definition"; col.cx = 200; ListView_InsertColumn(g_list, 1, &col);
        col.pszText = (LPWSTR)L"Example"; col.cx = 200; ListView_InsertColumn(g_list, 2, &col);
        col.pszText = (LPWSTR)L"Etymology"; col.cx = 150; ListView_InsertColumn(g_list, 3, &col);
        col.pszText = (LPWSTR)L"Tags"; col.cx = 80; ListView_InsertColumn(g_list, 4, &col);

        refresh_list();
        return 0;
    }
    case WM_COMMAND: {
        int lw = LOWORD(wParam);
        int hw = HIWORD(wParam);

        if (lw == ID_ADD) {
            wchar_t bufWord[256] = { 0 }; SendDlgItemMessageW(hwnd, ID_WORD, WM_GETTEXT, 256, (LPARAM)bufWord);
            wchar_t bufDef[2048] = { 0 }; SendDlgItemMessageW(hwnd, ID_DEFINITION, WM_GETTEXT, 2048, (LPARAM)bufDef);
            wchar_t bufEx[2048] = { 0 }; SendDlgItemMessageW(hwnd, ID_EXAMPLE, WM_GETTEXT, 2048, (LPARAM)bufEx);
            wchar_t bufEt[512] = { 0 }; SendDlgItemMessageW(hwnd, ID_ETYMOLOGY, WM_GETTEXT, 512, (LPARAM)bufEt);
            wchar_t bufTags[512] = { 0 }; SendDlgItemMessageW(hwnd, ID_TAGS, WM_GETTEXT, 512, (LPARAM)bufTags);

            std::wstring w(bufWord), def(bufDef), ex(bufEx), et(bufEt), tags(bufTags);
            if (w.empty()) { MessageBoxW(hwnd, L"Word required.", L"Validation", MB_OK | MB_ICONWARNING); break; }

            bool success = false;
            if (g_editingId != -1) {
                // We are in UPDATE mode
                if (update_word_in_db(g_editingId, w, def, ex, et, tags)) {
                    g_editingId = -1;
                    SendDlgItemMessageW(hwnd, ID_ADD, WM_SETTEXT, 0, (LPARAM)L"Add");
                    ShowWindow(g_deleteButton, SW_HIDE);
                    success = true;
                }
                else {
                    MessageBoxW(hwnd, L"Failed to update. Does the new word text already exist?", L"Update Error", MB_OK | MB_ICONERROR);
                }
            }
            else {
                // We are in ADD mode
                if (add_word_to_db(w, def, ex, et, tags)) {
                    success = true;
                }
                else {
                    MessageBoxW(hwnd, L"Failed to add. Does this word already exist?", L"Add Error", MB_OK | MB_ICONERROR);
                }
            }

            if (success) {
                ClearInputFields(hwnd);
                // Get current filter to refresh list properly
                wchar_t bufFilter[256]; SendDlgItemMessageW(hwnd, ID_FILTER_TAG, WM_GETTEXT, 256, (LPARAM)bufFilter);
                refresh_list(std::wstring(bufFilter));
            }
        }
        else if (lw == ID_DELETE) {
            if (g_editingId != -1) {
                if (MessageBoxW(hwnd, L"Are you sure you want to delete this word?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    delete_word_from_db(g_editingId);
                    g_editingId = -1;
                    ClearInputFields(hwnd);
                    SendDlgItemMessageW(hwnd, ID_ADD, WM_SETTEXT, 0, (LPARAM)L"Add");
                    ShowWindow(g_deleteButton, SW_HIDE);

                    wchar_t bufFilter[256]; SendDlgItemMessageW(hwnd, ID_FILTER_TAG, WM_GETTEXT, 256, (LPARAM)bufFilter);
                    refresh_list(std::wstring(bufFilter));
                }
            }
        }
        else if (hw == EN_CHANGE) {
            if (lw == ID_FILTER_TAG) {
                wchar_t buf[256]; SendDlgItemMessageW(hwnd, ID_FILTER_TAG, WM_GETTEXT, 256, (LPARAM)buf);
                refresh_list(std::wstring(buf));
            }
            // MOD: This is the new "cancel" logic.
            // If user is in edit mode and *deletes all text* from the WORD box,
            // revert to "Add" mode.
            else if (lw == ID_WORD && g_editingId != -1) {
                wchar_t bufWord[256] = { 0 };
                SendDlgItemMessageW(hwnd, ID_WORD, WM_GETTEXT, 256, (LPARAM)bufWord);
                if (wcslen(bufWord) == 0) {
                    g_editingId = -1;
                    SendDlgItemMessageW(hwnd, ID_ADD, WM_SETTEXT, 0, (LPARAM)L"Add");
                    ShowWindow(g_deleteButton, SW_HIDE);
                    // Clear other fields to complete the "cancel"
                    // This is safe because g_editingId is now -1
                    ClearInputFields(hwnd);
                }
            }
            // MOD: The buggy "else if (!g_isProgrammaticChange...)" block
            // that reverted to "Add" mode on *any* typing has been REMOVED.
        }
        return 0;
    }
    case WM_SIZE: {
        // Handle window resizing
        if (g_list) {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            // Keep the top form area fixed (approx 210 pixels high)
            // Resize the list view to fill the rest, with a 10px margin
            MoveWindow(g_list, 10, 210, width - 20, height - 220, TRUE);
        }
        return 0;
    }
                // MOD: Added WM_ERASEBKGND handler to prevent resize flicker
    case WM_ERASEBKGND:
        return 1; // Tell Windows we handled it (by doing nothing)

    case WM_NOTIFY: {
        // Handle ListView notifications (for Delete key and Double-click)
        LPNMHDR lpnmh = (LPNMHDR)lParam;
        if (lpnmh->hwndFrom == g_list) {
            if (lpnmh->code == LVN_KEYDOWN) {
                // DELETE key pressed
                LPNMLVKEYDOWN lpnmkv = (LPNMLVKEYDOWN)lParam;
                if (lpnmkv->wVKey == VK_DELETE) {
                    // Only delete if NOT in edit mode (use the button for that)
                    if (g_editingId == -1) {
                        int sel = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
                        if (sel != -1) {
                            if (MessageBoxW(hwnd, L"Are you sure you want to delete this word?", L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                LVITEMW li{};
                                li.mask = LVIF_PARAM;
                                li.iItem = sel;
                                if (ListView_GetItem(g_list, &li)) {
                                    delete_word_from_db((int)li.lParam); // li.lParam holds the DB ID
                                    // Refresh list with filter
                                    wchar_t bufFilter[256]; SendDlgItemMessageW(hwnd, ID_FILTER_TAG, WM_GETTEXT, 256, (LPARAM)bufFilter);
                                    refresh_list(std::wstring(bufFilter));
                                }
                            }
                        }
                    }
                }
            }
            else if (lpnmh->code == NM_DBLCLK) {
                // Double-click to EDIT
                LPNMITEMACTIVATE lpnmia = (LPNMITEMACTIVATE)lParam;
                int sel = lpnmia->iItem;
                if (sel != -1) {
                    // Get the DB ID from the item's lParam
                    LVITEMW li{};
                    li.mask = LVIF_PARAM;
                    li.iItem = sel;
                    if (ListView_GetItem(g_list, &li)) {
                        // MOD: No longer need g_isProgrammaticChange flag
                        // g_isProgrammaticChange = true; 

                        g_editingId = (int)li.lParam; // Set global editing ID

                        // Get text from list and populate fields
                        wchar_t buf[2048]; // Re-use a large buffer
                        ListView_GetItemText(g_list, sel, 0, buf, 2048); SendDlgItemMessageW(hwnd, ID_WORD, WM_SETTEXT, 0, (LPARAM)buf);
                        ListView_GetItemText(g_list, sel, 1, buf, 2048); SendDlgItemMessageW(hwnd, ID_DEFINITION, WM_SETTEXT, 0, (LPARAM)buf);
                        ListView_GetItemText(g_list, sel, 2, buf, 2048); SendDlgItemMessageW(hwnd, ID_EXAMPLE, WM_SETTEXT, 0, (LPARAM)buf);
                        ListView_GetItemText(g_list, sel, 3, buf, 2048); SendDlgItemMessageW(hwnd, ID_ETYMOLOGY, WM_SETTEXT, 0, (LPARAM)buf);
                        ListView_GetItemText(g_list, sel, 4, buf, 2048); SendDlgItemMessageW(hwnd, ID_TAGS, WM_SETTEXT, 0, (LPARAM)buf);

                        // Change button text and show Delete button
                        SendDlgItemMessageW(hwnd, ID_ADD, WM_SETTEXT, 0, (LPARAM)L"Update");
                        ShowWindow(g_deleteButton, SW_SHOW);
                        SetFocus(GetDlgItem(hwnd, ID_WORD)); // Focus the first field

                        // MOD: No longer need g_isProgrammaticChange flag
                        // g_isProgrammaticChange = false; 
                    }
                }
            }
        }
        return 0;
    }
    case WM_DESTROY:
        if (g_db) sqlite3_close(g_db);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    init_db();
    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // MOD: Set background brush to default window color.
    // WM_ERASEBKGND will handle most flicker, but this is good practice.
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    // MOD: Kept WS_CLIPCHILDREN as it's still good practice
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 520, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

/* Notes:
 - Now includes definition and etymology fields.
 - If you already had an old DB, delete rare_words.db once so it can recreate the schema.
 - Columns auto-resize for readability; add scrolling if needed.
 - MOD: Handles WM_SIZE to resize ListView.
 - MOD: Handles NM_DBLCLK to load item for editing.
 - MOD: Handles LVN_KEYDOWN (VK_DELETE) to delete item.
 - MOD: "Add" button toggles to "Update" when editing.
 - MOD: Added "Delete" button visible only in edit mode.
 - MOD: Fixed resize repaint flicker (black box) with WM_ERASEBKGND.
 - MOD: Fixed update logic failing. Button no longer reverts on type.
 - MOD: Clearing "Word" field during edit mode cancels edit mode.
*/

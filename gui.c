#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <io.h>
#include <commdlg.h>
#include <windows.h>
#include <winioctl.h>

extern int create_directory(const char *path);
extern void normalize_path(char *path);
extern int file_exists(const char *path);
extern void get_zip_basename(const char *zip_path, char *output, size_t output_size);
extern void TrimSpaces(char *str);
extern int set_file_times(const char *path, unsigned int dostime, unsigned int dosdate);

extern int extract_entry(FILE *zip_file, char *filename, unsigned int filename_length,
                  unsigned short compression_method, unsigned long long compressed_size,
                  unsigned long long uncompressed_size, unsigned short last_mod_time,
                  unsigned short last_mod_date, const char *output_dir, int preserve_time);

#ifdef _WIN32
#define fseek64 fseeko64
#define ftell64 ftello64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

void gui_enable_sparse(HANDLE hFile) {
    DWORD dwTemp;
    DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dwTemp, NULL);
}

void gui_free_disk_space(HANDLE hFile, unsigned long long offset, unsigned long long size) {
    if (size == 0) return;
    FILE_ZERO_DATA_INFORMATION fzdi;
    fzdi.FileOffset.QuadPart = (LONGLONG)offset;
    fzdi.BeyondFinalZero.QuadPart = (LONGLONG)(offset + size);
    DWORD dwTemp;
    DeviceIoControl(hFile, FSCTL_SET_ZERO_DATA, &fzdi, sizeof(fzdi), NULL, 0, &dwTemp, NULL);
}

DWORD WINAPI ExtractionThreadProc(LPVOID lpParam) {
    ExtractionParams *params = (ExtractionParams *)lpParam;
    
    // Open in r+b to allow sparse operations
    FILE *zip_file = fopen(params->zip_path, "r+b");
    if (!zip_file) {
        PostMessage(params->hwnd, WM_EXTRACTION_ERROR, 0, (LPARAM)strdup("Cannot open ZIP file"));
        free(params);
        return 1;
    }

    HANDLE hZip = (HANDLE)_get_osfhandle(fileno(zip_file));
    if (!params->keep_zip) {
        gui_enable_sparse(hZip);
    }

    char output_dir[MAX_PATH_LEN];
    get_zip_basename(params->zip_path, output_dir, MAX_PATH_LEN);
    create_directory(output_dir);

    // Find EOCD
    fseek64(zip_file, 0, SEEK_END);
    unsigned long long file_size = ftell64(zip_file);
    unsigned long long eocd_pos = 0;
    unsigned char buf[4];
    int found = 0;

    for (int i = 0; i <= 65535; i++) {
        unsigned long long pos = (file_size > 22 + (unsigned long long)i) ? file_size - 22 - i : 0;
        fseek64(zip_file, (long long)pos, SEEK_SET);
        if (fread(buf, 1, 4, zip_file) == 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x05 && buf[3] == 0x06) {
            eocd_pos = pos;
            found = 1;
            break;
        }
        if (pos == 0) break;
    }

    if (!found) {
        PostMessage(params->hwnd, WM_EXTRACTION_ERROR, 0, (LPARAM)strdup("Could not find ZIP Central Directory"));
        fclose(zip_file);
        free(params);
        return 1;
    }

    unsigned char eocd[22];
    fseek64(zip_file, (long long)eocd_pos, SEEK_SET);
    fread(eocd, 1, 22, zip_file);

    unsigned long long total_entries = READ_LE16(eocd + 10);
    unsigned long long cd_offset = READ_LE32(eocd + 16);

    // Check for ZIP64 EOCD Locator
    if (eocd_pos >= 20) {
        fseek64(zip_file, (long long)(eocd_pos - 20), SEEK_SET);
        unsigned char locator[20];
        if (fread(locator, 1, 20, zip_file) == 20 && locator[0] == 'P' && locator[1] == 'K' && locator[2] == 0x06 && locator[3] == 0x07) {
            unsigned long long zip64_eocd_pos = READ_LE64(locator + 8);
            fseek64(zip_file, (long long)zip64_eocd_pos, SEEK_SET);
            unsigned char eocd64[56];
            if (fread(eocd64, 1, 56, zip_file) == 56 && eocd64[0] == 'P' && eocd64[1] == 'K' && eocd64[2] == 0x06 && eocd64[3] == 0x06) {
                total_entries = READ_LE64(eocd64 + 24);
                cd_offset = READ_LE64(eocd64 + 48);
            }
        }
    }

    int entry_count = 0;
    unsigned long long total_extracted = 0;
    fseek64(zip_file, (long long)cd_offset, SEEK_SET);

    for (unsigned long long i = 0; i < total_entries; i++) {
        unsigned char cd[46];
        if (fread(cd, 1, 46, zip_file) != 46) break;
        if (cd[0] != 'P' || cd[1] != 'K' || cd[2] != 0x01 || cd[3] != 0x02) break;

        unsigned short method = READ_LE16(cd + 10);
        unsigned short mod_time = READ_LE16(cd + 12);
        unsigned short mod_date = READ_LE16(cd + 14);
        unsigned long long comp_size = READ_LE32(cd + 20);
        unsigned long long uncomp_size = READ_LE32(cd + 24);
        unsigned short name_len = READ_LE16(cd + 28);
        unsigned short extra_len = READ_LE16(cd + 30);
        unsigned short comment_len = READ_LE16(cd + 32);
        unsigned long long local_offset = READ_LE32(cd + 42);

        char *filename = (char *)malloc(name_len + 1);
        fread(filename, 1, name_len, zip_file);
        filename[name_len] = '\0';

        // Check for ZIP64 extra fields
        if (comp_size == 0xFFFFFFFF || uncomp_size == 0xFFFFFFFF || local_offset == 0xFFFFFFFF) {
            unsigned char *extra = (unsigned char *)malloc(extra_len);
            fread(extra, 1, extra_len, zip_file);
            for (int e = 0; e < extra_len - 4; ) {
                unsigned short tag = READ_LE16(extra + e);
                unsigned short tsize = READ_LE16(extra + e + 2);
                if (tag == 0x0001) {
                    int p = e + 4;
                    if (uncomp_size == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        uncomp_size = READ_LE64(extra + p); p += 8;
                    }
                    if (comp_size == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        comp_size = READ_LE64(extra + p); p += 8;
                    }
                    if (local_offset == 0xFFFFFFFF && p + 8 <= e + 4 + tsize) {
                        local_offset = READ_LE64(extra + p);
                    }
                }
                e += 4 + tsize;
            }
            free(extra);
            fseek64(zip_file, (long long)comment_len, SEEK_CUR);
        } else {
            fseek64(zip_file, (long long)(extra_len + comment_len), SEEK_CUR);
        }

        unsigned long long next_cd = ftell64(zip_file);

        // Jump to Local Header
        fseek64(zip_file, (long long)local_offset, SEEK_SET);
        unsigned char lfh[30];
        fread(lfh, 1, 30, zip_file);
        unsigned short lfh_name_len = READ_LE16(lfh + 26);
        unsigned short lfh_extra_len = READ_LE16(lfh + 28);
        unsigned long long data_offset = local_offset + 30 + lfh_name_len + lfh_extra_len;
        fseek64(zip_file, (long long)(lfh_name_len + lfh_extra_len), SEEK_CUR);

        char *progress_msg = (char *)malloc(256);
        if (progress_msg) {
            snprintf(progress_msg, 256, "Extracting: %s", filename);
            PostMessage(params->hwnd, WM_EXTRACTION_PROGRESS, (WPARAM)entry_count, (LPARAM)progress_msg);
        }

        char output_path[MAX_PATH_LEN];
        snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
        normalize_path(output_path);

        if (!params->force_overwrite && file_exists(output_path)) {
            // skip
        } else {
            if (extract_entry(zip_file, filename, name_len, method, comp_size, uncomp_size, mod_time, mod_date, output_dir, params->preserve_time)) {
                total_extracted += uncomp_size;
                if (!params->keep_zip && comp_size > 0) {
                    gui_free_disk_space(hZip, data_offset, comp_size);
                }
            }
        }

        free(filename);
        entry_count++;
        fseek64(zip_file, (long long)next_cd, SEEK_SET);
    }

    if (!params->keep_zip) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)cd_offset;
        SetFilePointerEx(hZip, li, NULL, FILE_BEGIN);
        SetEndOfFile(hZip);
    }
    fclose(zip_file);

    char *msg = (char *)malloc(512);
    if (msg) {
        snprintf(msg, 512, "Extraction complete!\n\nFiles extracted: %d\nTotal size: %I64u bytes\nOutput folder: %s\nZIP file: %s",
            entry_count, total_extracted, output_dir, params->keep_zip ? "kept intact" : "truncated");
        PostMessage(params->hwnd, WM_EXTRACTION_COMPLETE, (WPARAM)entry_count, (LPARAM)msg);
    }
    
    free(params);
    return 0;
}

LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static GUIControls *controls = NULL;
    static HBRUSH hBackgroundBrush = NULL;
    static HBRUSH hWhiteBrush = NULL;
    static HFONT hFontTitle = NULL;
    static HFONT hFontNormal = NULL;

    switch (msg) {
        case WM_CREATE: {
            hBackgroundBrush = CreateSolidBrush(RGB(0x1a, 0x2a, 0x4a));
            hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));

            LOGFONT lf = {0};
            lf.lfHeight = 22;
            lf.lfWeight = FW_BOLD;
            strcpy(lf.lfFaceName, "Segoe UI");
            hFontTitle = CreateFontIndirect(&lf);

            lf.lfHeight = 13;
            lf.lfWeight = FW_NORMAL;
            hFontNormal = CreateFontIndirect(&lf);

            HWND hTitleLabel = CreateWindow("STATIC", "Dezipper", WS_VISIBLE | WS_CHILD, 
                25, 15, 450, 35, hwnd, (HMENU)100, NULL, NULL);
            SendMessage(hTitleLabel, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
            
            HWND hSubtitleLabel = CreateWindow("STATIC", "Extract ZIP files & free disk space", WS_VISIBLE | WS_CHILD, 
                25, 48, 450, 20, hwnd, (HMENU)101, NULL, NULL);
            SendMessage(hSubtitleLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hZipLabel = CreateWindow("STATIC", "ZIP File:", WS_VISIBLE | WS_CHILD, 
                25, 85, 100, 20, hwnd, (HMENU)102, NULL, NULL);
            SendMessage(hZipLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hEditPath = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                25, 108, 380, 26, hwnd, (HMENU)1, NULL, NULL);
            SendMessage(hEditPath, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hEditPath, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(5, 5));

            HWND hBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                415, 108, 82, 26, hwnd, (HMENU)2, NULL, NULL);
            SendMessage(hBrowse, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassLabel = CreateWindow("STATIC", "Password (optional):", WS_VISIBLE | WS_CHILD, 
                25, 148, 150, 20, hwnd, (HMENU)103, NULL, NULL);
            SendMessage(hPassLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                25, 170, 470, 26, hwnd, (HMENU)10, NULL, NULL);
            SendMessage(hPassEdit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hPassEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(5, 5));

            HWND hCheckKeep = CreateWindow("BUTTON", "Keep original ZIP file", 
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
                35, 215, 300, 22, hwnd, (HMENU)3, NULL, NULL);
            SendMessage(hCheckKeep, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckPreserve = CreateWindow("BUTTON", "Preserve file timestamps", 
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
                35, 240, 300, 22, hwnd, (HMENU)4, NULL, NULL);
            SendMessage(hCheckPreserve, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckForce = CreateWindow("BUTTON", "Force overwrite existing files", 
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | WS_TABSTOP,
                35, 265, 300, 22, hwnd, (HMENU)5, NULL, NULL);
            SendMessage(hCheckForce, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hExtract = CreateWindow("BUTTON", "Extract ZIP", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                25, 305, 155, 40, hwnd, (HMENU)6, NULL, NULL);
            SendMessage(hExtract, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hQuit = CreateWindow("BUTTON", "Exit", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                195, 305, 80, 40, hwnd, (HMENU)7, NULL, NULL);
            SendMessage(hQuit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hStatus = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
                25, 360, 470, 20, hwnd, (HMENU)8, NULL, NULL);
            SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hProgress = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_LEFTNOWORDWRAP,
                25, 385, 470, 18, hwnd, (HMENU)9, NULL, NULL);
            SendMessage(hProgress, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            controls = (GUIControls *)malloc(sizeof(GUIControls));
            memset(controls, 0, sizeof(GUIControls));
            controls->hEditPath = hEditPath;
            controls->hBrowseButton = hBrowse;
            controls->hPassEdit = hPassEdit;
            controls->hCheckKeep = hCheckKeep;
            controls->hCheckPreserve = hCheckPreserve;
            controls->hCheckForce = hCheckForce;
            controls->hExtractButton = hExtract;
            controls->hQuitButton = hQuit;
            controls->hStaticStatus = hStatus;
            controls->hProgressBar = hProgress;
            controls->is_extracting = 0;

            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)controls);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            if (controls) {
                HWND hwndControl = (HWND)lParam;
                if (hwndControl == controls->hEditPath || hwndControl == controls->hPassEdit) {
                    break;
                }
            }
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0xcc, 0xcc, 0xcc));
            return (LRESULT)hBackgroundBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)hWhiteBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(255, 255, 255));
            SetTextColor(hdc, RGB(0, 0, 0));
            return (LRESULT)hWhiteBrush;
        }

        case WM_EXTRACTION_PROGRESS: {
            if (controls && lParam) {
                char *progress_msg = (char *)lParam;
                SetWindowText(controls->hProgressBar, progress_msg);
                free(progress_msg);
            }
            break;
        }

        case WM_EXTRACTION_COMPLETE: {
            if (controls) {
                controls->is_extracting = 0;
                EnableWindow(controls->hExtractButton, TRUE);
                EnableWindow(controls->hBrowseButton, TRUE);
                SetWindowText(controls->hStaticStatus, "Extraction complete!");
                SetWindowText(controls->hProgressBar, "");
                if (lParam) {
                    MessageBox(hwnd, (char *)lParam, "Dezipper", MB_OK | MB_ICONINFORMATION);
                    free((char *)lParam);
                }
            }
            break;
        }

        case WM_EXTRACTION_ERROR: {
            if (controls) {
                controls->is_extracting = 0;
                EnableWindow(controls->hExtractButton, TRUE);
                EnableWindow(controls->hBrowseButton, TRUE);
                SetWindowText(controls->hStaticStatus, "Extraction failed!");
                SetWindowText(controls->hProgressBar, "");
                if (lParam) {
                    MessageBox(hwnd, (char *)lParam, "Dezipper Error", MB_OK | MB_ICONERROR);
                    free((char *)lParam);
                }
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);

            if (wmId == 2) {
                OPENFILENAME ofn = {0};
                char filename[MAX_PATH] = {0};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "ZIP files\0*.zip\0All files\0*.*\0\0";
                ofn.lpstrFile = filename;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    if (controls) SetWindowText(controls->hEditPath, filename);
                }
            }
            else if (wmId == 6) {
                if (!controls || controls->is_extracting) break;

                char path[MAX_PATH];
                GetWindowText(controls->hEditPath, path, MAX_PATH);
                TrimSpaces(path);

                if (strlen(path) == 0) {
                    SetWindowText(controls->hStaticStatus, "Please select a ZIP file");
                    break;
                }
                if (!file_exists(path)) {
                    SetWindowText(controls->hStaticStatus, "File not found!");
                    break;
                }

                int keep_zip = SendMessage(controls->hCheckKeep, BM_GETCHECK, 0, 0) == BST_CHECKED;
                int preserve_time = SendMessage(controls->hCheckPreserve, BM_GETCHECK, 0, 0) == BST_CHECKED;
                int force_overwrite = SendMessage(controls->hCheckForce, BM_GETCHECK, 0, 0) == BST_CHECKED;

                char password[MAX_PATH] = {0};
                GetWindowText(controls->hPassEdit, password, MAX_PATH);
                TrimSpaces(password);

                EnableWindow(controls->hExtractButton, FALSE);
                EnableWindow(controls->hBrowseButton, FALSE);
                controls->is_extracting = 1;
                SetWindowText(controls->hStaticStatus, "Starting extraction...");
                SetWindowText(controls->hProgressBar, "Initializing...");

                ExtractionParams *params = (ExtractionParams *)malloc(sizeof(ExtractionParams));
                strncpy(params->zip_path, path, MAX_PATH - 1);
                params->zip_path[MAX_PATH - 1] = '\0';
                params->keep_zip = keep_zip;
                params->preserve_time = preserve_time;
                params->force_overwrite = force_overwrite;
                params->hwnd = hwnd;
                strncpy(params->password, password, MAX_PATH - 1);
                params->password[MAX_PATH - 1] = '\0';

                DWORD threadId;
                HANDLE hThread = CreateThread(NULL, 0, ExtractionThreadProc, params, 0, &threadId);
                if (hThread) CloseHandle(hThread);
                else {
                    controls->is_extracting = 0;
                    EnableWindow(controls->hExtractButton, TRUE);
                    EnableWindow(controls->hBrowseButton, TRUE);
                    SetWindowText(controls->hStaticStatus, "Failed to start extraction!");
                    free(params);
                }
            }
            else if (wmId == 7) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_DESTROY:
            if (controls) free(controls);
            if (hFontTitle) DeleteObject(hFontTitle);
            if (hFontNormal) DeleteObject(hFontNormal);
            if (hBackgroundBrush) DeleteObject(hBackgroundBrush);
            if (hWhiteBrush) DeleteObject(hWhiteBrush);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowGUI(void) {
    HINSTANCE hInstance = GetModuleHandle(NULL);

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = GUIWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "DezipperWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x1a, 0x2a, 0x4a));

    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, "DezipperWindow", "Dezipper v1.0.0",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 550, 480,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBox(NULL, "Failed to create window", "Dezipper Error", MB_OK | MB_ICONERROR);
        return;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <io.h>
#include <commdlg.h>

extern int create_directory(const char *path);
extern void normalize_path(char *path);
extern int file_exists(const char *path);
extern void get_zip_basename(const char *zip_path, char *output, size_t output_size);
extern void TrimSpaces(char *str);
extern int set_file_times(const char *path, unsigned int dostime, unsigned int dosdate);

DWORD WINAPI ExtractionThreadProc(LPVOID lpParam) {
    ExtractionParams *params = (ExtractionParams *)lpParam;
    
    FILE *zip_file = fopen(params->zip_path, "rb");
    if (!zip_file) {
        PostMessage(params->hwnd, WM_EXTRACTION_ERROR, 0, (LPARAM)strdup("Cannot open ZIP file"));
        free(params);
        return 1;
    }

    char output_dir[MAX_PATH_LEN];
    get_zip_basename(params->zip_path, output_dir, MAX_PATH_LEN);
    create_directory(output_dir);

    unsigned char signature[4];
    int entry_count = 0;
    unsigned long long total_extracted = 0;

    while (fread(signature, 1, 4, zip_file) == 4) {
        if (!(signature[0] == 0x50 && signature[1] == 0x4b && signature[2] == 0x03 && signature[3] == 0x04)) {
            break;
        }

        unsigned char header[26];
        if (fread(header, 1, 26, zip_file) != 26) break;

        unsigned short compression_method = header[4] | (header[5] << 8);
        unsigned int compressed_size = header[14] | (header[15] << 8) | (header[16] << 16) | (header[17] << 24);
        unsigned int uncompressed_size = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
        unsigned short filename_length = header[22] | (header[23] << 8);
        unsigned short extra_field_length = header[24] | (header[25] << 8);
        unsigned short last_mod_time = header[6] | (header[7] << 8);
        unsigned short last_mod_date = header[8] | (header[9] << 8);

        char *filename = (char *)malloc(filename_length + 1);
        fread(filename, 1, filename_length, zip_file);
        filename[filename_length] = '\0';
        fseek(zip_file, extra_field_length, SEEK_CUR);

        int is_directory = (filename_length > 0 && 
                          (filename[filename_length - 1] == '/' || 
                           filename[filename_length - 1] == '\\'));

        if (is_directory) {
            char dir_path[MAX_PATH_LEN];
            snprintf(dir_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
            for (char *p = dir_path; *p; p++) if (*p == '/') *p = '\\';
            size_t dir_len = strlen(dir_path);
            if (dir_len > 0 && (dir_path[dir_len-1] == '\\' || dir_path[dir_len-1] == '/')) {
                dir_path[dir_len-1] = '\0';
            }
            create_directory(dir_path);
            free(filename);
            entry_count++;
            continue;
        }

        if (compressed_size == 0) {
            char output_path[MAX_PATH_LEN];
            snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
            for (char *p = output_path; *p; p++) if (*p == '/') *p = '\\';
            char *last_slash = strrchr(output_path, '\\');
            if (last_slash) { *last_slash = '\0'; create_directory(output_path); *last_slash = '\\'; }
            FILE *out_file = fopen(output_path, "wb");
            if (out_file) {
                fclose(out_file);
                if (params->preserve_time) set_file_times(output_path, last_mod_time, last_mod_date);
            }
            free(filename);
            entry_count++;
            continue;
        }

        unsigned char *compressed_data = (unsigned char *)malloc(compressed_size);
        fread(compressed_data, 1, compressed_size, zip_file);

        unsigned char *file_data = NULL;
        unsigned int final_size = 0;

        if (compression_method == 0) {
            file_data = compressed_data;
            final_size = compressed_size;
        } else if (compression_method == 8) {
            file_data = (unsigned char *)malloc(uncompressed_size);
            if (file_data) {
                z_stream strm = {0};
                strm.zalloc = Z_NULL;
                strm.zfree = Z_NULL;
                strm.opaque = Z_NULL;
                strm.avail_in = compressed_size;
                strm.next_in = compressed_data;
                strm.avail_out = uncompressed_size;
                strm.next_out = file_data;
                inflateInit2(&strm, -15);
                inflate(&strm, Z_FINISH);
                inflateEnd(&strm);
                free(compressed_data);
                final_size = uncompressed_size;
            } else {
                free(compressed_data);
                free(filename);
                continue;
            }
        } else {
            free(compressed_data);
            free(filename);
            continue;
        }

        char output_path[MAX_PATH_LEN];
        snprintf(output_path, MAX_PATH_LEN, "%s\\%s", output_dir, filename);
        for (char *p = output_path; *p; p++) if (*p == '/') *p = '\\';

        char *last_slash = strrchr(output_path, '\\');
        if (last_slash) { *last_slash = '\0'; create_directory(output_path); *last_slash = '\\'; }

        FILE *out_file = fopen(output_path, "wb");
        if (out_file) {
            fwrite(file_data, 1, final_size, out_file);
            fclose(out_file);
            if (params->preserve_time) set_file_times(output_path, last_mod_time, last_mod_date);
        }

        if (compression_method != 0) free(file_data);
        free(filename);

        total_extracted += final_size;
        entry_count++;

        char *progress_msg = (char *)malloc(256);
        if (progress_msg) {
            snprintf(progress_msg, 256, "Extracting: %s", filename);
            PostMessage(params->hwnd, WM_EXTRACTION_PROGRESS, (WPARAM)entry_count, (LPARAM)progress_msg);
        }
    }

    fclose(zip_file);

    if (!params->keep_zip) {
        zip_file = fopen(params->zip_path, "rb");
        if (zip_file) {
            fseek(zip_file, -22, SEEK_END);
            unsigned char eocd[22];
            if (fread(eocd, 1, 22, zip_file) == 22 && eocd[0] == 0x50 && eocd[1] == 0x4b && 
                eocd[2] == 0x05 && eocd[3] == 0x06) {
                long cd_offset = eocd[16] | (eocd[17] << 8) | (eocd[18] << 16) | (eocd[19] << 24);
                fclose(zip_file);
                zip_file = fopen(params->zip_path, "r+b");
                if (zip_file) {
                    HANDLE hFile = (HANDLE)_get_osfhandle(fileno(zip_file));
                    SetFilePointer(hFile, cd_offset, NULL, FILE_BEGIN);
                    SetEndOfFile(hFile);
                    fclose(zip_file);
                }
            } else {
                fclose(zip_file);
            }
        }
    }

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
                25, 15, 350, 35, hwnd, (HMENU)100, NULL, NULL);
            SendMessage(hTitleLabel, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
            
            HWND hSubtitleLabel = CreateWindow("STATIC", "Extract ZIP files & free disk space", WS_VISIBLE | WS_CHILD, 
                25, 48, 350, 20, hwnd, (HMENU)101, NULL, NULL);
            SendMessage(hSubtitleLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hZipLabel = CreateWindow("STATIC", "ZIP File:", WS_VISIBLE | WS_CHILD, 
                25, 85, 100, 20, hwnd, (HMENU)102, NULL, NULL);
            SendMessage(hZipLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hEditPath = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                25, 108, 260, 26, hwnd, (HMENU)1, NULL, NULL);
            SendMessage(hEditPath, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hEditPath, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(5, 5));

            HWND hBrowse = CreateWindow("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                293, 108, 82, 26, hwnd, (HMENU)2, NULL, NULL);
            SendMessage(hBrowse, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassLabel = CreateWindow("STATIC", "Password (optional):", WS_VISIBLE | WS_CHILD, 
                25, 148, 150, 20, hwnd, (HMENU)103, NULL, NULL);
            SendMessage(hPassLabel, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hPassEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
                25, 170, 350, 26, hwnd, (HMENU)10, NULL, NULL);
            SendMessage(hPassEdit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
            SendMessage(hPassEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(5, 5));

            HWND hCheckKeep = CreateWindow("BUTTON", "Keep original ZIP file", 
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX | WS_TABSTOP,
                35, 215, 200, 22, hwnd, (HMENU)3, NULL, NULL);
            SendMessage(hCheckKeep, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckPreserve = CreateWindow("BUTTON", "Preserve file timestamps", 
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX | WS_TABSTOP,
                35, 240, 200, 22, hwnd, (HMENU)4, NULL, NULL);
            SendMessage(hCheckPreserve, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hCheckForce = CreateWindow("BUTTON", "Force overwrite existing files", 
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX | WS_TABSTOP,
                35, 265, 220, 22, hwnd, (HMENU)5, NULL, NULL);
            SendMessage(hCheckForce, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hExtract = CreateWindow("BUTTON", "Extract ZIP", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                25, 305, 155, 40, hwnd, (HMENU)6, NULL, NULL);
            SendMessage(hExtract, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hQuit = CreateWindow("BUTTON", "Exit", 
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                195, 305, 80, 40, hwnd, (HMENU)7, NULL, NULL);
            SendMessage(hQuit, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hStatus = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_LEFT,
                25, 360, 350, 20, hwnd, (HMENU)8, NULL, NULL);
            SendMessage(hStatus, WM_SETFONT, (WPARAM)hFontNormal, TRUE);

            HWND hProgress = CreateWindow("STATIC", "", WS_VISIBLE | WS_CHILD | SS_LEFT,
                25, 385, 350, 18, hwnd, (HMENU)9, NULL, NULL);
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
                GetWindowText(controls->hEditPassword, password, MAX_PATH);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 460,
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
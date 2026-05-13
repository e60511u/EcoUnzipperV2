#ifndef GUI_H
#define GUI_H

#include "common.h"
#include <windows.h>

#define WM_EXTRACTION_PROGRESS (WM_USER + 1)
#define WM_EXTRACTION_COMPLETE (WM_USER + 2)
#define WM_EXTRACTION_ERROR (WM_USER + 3)

typedef struct {
    HWND hStaticStatus;
    HWND hProgressText;
    HWND hProgressReal;
    HWND hExtractButton;
    HWND hPauseButton;
    HWND hBrowseButton;
    HWND hQuitButton;
    HWND hCheckKeep;
    HWND hCheckPreserve;
    HWND hCheckForce;
    HWND hEditPath;
    HWND hPassEdit;
    int is_extracting;
    int first_progress;
} GUIControls;

typedef struct {
    char zip_path[MAX_PATH_LEN];
    int keep_zip;
    int preserve_time;
    int force_overwrite;
    char password[MAX_PATH_LEN];
    HWND hwnd;
} ExtractionParams;

DWORD WINAPI ExtractionThreadProc(LPVOID lpParam);
LRESULT CALLBACK GUIWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowGUI(void);

#endif
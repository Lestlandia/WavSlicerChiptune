/* slicerGUI_win32.c: Win32 GUI for slicer.exe.
   USAGE: Select audio file & params; calls slicer.exe.
   Compile: gcc slicerGUI_win32.c -o slicerGUI_win32 -lcomctl32 -mwindows -fgnu89-inline
*/
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

// Control IDs
#define IDC_LABEL_FILEPATH      101
#define IDC_EDIT_FILEPATH       102
#define IDC_BUTTON_BROWSE       103
#define IDC_LABEL_BPM           104
#define IDC_EDIT_BPM            105
#define IDC_LABEL_RPB           106
#define IDC_EDIT_RPB            107
#define IDC_LABEL_ROWLEN        108
#define IDC_EDIT_ROWLEN         109
#define IDC_CHECK_HEX           110
#define IDC_LABEL_EXPLAIN       111
#define IDC_PROGRESS_LABEL      112
#define IDC_PROGRESS_BAR        113
#define IDC_BUTTON_SLICE        114

// Global handles for controls we need to read/manipulate.
HWND hEditFilepath, hEditBPM, hEditRowsPerBeat, hEditRowLen;
HWND hCheckHex, hProgressBar, hProgressText; // Added: static control to display progress text

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void InitCommonControlsOnce(void);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    InitCommonControlsOnce();
    // Register window class
    const char CLASS_NAME[] = "SlicerGUIClass";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, "Window Reg Failed!", "Error", MB_ICONERROR);
        return 1;
    }
    // Create main window
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, "Slicer for Furnace",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 320,
        NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, "Window Create Failed!", "Error", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void InitCommonControlsOnce(void)
{
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icex);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg) {
    case WM_CREATE: {
        DragAcceptFiles(hwnd, TRUE);
        // File controls
        CreateWindow("STATIC", "Filepath", WS_CHILD|WS_VISIBLE, 10, 10, 60, 20,
            hwnd, (HMENU)IDC_LABEL_FILEPATH, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        hEditFilepath = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", 
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 80, 10, 400, 20,
            hwnd, (HMENU)IDC_EDIT_FILEPATH, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        CreateWindow("BUTTON", "Browse", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            490, 8, 80, 24, hwnd, (HMENU)IDC_BUTTON_BROWSE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        // Param controls
        CreateWindow("STATIC", "BPM", WS_CHILD|WS_VISIBLE, 10, 50, 40, 20,
            hwnd, (HMENU)IDC_LABEL_BPM, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        hEditBPM = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "125",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 50, 50, 60, 20,
            hwnd, (HMENU)IDC_EDIT_BPM, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        CreateWindow("STATIC", "Rows per Beat", WS_CHILD|WS_VISIBLE, 120, 50, 80, 20,
            hwnd, (HMENU)IDC_LABEL_RPB, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        hEditRowsPerBeat = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "4",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 205, 50, 40, 20,
            hwnd, (HMENU)IDC_EDIT_RPB, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        CreateWindow("STATIC", "Row Length", WS_CHILD|WS_VISIBLE, 260, 50, 70, 20,
            hwnd, (HMENU)IDC_LABEL_ROWLEN, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        hEditRowLen = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "64",
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 335, 50, 50, 20,
            hwnd, (HMENU)IDC_EDIT_ROWLEN, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        // Naming checkbox & progress
        CreateWindow("BUTTON", "<-- Tick for Hex", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
            400, 50, 180, 20, hwnd, (HMENU)IDC_CHECK_HEX, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        hCheckHex = GetDlgItem(hwnd, IDC_CHECK_HEX);
        CreateWindow("STATIC", "Explanation: slicer usage.", WS_CHILD|WS_VISIBLE|SS_LEFT,
            10, 80, 580, 50, hwnd, (HMENU)IDC_LABEL_EXPLAIN, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        CreateWindow("STATIC", "Slicing Progress", WS_CHILD|WS_VISIBLE|SS_LEFT,
            10, 140, 300, 20, hwnd, (HMENU)IDC_PROGRESS_LABEL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
            10, 160, 580, 25, hwnd, (HMENU)IDC_PROGRESS_BAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0,100));
        SendMessage(hProgressBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(0,255,0)); // default green

        // Added: Create a static text control overlaying the progress bar
        hProgressText = CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 160, 580, 25,
            hwnd, (HMENU)115, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        CreateWindow("BUTTON", "Slice!", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            10, 195, 100, 30, hwnd, (HMENU)IDC_BUTTON_SLICE, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
        break;
    }
    case WM_DROPFILES: {
        char filePath[MAX_PATH];
        HDROP hDrop = (HDROP)wParam;
        if (DragQueryFile(hDrop, 0, filePath, MAX_PATH))
            SetWindowText(hEditFilepath, filePath);
        DragFinish(hDrop);
        break;
    }
    case WM_COMMAND: {
        switch(LOWORD(wParam)) {
        case IDC_BUTTON_BROWSE: {
            OPENFILENAME ofn = {0};
            char szFile[MAX_PATH] = "";
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFile   = szFile;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = "Audio Files\0*.wav;*.mp3;*.flac;*.ogg\0All Files\0*.*\0";
            ofn.lpstrTitle  = "Select an Audio File";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileName(&ofn))
                SetWindowText(hEditFilepath, szFile);
            break;
        }
        case IDC_BUTTON_SLICE: {
            char filePath[MAX_PATH], bpm[16], rpb[16], rowlen[16], namingMode[4];
            GetWindowText(hEditFilepath, filePath, MAX_PATH);
            GetWindowText(hEditBPM, bpm, sizeof(bpm));
            GetWindowText(hEditRowsPerBeat, rpb, sizeof(rpb));
            GetWindowText(hEditRowLen, rowlen, sizeof(rowlen));
            strcpy(namingMode, (SendMessage(hCheckHex, BM_GETCHECK, 0, 0)==BST_CHECKED) ? "HEX" : "DEC");
            if (!strlen(filePath)) { MessageBox(hwnd, "Select an audio file.", "Error", MB_ICONERROR); break; }
            if (!strlen(bpm)) strcpy(bpm,"125"); if (!strlen(rpb)) strcpy(rpb,"4"); if (!strlen(rowlen)) strcpy(rowlen,"64");
            SendMessage(hProgressBar, PBM_SETPOS, 0, 0);
            // Set progress bar to yellow and update text to "slicing..."
            SendMessage(hProgressBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(255,255,0));
            SetWindowText(hProgressText, "slicing...");

            char cmdLine[1024];
            snprintf(cmdLine, sizeof(cmdLine), "slicer.exe \"%s\" %s %s %s %s", filePath, bpm, rpb, rowlen, namingMode);
            SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
            HANDLE hRead, hWrite;
            if (!CreatePipe(&hRead, &hWrite, &sa, 0)) { MessageBox(hwnd, "Pipe error.", "Error", MB_ICONERROR); break; }
            SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFO si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            si.hStdOutput = hWrite; si.hStdError = hWrite;
            si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
            if (!CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                MessageBox(hwnd, "Failed to run slicer.exe.", "Error", MB_ICONERROR);
                CloseHandle(hWrite); CloseHandle(hRead); break;
            }
            CloseHandle(hWrite);
            char buffer[256]; DWORD bytesRead; int lastProgress = 0;
            while (ReadFile(hRead, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead) {
                buffer[bytesRead] = '\0';
                char *line = strtok(buffer, "\n");
                while (line) {
                    int cur, total;
                    if (sscanf(line, "Processing slice %d/%d:", &cur, &total) == 2) {
                        int progress = (int)(((double)cur / total) * 100);
                        if (progress > lastProgress) {
                            SendMessage(hProgressBar, PBM_SETPOS, progress, 0);
                            lastProgress = progress;
                        }
                    }
                    line = strtok(NULL, "\n");
                }
            }
            CloseHandle(hRead);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            if (lastProgress < 100)
                SendMessage(hProgressBar, PBM_SETPOS, 100, 0);
            // Once slicing is done, turn the bar green and update text
            SendMessage(hProgressBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(0,255,0));
            SetWindowText(hProgressText, "slicing done!");
            break;
        }
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

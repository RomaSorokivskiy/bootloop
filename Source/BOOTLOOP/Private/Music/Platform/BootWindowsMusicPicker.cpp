#include "Music/BootMusicPicker.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <shobjidl.h>
#include <wrl/client.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
TArray<FString> FBootMusicPicker::PickFiles(void* ParentWindowHandle)
{
    TArray<FString> Paths;
#if PLATFORM_WINDOWS
    // Unreal's game thread owns the native window/message pump. Never invoke from decoder workers.
    if (!ensureMsgf(IsInGameThread(), TEXT("Music file picker requires the game/UI thread"))) return Paths;
    const HRESULT Com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(Com)) return Paths;
    {
        Microsoft::WRL::ComPtr<IFileOpenDialog> Dialog;
        HRESULT Hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Dialog));
        if (SUCCEEDED(Hr))
        {
            DWORD Options = 0;
            Dialog->GetOptions(&Options);
            Dialog->SetOptions(Options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
            const COMDLG_FILTERSPEC Filters[] = {{L"Music (WAV, MP3)", L"*.wav;*.mp3"}};
            Dialog->SetFileTypes(1, Filters);
            Dialog->SetTitle(L"Choose music for BOOTLOOP.EXE");
            Hr = Dialog->Show(static_cast<HWND>(ParentWindowHandle));
            if (SUCCEEDED(Hr))
            {
                Microsoft::WRL::ComPtr<IShellItemArray> Items;
                if (SUCCEEDED(Dialog->GetResults(&Items)))
                {
                    DWORD Count = 0; Items->GetCount(&Count);
                    // Bound the request list before handing it to the import queue.
                    for (DWORD I = 0; I < FMath::Min<DWORD>(Count, 1024); ++I)
                    {
                        Microsoft::WRL::ComPtr<IShellItem> Item;
                        PWSTR Name = nullptr;
                        if (SUCCEEDED(Items->GetItemAt(I, &Item)) && SUCCEEDED(Item->GetDisplayName(SIGDN_FILESYSPATH, &Name)))
                            Paths.Add(FString(Name));
                        if (Name) CoTaskMemFree(Name);
                    }
                }
            }
        }
    }
    CoUninitialize();
#endif
    return Paths;
}

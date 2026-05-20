#pragma once
#include "xrGameSpy/xrGameSpy.h"

class XRGAMESPY_API CGameSpy_HTTP
{
public:
    using CompletionCallback = fastdelegate::FastDelegate<void(bool)>;
    using ProgressCallback = fastdelegate::FastDelegate<void(u64 received, u64 total)>;
    // Body is owned by ghttp's internal buffer and only valid for the duration
    // of the callback. Copy if you need to retain it.
    using StringCompletionCallback = fastdelegate::FastDelegate<void(bool success, const char* body, u32 length)>;

    CGameSpy_HTTP();
    ~CGameSpy_HTTP();

    void StartUp();
    void CleanUp();

    void DownloadFile(LPCSTR URL, LPCSTR FileName, CompletionCallback& completed, ProgressCallback& progress);
    void FetchString(LPCSTR URL, StringCompletionCallback& completed);
    void StopDownload();
    void Think();

private:
    GHTTPRequest m_LastRequest;
};

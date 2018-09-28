#include "httpd.h"
#include <Dbghelp.h> 


#pragma comment(lib, "Dbghelp.lib")

LONG __stdcall crush_callback(struct _EXCEPTION_POINTERS* ep)
{
    time_t t;
    struct tm *p;
    char fname[MAX_PATH] = {0};
    MINIDUMP_EXCEPTION_INFORMATION    exceptioninfo;
    HANDLE hFile;

    t = time(NULL) + 8 * 3600;
    p = gmtime(&t);

    sprintf(fname, "dump_%d-%d-%d_%d_%d_%d.DMP", 1900+p->tm_year, 1+p->tm_mon, p->tm_mday, (p->tm_hour)%24, p->tm_min, p->tm_sec);

    hFile = CreateFileA(fname,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    exceptioninfo.ExceptionPointers = ep;
    exceptioninfo.ThreadId          = GetCurrentThreadId();
    exceptioninfo.ClientPointers    = FALSE;

    if (!MiniDumpWriteDump(GetCurrentProcess(),
        GetCurrentProcessId(),
        hFile,
        MiniDumpWithFullMemory,
        &exceptioninfo,
        NULL,
        NULL))
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CloseHandle(hFile);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main()
{
    UINT16 port = 80;
    SetUnhandledExceptionFilter(crush_callback);
    
    http_startup(&port);
    system("pause");
    return 0;
}

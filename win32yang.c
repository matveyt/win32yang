/*
 * win32yang - Clipboard tool for Windows
 * Last Change:  2024 Dec 16
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/win32yang
 */


#if defined(UNICODE) && !defined(_UNICODE)
#define _UNICODE
#endif // UNICODE

#define WIN32_LEAN_AND_MEAN
#include <stdbool.h>
#include <stdint.h>
#include <tchar.h>
#include <windows.h>


// forward prototypes
static void* stdio_read(size_t* psz, bool crlf);
static void stdio_write(void* ptr, size_t sz, bool lf);
static HANDLE mb2wc(uint32_t cp, const void* pSrc, size_t cchSrc);
static void* wc2mb(uint32_t cp, HANDLE hUCS, size_t* psz);
static void* heap_alloc(void* ptr, size_t sz);
static void heap_free(void* ptr);


int _tmain(int argc, _TCHAR* argv[])
{
    int action = 0;
    bool crlf = false, lf = false;
    uint32_t cp = CP_UTF8;

    for (int optind = 1; optind < argc; ++optind) {
        const _TCHAR* optarg = argv[optind];
        if (*optarg++ == _T('-')) {
            switch (*optarg++) {
            case _T('i'):
            case _T('o'):
            case _T('x'):
                if (optarg[0] == 0)
                    action = optarg[-1];
            break;
            case _T('-'):
                if (!lstrcmp(optarg, _T("crlf")))
                    crlf = true;
                else if (!lstrcmp(optarg, _T("lf")))
                    lf = true;
                else if (!lstrcmp(optarg, _T("acp")))
                    cp = GetACP();
                else if (!lstrcmp(optarg, _T("oem")))
                    cp = GetOEMCP();
                else if (!lstrcmp(optarg, _T("utf8")))
                    cp = CP_UTF8;
            break;
            }
        }
    }

    switch (action) {
        HANDLE hUCS;
        void* ptr;
        size_t sz;

    case _T('i'):
        // stdin => clipboard
        ptr = stdio_read(&sz, crlf);
        hUCS = mb2wc(cp, ptr, sz);
        heap_free(ptr);
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, hUCS) == NULL)
                GlobalFree(hUCS);   // release HANDLE on failure
            CloseClipboard();
        }
    break;

    case _T('o'):
        // clipboard => stdout
        if (OpenClipboard(NULL)) {
            hUCS = GetClipboardData(CF_UNICODETEXT);
            if (hUCS == NULL) {
                CloseClipboard();
                break;
            }
            ptr = wc2mb(cp, hUCS, &sz);
            CloseClipboard();
            stdio_write(ptr, sz, lf);
            heap_free(ptr);
        }
    break;

    case _T('x'):
        // delete clipboard
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
        }
    break;

    default:
#define STR(a) (a), (sizeof(a) - sizeof(*a))
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), STR(
            "Invalid arguments\n\n"
            "Usage:\n"
            "\twin32yang -i [--crlf]\n"
            "\twin32yang -o [--lf]\n"
            "\twin32yang -x\n"
            "\n"
            "Options:\n"
            "\t-i\t\tSet clipboard from stdin\n"
            "\t-o\t\tPrint clipboard contents to stdout\n"
            "\t-x\t\tDelete clipboard\n"
            "\t--crlf\t\tReplace lone LF bytes with CRLF before setting the clipboard\n"
            "\t--lf\t\tReplace CRLF with LF before printing to stdout\n"
            "\t--acp\t\tAssume CP_ACP (system ANSI code page) encoding\n"
            "\t--oem\t\tAssume CP_OEMCP (OEM code page) encoding\n"
            "\t--utf8\t\tAssume CP_UTF8 encoding (default)\n"
        ), &(DWORD){0}, NULL);
    break;
    }

    return 0;
}


// stdin => buffer (heap_alloc)
void* stdio_read(size_t* psz, bool crlf)
{
    void* ptr = NULL;
    uint8_t* pOut = NULL;
    size_t szDone = 0, szHole = 0, szTail = 0;
    size_t szIncr = 2048;

    for (;;) {
        // ptr => szDone + szHole + szTail
        //        pOut---^        ^---pIn
        // szHole is a number of extra bytes between pOut and pIn
        // reserved for LF => CRLF expansion
        // if crlf == FALSE then szHole = 0; otherwise szHole = szIncr >= cbRead
        // szTail is a number of free bytes at the end of a buffer
        // to make room for ReadFile(): szTail >= szIncr >= cbRead

        size_t szIncr2 = szIncr + (crlf ? szIncr : 0);
        if (szHole + szTail < szIncr2) {
            // grow buffer
            szIncr += szIncr;
            szTail += szIncr2 + szIncr2;
            ptr = heap_alloc(ptr, szDone + szHole + szTail);
            pOut = (uint8_t*)ptr + szDone;
        }

        if (crlf && szHole < szIncr) {
            // grow hole
            szTail -= szIncr - szHole;
            szHole = szIncr;
        }

        // read szIncr bytes
        uint8_t* pIn = pOut + szHole;
        DWORD cbRead;
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), pIn, (DWORD)szIncr, &cbRead, NULL);
        // test EOF or error
        if (cbRead == 0)
            break;
        szDone += cbRead;
        szTail -= cbRead;

        if (crlf) {
            // LF => CRLF
            int c1 = 0;
            do {
                int c = *pIn++;
                if (c1 == '\r' || c != '\n') {
                    *pOut++ = (uint8_t)c;
                } else {
                    *pOut++ = '\r';
                    *pOut++ = '\n';
                    --szHole;
                    ++szDone;
                }
                c1 = c;
            } while (--cbRead);
        } else {
            pOut += cbRead;
        }
    }

    return *psz = szDone, ptr;
}


// buffer => stdout
void stdio_write(void* ptr, size_t sz, bool lf)
{
    uint8_t* pOut = ptr;

    if (lf) {
        // CRLF => LF
        uint8_t* pIn;
        size_t szTail;
        for (pIn = pOut, szTail = sz; szTail >= 2; --szTail) {
            // at least two bytes left to look ahead
            int c = *pIn++;
            if (c != '\r' || *pIn != '\n') {
                *pOut++ = (uint8_t)c;
            } else {
                *pOut++ = '\n';
                ++pIn;
                --szTail;
                --sz;
            }
        }
        // pass last byte through
        if (szTail > 0)
            *pOut++ = *pIn;
    } else {
        pOut += sz;
    }

    // chop trailing zeroes
    if (sz > 0)
        while (*--pOut == 0 && --sz) ;

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ptr, (DWORD)sz, &(DWORD){0}, NULL);
}


// MultiByte => WideChar (GlobalAlloc)
HANDLE mb2wc(uint32_t cp, const void* pSrc, size_t cchSrc)
{
    int cchDst = MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, NULL, 0) + 1;
    HANDLE hUCS = GlobalAlloc(GHND, sizeof(WCHAR) * cchDst);
    MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, GlobalLock(hUCS), cchDst);
    GlobalUnlock(hUCS);
    return hUCS;
}


// WideChar => MultiByte (heap_alloc)
void* wc2mb(uint32_t cp, HANDLE hUCS, size_t* psz)
{
    const void* pSrc = GlobalLock(hUCS);
    int cchSrc = GlobalSize(hUCS) / sizeof(WCHAR);
    int cchDst = WideCharToMultiByte(cp, 0, pSrc, cchSrc, NULL, 0, NULL, NULL) + 1;
    void* ptr = heap_alloc(NULL, cchDst);
    cchDst = WideCharToMultiByte(cp, 0, pSrc, cchSrc, ptr, cchDst, NULL, NULL);
    GlobalUnlock(hUCS);
    return *psz = (size_t)cchDst, ptr;
}


// heap allocation
static inline void* heap_alloc(void* ptr, size_t sz)
{
    return ptr ? HeapReAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, ptr, sz)
        : HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, sz);
}

static inline void heap_free(void* ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}


// micro CRT startup code
#if __has_include("nocrt0c.c")
#define ARGV builtin
#include "nocrt0c.c"
#endif

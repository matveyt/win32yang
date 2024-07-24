/*
 * win32yang - Clipboard tool for Windows
 * Last Change:  2024 Jul 24
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/win32yang
 */


#include <stdbool.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


// forward prototypes
static void* stdio_read(size_t* psz, bool crlf);
static void stdio_write(void* ptr, size_t sz, bool lf);
static HANDLE mb2wc(uint32_t cp, const void* pSrc, size_t cchSrc);
static void* wc2mb(uint32_t cp, HANDLE hUCS, size_t* psz);
static void* mem_alloc(size_t sz);
static void* mem_realloc(void* ptr, size_t sz);
static void mem_free(void* ptr);


int main(int argc, char* argv[])
{
    int action = 0;
    bool crlf = false, lf = false;
    uint32_t cp = CP_UTF8;

    for (int optind = 1; optind < argc; ++optind) {
        const char* opt = argv[optind];
        if (*opt++ == '-') {
            switch (*opt++) {
            case 'i':
            case 'o':
            case 'x':
                if (opt[0] == 0)
                    action = opt[-1];
            break;
            case '-':
                if (!lstrcmpA(opt, "crlf"))
                    crlf = true;
                else if (!lstrcmpA(opt, "lf"))
                    lf = true;
                else if (!lstrcmpA(opt, "acp"))
                    cp = GetACP();
                else if (!lstrcmpA(opt, "oem"))
                    cp = GetOEMCP();
                else if (!lstrcmpA(opt, "utf8"))
                    cp = CP_UTF8;
            break;
            }
        }
    }

    switch (action) {
        HANDLE hUCS;
        void* ptr;
        size_t sz;

    case 'i':
        // stdin => clipboard
        ptr = stdio_read(&sz, crlf);
        hUCS = mb2wc(cp, ptr, sz);
        mem_free(ptr);
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, hUCS) == NULL)
                GlobalFree(hUCS);   // release HANDLE on failure
            CloseClipboard();
        }
    break;

    case 'o':
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
            mem_free(ptr);
        }
    break;

    case 'x':
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


// allocate buffer to read from stdin
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
            ptr = mem_realloc(ptr, szDone + szHole + szTail);
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
        ReadFile(GetStdHandle(STD_INPUT_HANDLE), pIn, szIncr, &cbRead, NULL);
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
                    *pOut++ = c;
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


// write buffer to stdout
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
                *pOut++ = c;
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

    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ptr, sz, &(DWORD){0}, NULL);
}


// MultiByte => WideChar (HGLOBAL)
HANDLE mb2wc(uint32_t cp, const void* pSrc, size_t cchSrc)
{
    int cchDst = MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, NULL, 0) + 1;
    HANDLE hUCS = GlobalAlloc(GHND, sizeof(WCHAR) * cchDst);
    MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, GlobalLock(hUCS), cchDst);
    GlobalUnlock(hUCS);
    return hUCS;
}


// WideChar => MultiByte
void* wc2mb(uint32_t cp, HANDLE hUCS, size_t* psz)
{
    const void* pSrc = GlobalLock(hUCS);
    int cchSrc = GlobalSize(hUCS) / sizeof(WCHAR);
    int cchDst = WideCharToMultiByte(cp, 0, pSrc, cchSrc, NULL, 0, NULL, NULL) + 1;
    void* ptr = mem_alloc(cchDst);
    cchDst = WideCharToMultiByte(cp, 0, pSrc, cchSrc, ptr, cchDst, NULL, NULL);
    GlobalUnlock(hUCS);
    return *psz = (size_t)cchDst, ptr;
}


// heap memory allocation
static HANDLE g_hHeap = NULL;
inline void* mem_alloc(size_t sz)
{
    if (g_hHeap == NULL)
        g_hHeap = GetProcessHeap();
    return HeapAlloc(g_hHeap, HEAP_GENERATE_EXCEPTIONS, sz);
}
inline void* mem_realloc(void* ptr, size_t sz)
{
    return ptr ? HeapReAlloc(g_hHeap, HEAP_GENERATE_EXCEPTIONS, ptr, sz) : mem_alloc(sz);
}
inline void mem_free(void* ptr)
{
    HeapFree(g_hHeap, 0, ptr);
}


// ---micro startup code---
#ifdef __GNUC__
// from msvcrt.dll
typedef struct { int newmode; } _startupinfo;
extern void __getmainargs(int*, char***, char***, int, _startupinfo*);
extern void __set_app_type(int);

void __main(void) {}

__declspec(noreturn)
void mainCRTStartup(void)
{
    int argc;
    char** argv;

    __set_app_type(1);  // _CONSOLE_APP
    __getmainargs(&argc, &argv, &(char**){NULL}, 0, &(_startupinfo){0});
    ExitProcess(main(argc, argv));
}
#endif // __GNUC__

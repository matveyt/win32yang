/*
 * win32yang - Clipboard tool for Windows
 * Last Change:  2024 Jul 22
 * License:      https://unlicense.org
 * URL:          https://github.com/matveyt/win32yang
 */


#define WIN32_LEAN_AND_MEAN
#include <windows.h>


// forward prototypes
static void* read_file(HANDLE h, BOOL crlf, size_t* psz);
static BOOL write_file(HANDLE h, BOOL lf, void* pBuf, size_t sz);
static HANDLE mb2wc(UINT cp, const void* pSrc, size_t cchSrc);
static void* wc2mb(UINT cp, const void* pSrc, size_t cchSrc, size_t* psz);
static void set_nolock(UINT uFormat, HANDLE hData);
static HANDLE get_and_lock(UINT uFormat, void* ppData, size_t* psz);
static void* mem_alloc(size_t sz);
static void* mem_realloc(void* ptr, size_t sz);
static void mem_free(void* ptr);


int wmain(int argc, wchar_t* argv[])
{
    int dir = 0;
    BOOL lf = FALSE, crlf = FALSE;
    UINT cp = CP_UTF8;

    for (int i = 1; i < argc; ++i) {
        PCWSTR p = argv[i];
        if (*p++ == L'-') {
            switch (*p++) {
            case L'i':
            case L'o':
            case L'x':
                if (p[0] == 0)
                    dir = p[-1];
            break;
            case L'-':
                if (!lstrcmpW(p, L"lf"))
                    lf = TRUE;
                else if (!lstrcmpW(p, L"crlf"))
                    crlf = TRUE;
                else if (!lstrcmpW(p, L"acp"))
                    cp = GetACP();
                else if (!lstrcmpW(p, L"oem"))
                    cp = GetOEMCP();
                else if (!lstrcmpW(p, L"utf8"))
                    cp = CP_UTF8;
            break;
            }
        }
    }

    switch (dir) {
        HANDLE hData;
        void* pBuf;
        size_t sz;

    case L'i':
        // stdin => clipboard
        pBuf = read_file(GetStdHandle(STD_INPUT_HANDLE), crlf, &sz);
        hData = mb2wc(cp, pBuf, sz);
        mem_free(pBuf);
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            set_nolock(CF_UNICODETEXT, hData);
            CloseClipboard();
        }
    break;

    case L'o':
        // clipboard => stdout
        pBuf = NULL, sz = 0;
        if (OpenClipboard(NULL)) {
            void* pBufGlobal;
            size_t szGlobal;
            hData = get_and_lock(CF_UNICODETEXT, &pBufGlobal, &szGlobal);
            if (hData != NULL) {
                pBuf = wc2mb(cp, pBufGlobal, szGlobal / sizeof(WCHAR), &sz);
                GlobalUnlock(hData);
            }
            CloseClipboard();
        }
        write_file(GetStdHandle(STD_OUTPUT_HANDLE), lf, pBuf, sz);
        mem_free(pBuf);
    break;

    case L'x':
        // delete clipboard
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
        }
    break;

    default:
#define ARRAY(a) (a), sizeof(a)
        write_file(GetStdHandle(STD_ERROR_HANDLE), FALSE, ARRAY(
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
            "\t--lf\t\tReplace CRLF with LF before printing to stdout\n"
            "\t--crlf\t\tReplace lone LF bytes with CRLF before setting the clipboard\n"
            "\t--acp\t\tAssume CP_ACP (system ANSI code page) encoding\n"
            "\t--oem\t\tAssume CP_OEMCP (OEM code page) encoding\n"
            "\t--utf8\t\tAssume CP_UTF8 encoding (default)\n"
        ));
    break;
    }

    return 0;
}


// allocate buffer and read file into it
static void* read_file(HANDLE h, BOOL crlf, size_t* psz)
{
    PBYTE pBuf = NULL, pbOut = NULL;
    size_t szDone = 0, szHole = 0, szTail = 0;
    size_t szIncr = 2048;

    for (;;) {
        // pBuf => szDone + szHole + szTail
        //        pbOut---^        ^---pbIn
        // szHole is a number of extra bytes between pbOut and pbIn
        // reserved for LF => CRLF expansion
        // if crlf == FALSE then szHole = 0; otherwise szHole = szIncr >= cbRead
        // szTail is a number of free bytes at the end of a buffer
        // to make room for ReadFile(): szTail >= szIncr >= cbRead

        size_t szIncr2 = szIncr + (crlf ? szIncr : 0);
        if (szHole + szTail < szIncr2) {
            // grow buffer
            szIncr += szIncr;
            szTail += szIncr2 + szIncr2;
            pBuf = mem_realloc(pBuf, szDone + szHole + szTail);
            pbOut = pBuf + szDone;
        }

        if (crlf && szHole < szIncr) {
            // grow hole
            szTail -= szIncr - szHole;
            szHole = szIncr;
        }

        // read szIncr bytes
        PBYTE pbIn = pbOut + szHole;
        DWORD cbRead;
        ReadFile(h, pbIn, (DWORD)szIncr, &cbRead, NULL);
        // test EOF or error
        if (cbRead == 0)
            break;
        szDone += cbRead;
        szTail -= cbRead;

        if (crlf) {
            // LF => CRLF
            int c1 = 0;
            do {
                int c = *pbIn++;
                if (c1 == '\r' || c != '\n') {
                    *pbOut++ = c;
                } else {
                    *pbOut++ = '\r';
                    *pbOut++ = '\n';
                    --szHole;
                    ++szDone;
                }
                c1 = c;
            } while (--cbRead);
        } else {
            pbOut += cbRead;
        }
    }

    return *psz = szDone, pBuf;
}


// write buffer to file
// if lf == TRUE then buffer must be writable
static BOOL write_file(HANDLE h, BOOL lf, void* pBuf, size_t sz)
{
    PBYTE pbOut = pBuf;

    if (lf) {
        // CRLF => LF
        PBYTE pbIn;
        size_t szTail;
        for (pbIn = pbOut, szTail = sz; szTail >= 2; --szTail) {
            // at least two bytes left to look ahead
            int c = *pbIn++;
            if (c != '\r' || *pbIn != '\n') {
                *pbOut++ = c;
            } else {
                *pbOut++ = '\n';
                ++pbIn;
                --szTail;
                --sz;
            }
        }
        // pass last byte through
        if (szTail > 0)
            *pbOut++ = *pbIn;
    } else {
        pbOut += sz;
    }

    // chop trailing zeroes
    if (sz > 0)
        while (*--pbOut == 0 && --sz) ;

    return sz ? WriteFile(h, pBuf, (DWORD)sz, &(DWORD){0}, NULL) : FALSE;
}


// MultiByte to WideChar (HGLOBAL)
static HANDLE mb2wc(UINT cp, const void* pSrc, size_t cchSrc)
{
    int cchDst = MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, NULL, 0) + 1;
    HANDLE hBuf = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * cchDst);
    MultiByteToWideChar(cp, 0, pSrc, (int)cchSrc, GlobalLock(hBuf), cchDst);
    GlobalUnlock(hBuf);
    return hBuf;
}

// WideChar to MultiByte (HEAP)
static void* wc2mb(UINT cp, const void* pSrc, size_t cchSrc, size_t* psz)
{
    int cchDst = WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, NULL, 0, NULL, NULL) + 1;
    void* pBuf = mem_alloc(cchDst);
    cchDst = WideCharToMultiByte(cp, 0, pSrc, (int)cchSrc, pBuf, cchDst, NULL, NULL);
    return *psz = (size_t)cchDst, pBuf;
}


// safe set clipboard format
static inline void set_nolock(UINT uFormat, HANDLE hData)
{
    if (SetClipboardData(uFormat, hData) == NULL)
        GlobalFree(hData);
}

// safe get clipboard format
static inline HANDLE get_and_lock(UINT uFormat, void* ppData, size_t* psz)
{
    HANDLE hData = GetClipboardData(uFormat);

    if (hData != NULL) {
        *(void**)ppData = GlobalLock(hData);
        *psz = GlobalSize(hData);
    }

    return hData;
}


// process heap
static HANDLE g_hHeap = NULL;

static inline void* mem_alloc(size_t sz)
{
    if (g_hHeap == NULL)
        g_hHeap = GetProcessHeap();
    return HeapAlloc(g_hHeap, HEAP_GENERATE_EXCEPTIONS, sz);
}

static inline void* mem_realloc(void* ptr, size_t sz)
{
    return ptr ? HeapReAlloc(g_hHeap, HEAP_GENERATE_EXCEPTIONS, ptr, sz) : mem_alloc(sz);
}

static inline void mem_free(void* ptr)
{
    HeapFree(g_hHeap, 0, ptr);
}

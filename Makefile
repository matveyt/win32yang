#
# win32yang - Clipboard tool for Windows
# Last Change:    2024 Jul 21
# License:        https://unlicense.org
# URL:            https://github.com/matveyt/win32yang
#

CPPFLAGS := -DNDEBUG
CFLAGS := -O3 -Wall -Wextra -Wpedantic -Werror
LDFLAGS := -s -fno-ident -Wl,--gc-sections -municode

NOCRT0 := $(wildcard nocrt0c.c)
ifdef NOCRT0
    CPPFLAGS += -D_UNICODE -DARGV=msvcrt
    LDFLAGS += -nostdlib
    LDLIBS += -lkernel32 -luser32 -lmsvcrt
endif

win32yang.exe: win32yang.c $(NOCRT0)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

#
# win32yang - Clipboard tool for Windows
# Last Change:    2024 Jul 24
# License:        https://unlicense.org
# URL:            https://github.com/matveyt/win32yang
#

CFLAGS := -O -std=c99 -Wall -Wextra -Wpedantic -Werror
LDFLAGS := -s -fno-ident -nostartfiles
.DEFAULT_GOAL := win32yang

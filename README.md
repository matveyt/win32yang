### What is win32yang

It is a clipboard tool for Windows. A drop-in replacement for
[win32yank](https://github.com/equalsraf/win32yank).

Set the clipboard

```bat
help | win32yang -i --oem
```

Get the clipboard

```bat
win32yang -o --utf8 >clipboard.txt
```

### What are the advantages

Unfortunately, it is not much faster than the original `win32yank`. The bottleneck is a
process creation time. However, `win32yang`

* is very small
* arguably, few percent faster
* arguably, more stable
* written in pure C, no Rust needed
* supports ACP and OEM code pages as well as UTF-8

### How to compile

Just `make` it!

### Synopsis

```
win32yang -i [--crlf]
win32yang -o [--lf]
win32yang -x

-i      Set clipboard from stdin
-o      Print clipboard contents to stdout
-x      Delete clipboard
--crlf  Replace lone LF bytes with CRLF before setting the clipboard
--lf    Replace CRLF with LF before printing to stdout
--acp   Assume CP_ACP (system ANSI code page) encoding
--oem   Assume CP_OEMCP (OEM code page) encoding
--utf8  Assume CP_UTF8 encoding (default)
```

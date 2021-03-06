
proc console_print(text: cstring, tlen: int) {.importc, header: "ffi.h"}
proc exit(status: int) {.importc, header: "ffi.h"}
proc remote_breakpoint*(port: int = 2159) {.importc, header: "ffi.h"}

proc print*(content: string) =
    console_print(content, len(content))

proc exit*() {.used.} =
    exit(-1)

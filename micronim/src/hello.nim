proc hello_nim() {.cdecl, exportc.}
import json
import ffi

var j = %* {
    "name": "Hello",
    "email": "World",
    "books": ["Foundation"]
}

proc hello_nim() =
    print "Hello Nim World!\n" & j.pretty()

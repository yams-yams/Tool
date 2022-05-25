#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/memory.h>
#include <windows.h>
#include <assert.h>

CAMLprim value
caml_wait_for_changes( value vpath, value closure){
    
    char path[] = String_val(vpath);
    
    printf("watching %s for changes...\n", path);

    HANDLE hDir = CreateFile(path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    
    //ensures the path was valid and a handle could be produced
    assert(hDir != INVALID_HANDLE_VALUE);
    OVERLAPPED overlapped;
    overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);

    uint8_t change_buf[1024];
    BOOL success = ReadDirectoryChangesW(
        hDir, change_buf, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, &overlapped, NULL);

    while (true) {
        DWORD result = WaitForSingleObject(overlapped.hEvent, 0);

        if (result == WAIT_OBJECT_0) {
            DWORD bytes_transferred;
            GetOverlappedResult(hDir, &overlapped, &bytes_transferred, FALSE);

            FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*)change_buf;

            for (;;) {
                DWORD name_len = event->FileNameLength / sizeof(wchar_t);
                
                //create two ocaml local variables
                CAMLlocal2(action, filename);
                action = event->Action;
                filename = 
                
                //callback function
                caml_callback2(closure, action, filename);

            
                if (event->NextEntryOffset) {
                    *((uint8_t**)&event) += event->NextEntryOffset;
                } else {
                    break;
                }
            }
        }
    }
    return Val_unit;

}


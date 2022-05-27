#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <caml/mlvalues.h>
#include <caml/alloc.h>
#include <caml/fail.h>
#include <caml/callback.h>
#include <caml/osdeps.h>
#include <caml/memory.h>
#include <windows.h>
#include <assert.h>

CAMLprim value
caml_wait_for_changes( value vpath, value closure){
    
    CAMLparam2(vpath, closure);    
    CAMLlocal2(action, filename);
    
    const char *path = String_val(vpath);
    
    printf("watching %s for changes...\n", path);

    HANDLE hDir = CreateFile(path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);
    
    //Raises exception if directory cannot be opened
    if (hDir == INVALID_HANDLE_VALUE){
        //close handle
        CloseHandle(hDir);

        //raise caml exception
        caml_failwith("Directory cannot be found.");
        
    }

    OVERLAPPED overlapped;

    //close handle raise exception
    overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);
    
    if (overlapped.hEvent == NULL){
        CloseHandle(hDir);
        caml_failwith("Handle to event could not be created.");
    }


    uint8_t change_buf[1024];
    
    while (true) {

        BOOL success = ReadDirectoryChangesW(
            hDir, change_buf, 1024, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME  |
            FILE_NOTIFY_CHANGE_DIR_NAME   |
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL, &overlapped, NULL);
        
        //Does not raise exception if directory changes cannot be read
        /*if (success == false) {
            CloseHandle(hDir);
            CloseHandle(overlapped.hEvent);
            caml_failwith("Directory changes cannot be read.");
        }*/

        DWORD result = WaitForSingleObject(overlapped.hEvent, 0);
        
        //Raises exception if WaitForSingleObject fails        
        if (result == WAIT_FAILED) {
            CloseHandle(hDir);
            CloseHandle(overlapped.hEvent);
            caml_failwith("WaitForSingleObject function has failed.");
        }

        else if (result == WAIT_OBJECT_0) {
            DWORD bytes_transferred;
            GetOverlappedResult(hDir, &overlapped, &bytes_transferred, FALSE);
            FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*)change_buf;

            for (;;) {
                DWORD name_len = event->FileNameLength / sizeof(wchar_t);
                
                //create two ocaml local variables
                switch (event->Action){

                    case FILE_ACTION_ADDED: {
                        action = Val_int(0);
                    } break;
                    case FILE_ACTION_REMOVED: {
                        action = Val_int(1);
                    } break;
                    case FILE_ACTION_MODIFIED: {
                        action = Val_int(2);
                    } break;
                    case FILE_ACTION_RENAMED_OLD_NAME: {
                        action = Val_int(3);
                    } break;
                    case FILE_ACTION_RENAMED_NEW_NAME: {
                        action = Val_int(4);
                    } break;
                    
                }
                
                wchar_t *name = malloc(2 * (name_len + 1));
                memcpy(name, event->FileName, 2 * name_len);
                name[name_len] = 0;
                
                filename = caml_copy_string_of_os(name);
                
                free(name);

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
    
    CAMLreturn(Val_unit);

}


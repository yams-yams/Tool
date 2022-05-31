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
#include <caml/unixsupport.h>
#include <windows.h>
#include <assert.h>

struct myData {
    char *buffer;
    value closure;
    HANDLE *hDir;
};

void LpoverlappedCompletionRoutine(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped){
    CAMLparam0();
    CAMLlocal2(filename, action);
    struct myData *data;
    data = (struct myData*)lpOverlapped->hEvent;
    FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*)data->buffer;
    for (;;) {

        DWORD name_len = event->FileNameLength / sizeof(wchar_t);

        //create two ocaml local variables
        switch (event->Action){

            case FILE_ACTION_ADDED: 
                action = Val_int(0); 
                break;
            case FILE_ACTION_REMOVED: 
                action = Val_int(1);
                break;
            case FILE_ACTION_MODIFIED:
                action = Val_int(2);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                action = Val_int(3);
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                action = Val_int(4);
                break;

        }

        wchar_t *name = malloc(2 * (name_len + 1));
        memcpy(name, event->FileName, 2 * name_len);
        name[name_len] = 0;

        filename = caml_copy_string_of_os(name);

        free(name);

        //callback function
        caml_callback2(data->closure, action, filename);


        if (event->NextEntryOffset) {
            *((uint8_t**)&event) += event->NextEntryOffset;
        } else {
            break;
        }
    }
    
    uint8_t change_buf[1024];
    OVERLAPPED overlapped;
    struct myData nextData;
    nextData.buffer = change_buf;
    nextData.closure = data->closure;
    nextData.hDir = data->hDir;
    overlapped.hEvent = &nextData;

    BOOL success = ReadDirectoryChangesW(
        data->hDir, change_buf, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, &overlapped, &LpoverlappedCompletionRoutine);

    //Does not raise exception if directory changes cannot be read
    if (success == false) {
        CloseHandle(data->hDir);

        win32_maperr(GetLastError());
        uerror("ReadDirectoryChangesW", Nothing);
    }
    
    //printf("About to sleep again\n");
    //fflush(stdout);
    
    SleepEx(INFINITE, true);

    //printf("Stopped sleeping again\n");
    //fflush(stdout);
    
    CAMLreturn0;
}

CAMLprim value
caml_wait_for_changes( value path_list, value closure){

    CAMLparam2(path_list, closure);    
    CAMLlocal2(p, head);

    int count = 0;
    p = path_list;    

    while (p != Val_emptylist){
        head = Field(p, 0);  /* accessing the head */
        count++;
        p = Field(p, 1);  /* point to the tail for next loop */
    }

    if (count == 0){
        caml_failwith("No directory paths entered");            
    }

    const char *paths[count];

    for (int i = 0; i < count; i++){
        head = Field(path_list, 0);  /* accessing the head */
        paths[i] = String_val(head);
        path_list = Field(path_list, 1);  /* point to the tail for next loop */
    }

    //const char *path = paths[0];

    //printf("watching %s for changes...\n", path);

    HANDLE hDir;

    //combine with above for loop
    //for (int i = 0; i < count; i++){ 
    hDir = CreateFile(paths[0],
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);

    //Raises exception if directory cannot be opened
    if (hDir == INVALID_HANDLE_VALUE){
        //raise caml exception
        caml_failwith("Directory cannot be found.");

    }

    uint8_t change_buf[1024];
    OVERLAPPED overlapped;
    struct myData data;
    data.buffer = change_buf;
    data.closure = closure;
    data.hDir = hDir;
    overlapped.hEvent = &data;

    BOOL success = ReadDirectoryChangesW(
            hDir, change_buf, 1024, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME  |
            FILE_NOTIFY_CHANGE_DIR_NAME   |
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL, &overlapped, &LpoverlappedCompletionRoutine);

    //Does not raise exception if directory changes cannot be read
    if (success == false) {
        CloseHandle(hDir);
        //CloseHandle(overlapped.hEvent);

        win32_maperr(GetLastError());
        uerror("ReadDirectoryChangesW", Nothing);
    }

    SleepEx(INFINITE, true);

    CAMLreturn(Val_unit);

}

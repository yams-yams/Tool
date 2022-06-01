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

//Data structure of info for completion routine
struct myData {
    char *buffer;
    value closure;
    const char *path;
    HANDLE hDir;
};

//Completion routine function
void ChangeNotification(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped){
    CAMLparam0();
    CAMLlocal2(filename, action);

    struct myData *data;
    data = (struct myData*)lpOverlapped->hEvent;
    FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*)data->buffer;
    
    //Iterates over event notifications 
    for (;;) {

        DWORD name_len = event->FileNameLength / sizeof(wchar_t);

        //Assigns OCaml variable action
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

        //Assigns OCaml variable filename 
        wchar_t *name = malloc(2 * (name_len + 1));
        memcpy(name, event->FileName, 2 * name_len);
        name[name_len] = 0;
        filename = caml_copy_string_of_os(name);
        free(name);

        //OCaml callback function
        caml_callback2(data->closure, action, filename);

        //Traverse to next event entry
        if (event->NextEntryOffset) {
            *((uint8_t**)&event) += event->NextEntryOffset;
        } else {
            break;
        }
    }
    
    //Re-calls ReadDirectoryChangesW 
    BOOL success = ReadDirectoryChangesW(
        data->hDir, data->buffer, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, lpOverlapped, &ChangeNotification);

    //Raises exception if ReadDirectoryChanges fails
    if (success == false) {
        CloseHandle(data->hDir);
        win32_maperr(GetLastError());
        uerror("ReadDirectoryChangesW", Nothing);
    }
    
    CAMLreturn0;
}

CAMLprim value
caml_wait_for_changes( value path_list, value closure){

    CAMLparam2(path_list, closure);    
    CAMLlocal2(p, head);

    int count = 0;
    p = path_list;    

    //Gets number of paths using pointer p
    while (p != Val_emptylist){
        head = Field(p, 0);  /* accessing the head */
        count++;
        p = Field(p, 1);  /* point to the tail for next loop */
    }

    //Raises exception if no paths are passed
    if (count == 0){
        caml_failwith("No directory paths entered");            
    }

    const char *path;
    HANDLE hDir[count];
    int str_length;
    OVERLAPPED overlapped[count];
    struct myData data[count];
    
    //Calls ReadDirectoryChanges for each path
    for (int i = 0; i < count; i++){
        head = Field(path_list, 0);  /* accessing the head */
        
        //Allocates memory for path
        str_length = strlen(String_val(head));
        path = malloc(str_length + 1);
        path = String_val(head);

        //Creates handle to path
        hDir[i] = CreateFile(path,
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);

        //Raises exception if directory cannot be opened
        if (hDir[i] == INVALID_HANDLE_VALUE){
            //raise caml exception
            caml_failwith("Directory cannot be found.");

        }
        
        uint8_t change_buf[1024];
        data[i].buffer = change_buf;
        data[i].closure = closure;
        data[i].path = path;
        data[i].hDir = hDir[i];
        
        overlapped[i].hEvent = &data[i];
        
        BOOL success = ReadDirectoryChangesW(
            hDir[i], change_buf, 1024, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME  |
            FILE_NOTIFY_CHANGE_DIR_NAME   |
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            NULL, &overlapped[i], &ChangeNotification);

        //Raises exception if ReadDirectoryChanges function fails
        if (success == false) {
            CloseHandle(hDir[i]);
            win32_maperr(GetLastError());
            uerror("ReadDirectoryChangesW", Nothing);
        }
        
        printf("Watching %s\n", path);
        fflush(stdout);

        path_list = Field(path_list, 1);  /* point to the tail for next loop */
    }

    //Program sleeps except for when completion routine is called
    while (true){
        SleepEx(INFINITE, true);
    }

    CAMLreturn(Val_unit);

}

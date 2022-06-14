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
#include <caml/threads.h>
#include <caml/unixsupport.h>
#include <windows.h>
#include <assert.h>
#include <intsafe.h>

//global values
bool term_flag = false;
struct path_node *headNode;
value closure;

//structs for data storage
struct myData {
    char *buffer;
    const char *path;
    HANDLE hDir;
};

struct path_node {
    
    struct myData data;
    OVERLAPPED overlapped;
    struct path_node *next;
};

void terminate(ULONG_PTR arg){
    caml_acquire_runtime_system();    
    //sets flag that ends file-watching
    term_flag = true;
    
    caml_release_runtime_system();
}

CAMLprim value
caml_get_handle(){
    
    CAMLparam0();
    //get windows handle
    HANDLE p_handle = GetCurrentThread();
    
    LPHANDLE handle = &p_handle;

    //malloc for handle
    
    //duplicate p_handle
    int success = DuplicateHandle(
        GetCurrentProcess(),    //Current process handle
        p_handle,               //Handle to duplicate
        GetCurrentProcess(),    //Process that receives handle
        handle,                 //Target handle pointer
        THREAD_ALL_ACCESS,      //Thread access
        TRUE,                   //Thread is inheritable
        0);
    
    //check if handle copy is successful
    if (success == 0){
        win32_maperr(GetLastError());
        uerror("Thread handle could not be duplicated.", Nothing);
    }
   
    //return as ocaml int
    CAMLreturn(caml_copy_nativeint((intnat)(*handle)));

}

CAMLprim value
caml_exit_routine(value hThread){
    
    CAMLparam1(hThread);    
    //Cast native int to handle
    HANDLE handle = (HANDLE)(Nativeint_val(hThread));
    
    //Call to terminate the thread
    DWORD success = QueueUserAPC(&terminate, handle, 0);
    
    //Check success
    if (success == 0) {
        LPTSTR lpMsgBuf;
        DWORD dw = GetLastError();

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0, NULL );
        CloseHandle(handle);
        printf("%s\n", lpMsgBuf);

        win32_maperr(GetLastError());
        uerror("QueueUserAPC failed.", Nothing);
    }

    //Return main thread to OCaml
    CAMLreturn(Val_unit);
}


//Completion routine function
void ChangeNotification(DWORD dwErrorCode, DWORD dwBytes, LPOVERLAPPED lpOverlapped){
    caml_acquire_runtime_system();
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
        
        printf("At path %s\n", data->path);
        fflush(stdout);

        //OCaml callback function
        caml_callback2(closure, action, filename);
        

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
        uerror("ReadDirectoryChangesW failed", Nothing);
    }

    caml_release_runtime_system();

    CAMLreturn0;
}

void watch_path( ULONG_PTR lPath ) {

    caml_acquire_runtime_system();

    struct path_node *currentNode;
    
    currentNode = (struct path_node*) malloc(sizeof(struct path_node));
     
    uint8_t *change_buf = (uint8_t*) malloc(1024 * sizeof(uint8_t));
    currentNode->data.buffer = change_buf;
    currentNode->data.path = (char*) lPath;
    
    //Creates handle to path
    currentNode->data.hDir = CreateFile(currentNode->data.path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);

    //Raises exception if directory cannot be opened
    if (currentNode->data.hDir == INVALID_HANDLE_VALUE){
        //raise caml exception
        caml_failwith("Directory cannot be found.");

    }
       currentNode->overlapped.hEvent = &(currentNode->data);
    
    BOOL success = ReadDirectoryChangesW(
        currentNode->data.hDir, currentNode->data.buffer, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, &(currentNode->overlapped), &ChangeNotification);

    //Raises exception if ReadDirectoryChanges function fails
    if (success == false) {
        CloseHandle(currentNode->data.hDir);
        win32_maperr(GetLastError());
        uerror("ReadDirectoryChangesW failed", Nothing);
    }
    
    printf("Watching %s\n", currentNode->data.path);
    fflush(stdout);
    
    currentNode->next = headNode;
    headNode = currentNode;

    caml_release_runtime_system();
}

CAMLprim value
caml_add_path( value hThread, value ocaml_path ) {
    
    CAMLparam2(ocaml_path, hThread);
    
    //Allocates memory for path
    int str_length = strlen(String_val(ocaml_path));
    char *path = malloc(str_length + 1);
    memcpy(path, String_val(ocaml_path), str_length + 1);
   
    //Cast native int to handle
    HANDLE handle = (HANDLE)(Nativeint_val(hThread));
    
    ULONG_PTR ptr = (ULONG_PTR)path;

    //Call to terminate the thread
    DWORD success = QueueUserAPC(&watch_path, handle, ptr);
    
    //Check success
    if (success == 0) {
        LPTSTR lpMsgBuf;
        DWORD dw = GetLastError();

        FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0, NULL );
        CloseHandle(handle);
        printf("%s\n", lpMsgBuf);

        win32_maperr(GetLastError());
        uerror("QueueUserAPC failed.", Nothing);
    }
    
    //Return main thread to OCaml
    CAMLreturn(Val_unit);

}


CAMLprim value
caml_wait_for_changes( value closure_f ) {

    CAMLparam1(closure_f);    
    
    closure = closure_f;
   
    caml_release_runtime_system();

    //Program sleeps except for when completion routine is called
    while (!term_flag){
        SleepEx(INFINITE, true);
    }

    caml_acquire_runtime_system();

    struct path_node* tmp;

    while (headNode != NULL) {
       tmp = headNode;
       headNode = headNode->next;
       free(tmp);
    }
    
    CAMLreturn(Val_unit);
}

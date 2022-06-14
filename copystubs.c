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


CAMLprim value
caml_open_port( value closure_f ) {

    CAMLparam1(closure_f);    
    
    closure = closure_f;
    
    //Creates a completion port without associating it to a handle
    HANDLE hPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,   //Handle to watch
        NULL,                   //Existing Port
        0,                      //Completion Key
        1);                     //Num of threads used for IOC

    CAMLreturn(caml_copy_nativeint((intnat)(hPort)));
}

//Called from main thread, 
CAMLprim value
caml_add_path( value hPort, value ocaml_path ) {
    
    CAMLparam2(ocaml_path, hPort);
    
    //Allocates memory for path
    int str_length = strlen(String_val(ocaml_path));
    char *path = malloc(str_length + 1);
    memcpy(path, String_val(ocaml_path), str_length + 1);
    
    printf("The path is: %s\n", path);
    fflush(stdout);

    //Cast native int to handle
    HANDLE portHandle = (HANDLE)(Nativeint_val(hPort));
    //Casts path to ULONG ptr for completion port
    ULONG_PTR ptr = (ULONG_PTR)path;
    
    struct path_node *currentNode;
    currentNode = (struct path_node*) malloc(sizeof(struct path_node));
     
    //Creates handle to path
    currentNode->data.hDir = CreateFile(path,
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
    
    printf("Handle to file created, ");
    fflush(stdout);


    //Adds handle to Completion Port
    portHandle = CreateIoCompletionPort(
        currentNode->data.hDir,
        portHandle,
        path,
        1);

    if (portHandle == NULL) {
        caml_failwith("File could not be added to completion port.");
    }
    else { printf("file added to port\n"); fflush(stdout); }
    

    uint8_t *change_buf = (uint8_t*) malloc(1024 * sizeof(uint8_t));
    currentNode->data.buffer = change_buf;
    currentNode->data.path = path;
    currentNode->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
 
    
    //Read directory changes without completion routine
    BOOL success = ReadDirectoryChangesW(
        currentNode->data.hDir, currentNode->data.buffer, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, &(currentNode->overlapped), NULL);
    
    printf("RDCW done\n");
    fflush(stdout);
    
    //Raises exception if ReadDirectoryChanges function fails
    if (success == false) {
        
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
        
        CloseHandle(currentNode->data.hDir);
        
        printf("%s\n", lpMsgBuf);
        fflush(stdout);

        win32_maperr(GetLastError());
        uerror("ReadDirectoryChangesW failed", Nothing);
    }

    printf("hello\n");
    fflush(stdout);
    printf("Watching %s\n", currentNode->data.path);
    fflush(stdout);
    
    currentNode->next = headNode;
    headNode = currentNode;

    CAMLreturn(Val_unit);
    //CAMLreturn(caml_copy_nativeint((intnat)(portHandle)));
}


CAMLprim value
caml_block_for_changes( value port_handle, value closure ) {
    caml_acquire_runtime_system();
    CAMLparam2(port_handle, closure);
    CAMLlocal2(filename, action);
    printf("Entered block for changes\n");
    fflush(stdout);    
    
    HANDLE portHandle = (HANDLE)(Nativeint_val(port_handle));
    DWORD num_bytes = 0;
    ULONG_PTR completion_key = 0;
    OVERLAPPED *overlapped = NULL;
    
    while (true) {
        caml_release_runtime_system();
        bool success = GetQueuedCompletionStatus(
            portHandle,         //handle to completion port
            &num_bytes,         //bytes transferred
            &completion_key,    //completion key (path)
            &overlapped,        //overlapped data structure
            INFINITE);          //seconds to time out
        caml_acquire_runtime_system();
       
        //Following two lines create a seg fault
        //char* path;
        //HRESULT conversion = ULongPtrToChar(completion_key, path);
        
        printf("notification packet received\n");
        //printf("completion_key is: %d\n", path);
        fflush(stdout);

        if (success == false) {
            caml_failwith("GetQCompletionStatus failed.");
            fflush(stdout);
        }

        /*struct myData *data;
        data = (struct myData*)(overlapped->hEvent);
        FILE_NOTIFY_INFORMATION *event = (FILE_NOTIFY_INFORMATION*)data->buffer;
        */
        
        /*
        //Iterates over event notifications 
        for (;;) {
            
            DWORD name_len = event->FileNameLength / sizeof(wchar_t);
            printf("%d\n", name_len);
            fflush(stdout);

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
        }*/

    }
    struct path_node* tmp;

    while (headNode != NULL) {
       tmp = headNode;
       headNode = headNode->next;
       free(tmp);
    }

    printf("going to exit\n");
    fflush(stdout);

    CAMLreturn(Val_unit);
}

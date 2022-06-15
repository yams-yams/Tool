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

//global state to pass around
struct globalState {
    
    //pointer to list of allocated values
    //use for deallocating list
    struct pathNode *headNode;
    
    HANDLE completionPort;

    //callback function for posting processed notifications
    value closure;
};

//data necessary for file notifications
struct notifData {
    char *buffer;
    const char *path;
    HANDLE hDir;
    OVERLAPPED overlapped;
};


//node structure for allocated values
struct pathNode {    
    struct notifData data;
    struct pathNode *next;
    OVERLAPPED overlapped;
};

/*//structure for all requests' data
union requestData {
    bool stopRequest;       
    struct notifData fileChange;
    const char* path;
};*/

enum requestType {FileChange, Stop, AddPath};

//request data structure
struct request {
    enum requestType type;
    union {
        bool stopRequest;       
        struct notifData fileChange;
        const char* path;
    };
};


CAMLprim value
caml_open_port( value closure_f ) {

    CAMLparam1(closure_f);    
    
    struct globalState *state = (struct globalState*) malloc(sizeof(struct globalState));
    state->closure = closure_f;
    state->headNode = (struct pathNode*) malloc(sizeof(struct pathNode));
    state->headNode->data.hDir = INVALID_HANDLE_VALUE;
    state->headNode->next = NULL;

    //Creates a completion port without associating it to a handle
    state->completionPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,   //Handle to watch
        NULL,                   //Existing Port
        0,                      //Completion Key
        1);                     //Num of threads used for IOC

    CAMLreturn(caml_copy_nativeint((intnat)(state)));
}

//Called from main thread
CAMLprim value
caml_add_path( value statePtr, value ocaml_path ) {
    
    CAMLparam2(statePtr, ocaml_path);
    
    //Allocates memory for path
    int str_length = strlen(String_val(ocaml_path));
    char *path = malloc(str_length + 1);
    memcpy(path, String_val(ocaml_path), str_length + 1);
    
    printf("The path is: %s\n", path);
    fflush(stdout);
    
    //casts pointer to state
    struct globalState *state = (struct globalState*)(Nativeint_val(statePtr));
    
    struct pathNode *currentNode;
    currentNode = (struct pathNode*) malloc(sizeof(struct pathNode));
     
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
    
    uint8_t *change_buf = (uint8_t*) malloc(1024 * sizeof(uint8_t));
    currentNode->data.buffer = change_buf;
    currentNode->data.path = path;
    currentNode->overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    struct request *myReq = (struct request*)(malloc(sizeof(struct request)));
    
    myReq->type = 0;
    myReq->fileChange = currentNode->data;
    //Adds handle to Completion Port
    state->completionPort = CreateIoCompletionPort(
        currentNode->data.hDir,
        state->completionPort,
        myReq,           //pass request object
        1);             //ignored because port already exists

    if (state->completionPort == NULL) {
        caml_failwith("File could not be added to completion port.");
    }
    else { printf("file added to port\n"); fflush(stdout); }
       
    //Read directory changes without completion routine
    BOOL success = ReadDirectoryChangesW(
        currentNode->data.hDir, currentNode->data.buffer, 1024, TRUE,
        FILE_NOTIFY_CHANGE_FILE_NAME  |
        FILE_NOTIFY_CHANGE_DIR_NAME   |
        FILE_NOTIFY_CHANGE_LAST_WRITE,
        NULL, &(currentNode->overlapped), NULL);
    
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

    printf("Watching %s\n", currentNode->data.path);
    fflush(stdout);
    
    currentNode->next = state->headNode;
    state->headNode = currentNode;

    CAMLreturn(Val_unit);
}


CAMLprim value
caml_block_for_changes( value statePtr, value closure ) {

    CAMLparam2(statePtr, closure);
    CAMLlocal2(filename, action);
    
    struct globalState* state = (struct globalState*)(Nativeint_val(statePtr));
    DWORD num_bytes = 0;
    ULONG_PTR completion_key = 0;
    OVERLAPPED *overlapped = NULL;
    
    bool stop = false;

    while (!stop) {
        
        caml_release_runtime_system();
        bool success = GetQueuedCompletionStatus(
            state->completionPort,         //handle to completion port
            &num_bytes,         //bytes transferred
            &completion_key,    //completion key
            &overlapped,        //overlapped data structure
            INFINITE);          //seconds to time out
        caml_acquire_runtime_system();
        
        if (success == false) {
            caml_failwith("GetQCompletionStatus failed.");
            fflush(stdout);
        }

        char* path;
        struct request* notif = (struct request*)completion_key;
        
        if (notif->type == 1) {
            printf("stop signal received\n");
            fflush(stdout);
            stop = true;
        }
    
        else if (notif->type == 0) {
            struct notifData *data = &(notif->fileChange);
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

            //reset overlapped object
            //recall RDCW
            notif->fileChange.overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            
            //Read directory changes without completion routine
            BOOL success = ReadDirectoryChangesW(
                (notif->fileChange).hDir, (notif->fileChange).buffer, 1024, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME  |
                FILE_NOTIFY_CHANGE_DIR_NAME   |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL, &((notif->fileChange).overlapped), NULL);
            
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
                
                CloseHandle((notif->fileChange).hDir);
                
                printf("%s\n", lpMsgBuf);
                fflush(stdout);

                win32_maperr(GetLastError());
                uerror("ReadDirectoryChangesW failed", Nothing);
            }
        }
        
    }
    printf("exited loop\n");
    fflush(stdout);

    struct pathNode* tmp;

    while (state->headNode != NULL) {
       tmp = state->headNode;
       if (tmp->data.hDir != INVALID_HANDLE_VALUE) {
           bool success = CancelIo(tmp->data.hDir);
           if (success == 0) {
                caml_failwith("CancelIO failed.");
                fflush(stdout);
           }
       }
       state->headNode = (state->headNode)->next;
       free(tmp);
       printf("deleted a node\n");
       fflush(stdout);
    }
    printf("almost done freeing nodes\n");
    fflush(stdout);
    tmp = state->headNode;
    free(tmp);

    printf("done freeing nodes\n");
    fflush(stdout);
    free(state);
    printf("going to exit\n");
    fflush(stdout);

    CAMLreturn(Val_unit);
}

CAMLprim value
caml_stop_watching( value statePtr ){
    
    printf("caml_stop_watching called\n");
    fflush(stdout);
    CAMLparam1(statePtr);
    struct globalState* state = (struct globalState*)(Nativeint_val(statePtr));
    
    struct request* stopReq = (struct request*)(malloc(sizeof(struct request)));
    stopReq->type = 1;
    stopReq->stopRequest = true;
    
    DWORD num_bytes = sizeof(struct request) + sizeof(OVERLAPPED);
    ULONG_PTR completion_key = (ULONG_PTR)(&stopReq);
    OVERLAPPED *overlapped = NULL;

    bool success = PostQueuedCompletionStatus(
        state->completionPort,
        num_bytes,
        completion_key,
        overlapped);
    
    if (success == 0) {
            caml_failwith("PostQueuedCompletionStatus failed.");
            fflush(stdout);
    }
    printf("PostQueuedCompletionStatus function called\n");
    fflush(stdout);
    CAMLreturn(Val_unit);
}


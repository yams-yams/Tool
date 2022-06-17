#include <stdio.h>
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

#define BUFF_SIZE 1024

struct global_state 
{    
    struct path_node *head;
    HANDLE completion_port;
    value closure;
};

struct path_node 
{    
    char *buffer;
    const char *path;
    HANDLE handle;
    OVERLAPPED overlapped;
    struct path_node *next;
};

enum request_type 
{
    FileChange, 
    Stop, 
    AddPath
};

struct request 
{
    enum request_type type;
    union 
    {
        struct path_node* file_change;
        const char* path;
    };
};

static DWORD print_error(HANDLE handle)
{
    LPTSTR lpMsgBuf;
    DWORD dw;

    dw = GetLastError();

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
    fflush(stdout);
    
    return dw;
}


value winwatch_open_port(value v_unit) 
{
    CAMLparam1(v_unit);    
    struct global_state *state = NULL;

    state = malloc(sizeof(struct global_state));
    state->head = malloc(sizeof(struct path_node));
    state->head->handle = INVALID_HANDLE_VALUE;
    state->head->next = NULL;

    /*Creates a completion port without associating it to a handle*/
    state->completion_port = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,   
        NULL,                  
        0,                    
        1);                  

    /*Return integer value of pointer to state*/
    CAMLreturn(caml_copy_nativeint((intnat)(state)));
}

value winwatch_add_path(value v_state, value v_path) 
{
    CAMLparam2(v_state, v_path);
    int str_length;
    char *path = NULL;
    struct global_state *state = NULL;
    struct request *add_request = NULL;
    DWORD num_bytes;
    ULONG_PTR completion_key;
    OVERLAPPED *overlapped = NULL;

    str_length = strlen(String_val(v_path));
    path = malloc(str_length + 1);
    memcpy(path, String_val(v_path), str_length + 1);
    
    
    state = (struct global_state*)(Nativeint_val(v_state)); 
    add_request = malloc(sizeof(struct request));
    
    add_request->type = AddPath;
    add_request->path = path;
    
    completion_key = (ULONG_PTR)add_request;

    BOOL success = PostQueuedCompletionStatus(
        state->completion_port,
        num_bytes,
        completion_key,
        overlapped);
    
    if (success == 0) 
    {
        caml_failwith("PostQueuedCompletionStatus failed.");
    }

    CAMLreturn(Val_unit);
}

value winwatch_block_for_changes(value v_state, value v_closure) 
{
    CAMLparam2(v_state, v_closure);
    CAMLlocal2(filename, action);
    
    struct global_state* state = NULL;
    DWORD num_bytes;
    OVERLAPPED *overlapped = NULL;
    ULONG_PTR completion_key;
    BOOL stop;
    char* path;
    struct request* notif = NULL;
    struct path_node* tmp = NULL;

    state = (struct global_state*)(Nativeint_val(v_state));
    state->closure = v_closure;

    num_bytes = 0;
    completion_key = 0;
    stop = FALSE;

    while (!stop) 
    {
        caml_release_runtime_system();
        
        /*Blocks until completion port receives a packet*/
        BOOL success = GetQueuedCompletionStatus(
            state->completion_port,         
            &num_bytes,         
            &completion_key,    
            &overlapped,        
            INFINITE);

        caml_acquire_runtime_system();

        if (success == FALSE)
        {
            caml_failwith("GetQCompletionStatus failed.");
        }

        notif = (struct request*)completion_key;

        switch (notif->type)
        {
            case Stop:
            {
                stop = TRUE;
                break;
            }

            case FileChange:
            {
                struct path_node *data = NULL;
                FILE_NOTIFY_INFORMATION *event = NULL;
                
                data = notif->file_change;
                event = (FILE_NOTIFY_INFORMATION*)data->buffer;
                
                /*Iterates over file change notifications*/
                for (;;) 
                {
                    DWORD name_len = event->FileNameLength / sizeof(wchar_t);

                    switch (event->Action)
                    {
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
                    
                    printf("At path %s\n", data->path);
                    fflush(stdout);

                    /*Calls OCaml function that prints notification */
                    caml_callback2(state->closure, action, filename);
                    
                    if (event->NextEntryOffset) 
                    {
                        *((char**)&event) += event->NextEntryOffset;
                    } 
                    else 
                    {
                        break;
                    }
                
                }
                
                memset(&(notif->file_change->overlapped), 0, sizeof(OVERLAPPED));
                
                BOOL success = ReadDirectoryChangesW(
                    notif->file_change->handle, notif->file_change->buffer, BUFF_SIZE, TRUE,
                    FILE_NOTIFY_CHANGE_FILE_NAME  |
                    FILE_NOTIFY_CHANGE_DIR_NAME   |
                    FILE_NOTIFY_CHANGE_LAST_WRITE,
                    NULL, &(notif->file_change->overlapped), NULL);
                
                if (success == FALSE) 
                {
                    DWORD dw = print_error(notif->file_change->handle);
                    win32_maperr(dw);
                    uerror("ReadDirectoryChangesW failed", Nothing);
                }
            } break;
            
            case AddPath:
            {
                struct path_node *new_node = NULL;
                char *change_buf = NULL;
                struct request* changeRequest = NULL;
                ULONG_PTR completion_key;

                new_node = malloc(sizeof(struct path_node));
                change_buf = malloc(BUFF_SIZE * sizeof(char));
                new_node->buffer = change_buf;
                new_node->path = notif->path;

                new_node->handle = CreateFile(new_node->path,
                    FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                    NULL);

                if (new_node->handle == INVALID_HANDLE_VALUE)
                {
                    caml_failwith("Directory cannot be found.");
                }
                
                memset(&(new_node->overlapped), 0, sizeof(OVERLAPPED));
                
                changeRequest = malloc(sizeof(struct request));
                changeRequest->type = FileChange;
                changeRequest->file_change = new_node;
                
                completion_key = (ULONG_PTR) changeRequest; 

                state->completion_port = CreateIoCompletionPort(
                    new_node->handle,
                    state->completion_port,
                    completion_key,         
                    1);                     

                if (state->completion_port == NULL) 
                {
                    caml_failwith("File could not be added to completion port.");
                }
                   
                BOOL success = ReadDirectoryChangesW(
                    new_node->handle, new_node->buffer, BUFF_SIZE, TRUE,
                    FILE_NOTIFY_CHANGE_FILE_NAME  |
                    FILE_NOTIFY_CHANGE_DIR_NAME   |
                    FILE_NOTIFY_CHANGE_LAST_WRITE,
                    NULL, &(new_node->overlapped), NULL);
                
                if (success == FALSE) 
                {
                    DWORD dw = print_error(new_node->handle);
                    win32_maperr(dw);
                    
                    uerror("ReadDirectoryChangesW failed", Nothing);
                }

                printf("Watching %s\n", new_node->path);
                fflush(stdout);

                new_node->next = state->head;
                state->head = new_node;

            } break;
        
        free(notif);

        }
    }
    
    while (state->head != NULL) 
    {
       tmp = state->head;
       if (tmp->handle != INVALID_HANDLE_VALUE) 
       {
           BOOL success = CancelIo(tmp->handle);
           if (success == 0) 
           {
                caml_failwith("CancelIO failed.");
           }
       }
       state->head = (state->head)->next;
       free(tmp);
    }
    tmp = state->head;
    free(tmp);
    free(state);
    
    CAMLreturn(Val_unit);
}

value winwatch_stop_watching(value v_state)
{
    CAMLparam1(v_state);
    struct global_state* state = NULL;
    struct request* stopReq = NULL;
    DWORD num_bytes;
    ULONG_PTR completion_key;
    OVERLAPPED *overlapped = NULL;

    state = (struct global_state*)(Nativeint_val(v_state));
    stopReq = malloc(sizeof(struct request));
    stopReq->type = Stop;
    
    num_bytes = 0;
    completion_key = (ULONG_PTR)stopReq;

    BOOL success = PostQueuedCompletionStatus(
        state->completion_port,
        num_bytes,
        completion_key,
        overlapped);
    
    if (success == 0) 
    {
        caml_failwith("PostQueuedCompletionStatus failed.");
    }
    
    CAMLreturn(Val_unit);
}

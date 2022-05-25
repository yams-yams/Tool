#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
//#include <caml/mlvalues.h>
//#include <caml/alloc.h>
//#include <caml/memory.h>
#include <windows.h>
#include <assert.h>

/*CAMLprim value
  get_an_int( value v ){
  int i;
  i = Int_val(v);
  printf("%d\n", i);
  return Val_unit;
  }

  CAMLprim value
  inspect_tuple( value ml_tuple ){
  CAMLparam1( ml_tuple );
  CAMLlocal3( vi, vf, vs );

  vi = Field(ml_tuple, 0);
  vf = Field(ml_tuple, 1);
  vs = Field(ml_tuple, 2);

  printf("%d\n", Int_val(vi));
  printf("%f\n", Double_val(vf));
  printf("%s\n", String_val(vs));

  CAMLreturn( Val_unit );


  }
  */
int main() {

    char path[] = "D:\\Cygwin\\home\\uma\\lexifi\\testdir\\";
    //get handle of directory
    /*HANDLE hDir = CreateFileA(
        path,                       //file path
        GENERIC_READ,               //Access mode
        FILE_SHARE_WRITE,           //Share mode
        NULL,                       //Security 
        OPEN_EXISTING,              //Creation Disposition
        FILE_FLAG_BACKUP_SEMANTICS, //Flags
        NULL);*/                    //Template file
    printf("watching %s for changes...\n", path);

    HANDLE hDir = CreateFile(path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL);

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

                switch (event->Action) {
                    case FILE_ACTION_ADDED: {
                        wprintf(L"       Added: %.*S\n", name_len, event->FileName);
                    } break;

                    case FILE_ACTION_REMOVED: {
                        wprintf(L"     Removed: %.*S\n", name_len, event->FileName);
                    } break;

                    case FILE_ACTION_MODIFIED: {
                        wprintf(L"    Modified: %.*S\n", name_len, event->FileName);
                    } break;

                    case FILE_ACTION_RENAMED_OLD_NAME: {
                        wprintf(L"Renamed from: %.*S\n", name_len, event->FileName);
                    } break;

                    case FILE_ACTION_RENAMED_NEW_NAME: {
                        wprintf(L"          to: %.*S\n", name_len, event->FileName);
                    } break;

                    default: {
                        printf("Unknown action!\n");
                    } break;
                }

                // Are there more events to handle?
                if (event->NextEntryOffset) {
                        *((uint8_t**)&event) += event->NextEntryOffset;
                } else {
                        break;
                }
            }

            // Queue the next event
            BOOL success = ReadDirectoryChangesW(
                hDir, change_buf, 1024, TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME  |
                FILE_NOTIFY_CHANGE_DIR_NAME   |
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                NULL, &overlapped, NULL);

        }

            // Do other loop stuff here...
    }
}

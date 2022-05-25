type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW

external wait_for_changes: string (* directory name *) -> (action -> string -> unit) -> unit = "caml_wait_for_changes"

let handle action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n" filename
    | REMOVE -> Printf.printf "Removed: %s\n" filename
    | REMOVE -> Printf.printf "Removed: %s\n" filename
    | MODIFY -> Printf.printf "Modified: %s\n" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n" filename
    | _ -> Printf.printf "Unknown action!\n" in

let () =
    Callback.register "Handle callback" handle in 
    wait_for_changes "D:\\Cygwin\\home\\uma\\lexifi\\testdir\\" handle

type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW

external wait_for_changes: string list (* directory names *) -> (action -> string -> unit) -> unit = "caml_wait_for_changes"

let handle action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename

let () =
    match (Array.to_list Sys.argv) with
    | [] -> Printf.printf "No directories given"
    | h::t ->
        wait_for_changes t handle

type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW

external wait_for_changes: string list (* directory names *) -> (action -> string -> unit) -> unit = "caml_wait_for_changes"

external exit_routine: Thread.t -> unit = "caml_exit_routine"

let handle action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename

let func l =
    match l with
    | [] -> Printf.printf "No directories given"
    | h::t ->
        wait_for_changes t handle

let () =
    let handle = Thread.create func (Array.to_list Sys.argv) in
    Printf.printf "Type anything to end directory watching\n%!";
    match (input_line stdin) with
    | _ -> Printf.printf "Exiting\n%!" ; exit_routine handle;


type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external wait_for_changes: (action -> string -> unit) -> unit = "caml_wait_for_changes"

external exit_routine: t -> unit = "caml_exit_routine"

external get_handle: unit -> t = "caml_get_handle" 

external add_path: t -> string -> unit = "caml_add_path"


let handle_notif action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename

let handle = ref None

let func () =
    handle := Some(get_handle ());
    wait_for_changes handle_notif

let initial_paths handle = 
    match Array.to_list Sys.argv with
        | [] -> Printf.printf "No directories given\n%!"
        | h::t -> List.iter (add_path handle) t

let rec get_second_handle () =
    match !handle with 
    | Some handle -> handle
    | _ -> get_second_handle ()


let () =
    let ocaml_handle = Thread.create func () in
    initial_paths (get_second_handle ());
    Printf.printf "Type another path to watch or 'exit' to end directory watching\n%!";
    let flag = ref true in
    while !flag do
        match input_line stdin with
        | "exit" ->
            (Printf.printf "Terminating file-watching\n%!";
            exit_routine (get_second_handle ());
            Thread.join ocaml_handle;
            Printf.printf "File-watching has ended, main thread will exit now\n%!";
            flag := false);
        | _ as path -> add_path (get_second_handle ()) path;
    done;

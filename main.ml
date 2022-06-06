type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external wait_for_changes: string list (* directory names *) -> (action -> string -> unit) -> unit = "caml_wait_for_changes"

external exit_routine: t -> unit = "caml_exit_routine"

external get_handle: unit -> t = "caml_get_handle" (* get windows handle, change last bit to 1 and send back as ocaml int *)

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
    match Array.to_list Sys.argv  with
    | [] -> Printf.printf "No directories given"
    | h::t ->
        wait_for_changes t handle_notif

let () =
    let ocaml_handle = Thread.create func () in
    Printf.printf "Type anything to end directory watching\n%!";
    match input_line stdin with
    | _ ->
        (Printf.printf "Exiting\n%!";
        match !handle with
        | Some handle -> exit_routine handle
        | _ -> Printf.printf "Handle not ready\n%!");
    Thread.join ocaml_handle;
    match input_line stdin with
    | _ -> Printf.printf "Second thread killed, main thread will exit now\n%!"

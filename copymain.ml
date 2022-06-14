type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external open_port: (action -> string -> unit) -> int = "caml_open_port"

external add_path: int -> string -> unit = "caml_add_path"

external block_for_changes: int -> (action -> string -> unit)  -> unit = "caml_block_for_changes" 

let handle_notif action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename


let port_handle = ref None
let rec get_port_handle () =
    match !port_handle with 
    | Some port_handle -> port_handle
    | _ -> get_port_handle ()

let func () =
    block_for_changes (get_port_handle ()) handle_notif


let initial_paths handle = 
    match Array.to_list Sys.argv with
        | [] -> Printf.printf "No directories given\n%!"
        | h::t -> List.iter (add_path handle) t

let () =
    port_handle := Some(open_port handle_notif);
    initial_paths (get_port_handle ());
    let _ = Thread.create func () in
    Printf.printf "Type another path to watch or 'exit' to end directory watching\n%!";
    let flag = ref true in
    while !flag do
        match input_line stdin with
        | "exit" ->
            (Printf.printf "File-watching has ended, main thread will exit now\n%!";
            flag := false);
        | _ as path -> add_path (get_port_handle ()) path;
    done;


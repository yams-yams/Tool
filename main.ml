type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external open_port: unit -> t = "winwatch_open_port"

external add_path: t -> string -> unit = "winwatch_add_path"

external block_for_changes: t -> (action -> string -> unit)  -> unit = "winwatch_block_for_changes" 

external stop_watching: t -> unit = "winwatch_stop_watching"

let handle_notif action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename

let block state =
  block_for_changes state handle_notif

let initial_paths handle = 
  match Array.to_list Sys.argv with
    | [] -> Printf.printf "No directories given\n%!"
    | h::t -> List.iter (add_path handle) t

let rec watch_input state handle =
  match input_line stdin with
    | "exit" ->
      stop_watching state ;
      Thread.join handle;
      Printf.printf "File-watching has ended, main thread will exit now\n%!"
    | _ as path -> 
      add_path state path;
      watch_input state handle

let () =
  let state = open_port () in
  initial_paths state;
  let handle = Thread.create block state in
  Printf.printf "Type another path to watch or 'exit' to end directory watching\n%!";
  watch_input state handle;

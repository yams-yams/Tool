type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external create: unit -> t = "winwatch_create"

external add_path: t -> string -> unit = "winwatch_add_path"

external start: t -> (action -> string -> unit)  -> unit = "winwatch_start" 

external stop_watching: t -> unit = "winwatch_stop_watching"

let handle_notif action filename = 
    match action with
    | ADD -> Printf.printf "Added: %s\n%!" filename
    | REMOVE -> Printf.printf "Removed: %s\n%!" filename
    | MODIFY -> Printf.printf "Modified: %s\n%!" filename
    | RENAMED_OLD -> Printf.printf "Renamed from: %s\n%!" filename
    | RENAMED_NEW -> Printf.printf "          to: %s\n%!" filename

let start_thread state =
  start state handle_notif

let initial_paths state paths = 
  match paths with
    | [] -> Printf.printf "No directories given\n%!"
    | h::t -> List.iter (add_path state) t

let rec watch_input state handle =
  match input_line stdin with
    | "exit" ->
      stop_watching state;
      Thread.join handle;
      Printf.printf "File-watching has ended\n%!"
    | _ as path -> 
      add_path state path;
      watch_input state handle

let file_watch paths =
  let state = create () in
  initial_paths state paths;
  let handle = Thread.create start_thread state in
  Printf.printf "Type another path to watch or 'exit' to end directory watching\n%!";
  watch_input state handle  

let () =
  file_watch (Array.to_list Sys.argv);
  file_watch []

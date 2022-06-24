type action = ADD | REMOVE | MODIFY | RENAMED_OLD | RENAMED_NEW
type t

external create: unit -> t = "winwatch_create"

external add_path: t -> string -> unit = "winwatch_add_path"

external start_watch: t -> (action -> string -> string list -> (action -> string -> unit) -> unit) -> string list -> (action -> string -> unit) -> unit = "winwatch_start" 

external stop_watching: t -> unit = "winwatch_stop_watching"

let rec should_exclude filename paths = 
  match paths with
  | [] -> false
  | h::t ->
    match String.starts_with ~prefix:h filename with
    | true -> true
    | false -> should_exclude filename t

let filter_notif action filename exclusions func = 
  if not (should_exclude filename exclusions) then
    func action filename

let start state exclusions handler =
  start_watch state filter_notif exclusions handler
  

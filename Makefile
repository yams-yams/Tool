tool.exe: main.ml stubs.c
	ocamlopt -I +threads -o $@ unix.cmxa threads.cmxa $^

stubs.exe: stubs.c
	x86_64-w64-mingw32-gcc -o $@ $^



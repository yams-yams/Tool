tool.exe: main.ml stubs.c
	ocamlopt -o $@ unix.cmxa $^

stubs.exe: stubs.c
	x86_64-w64-mingw32-gcc -o $@ $^



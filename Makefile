tool.exe: main.ml stubs.c
	ocamlopt -I +threads -o $@ unix.cmxa threads.cmxa $^

stubs.exe: stubs.c
	x86_64-w64-mingw32-gcc -o $@ $^

copystubs.exe: copymain.ml copystubs.c Makefile
	ocamlopt -c -verbose -I +threads copymain.ml copystubs.c
	ocamlopt -o $@ -verbose -I +threads unix.cmxa threads.cmxa copymain.cmx copystubs.o 

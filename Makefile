pmalloc:
	gcc -g -o pmalloc pmalloc.c -lm

all:
	(cd tests && make)

clean:
	(cd tests && make clean)
	rm -f valgrind.out stdout.txt stderr.txt *.plist



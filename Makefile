xderat: xderat.c
	gcc -Wall -W -o $@ $< -lX11 -lXinerama

clean:
	rm -f xderat

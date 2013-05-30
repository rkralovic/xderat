xderat: xderat.c
	gcc -Wall -W -o $@ $< -lX11 -lXinerama -lXtst

clean:
	rm -f xderat

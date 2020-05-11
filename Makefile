xderat: xderat.c
	gcc -Wall -W -o $@ $< -lX11 -lXext -lXinerama -lXtst

clean:
	rm -f xderat

all: zlib/libz.a
	gcc main.c png.c jpeg.c utils.c zlib/libz.a

zlib/libz.a:
	cd zlib/ && ./configure && make static


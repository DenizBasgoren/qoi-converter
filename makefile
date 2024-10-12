all:
	gcc encode.c -lpng -o encode
	gcc decode.c -lpng -o decode
	gcc comparePngImages.c -lpng -o comparePngImages
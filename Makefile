CC      = clang
CFLAGS  = -arch x86_64 -O2 -Wall
LDFLAGS = -dynamiclib -framework OpenGL -undefined dynamic_lookup

accheat.dylib: src/accheat.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f accheat.dylib

.PHONY: clean

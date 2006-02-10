CFLAGS+=-g -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls
LDFLAGS+=-lX11 -lXext -lXrender -lXdamage

PROGS=xshowdamage

xshowdamage: xshowdamage.o

all: $(PROGS)

clean:
	rm -f $(PROGS) *.o

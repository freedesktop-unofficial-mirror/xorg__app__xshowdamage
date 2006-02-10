CFLAGS+=-g -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls `pkg-config --cflags xrender xdamage xext`
LDFLAGS+=`pkg-config --libs xrender xdamage xext`

PROGS=xshowdamage

xshowdamage: xshowdamage.o

all: $(PROGS)

clean:
	rm -f $(PROGS) *.o

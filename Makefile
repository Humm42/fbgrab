.POSIX:

MYCPPFLAGS = -D _POSIX_C_SOURCE=200809L ${CPPFLAGS}

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man
LIBS = -l png -l z

all: fbgrab

fbgrab: fbgrab.c
	${CC} -o fbgrab ${CFLAGS} ${MYCPPFLAGS} ${LDFLAGS} fbgrab.c ${LIBS}

install: fbgrab
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f fbgrab ${DESTDIR}${PREFIX}/bin/fbgrab
	chmod 555 ${DESTDIR}${PREFIX}/bin/fbgrab
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f fbgrab.1 ${DESTDIR}${MANPREFIX}/man1/fbgrab.1
	chmod 444 ${DESTDIR}${MANPREFIX}/man1/fbgrab.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/fbgrab ${DESTDIR}${MANPREFIX}/man1/fbgrab.1

clean:
	rm -f fbgrab

.PHONY: all install uninstall clean

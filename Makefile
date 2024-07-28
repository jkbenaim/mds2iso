target  ?= mds2iso
objects := mds2iso.o hexdump.o mapfile.o
CC=c99

.PHONY: all
all:	$(target) README

.PHONY: clean
clean:
	rm -f $(target) $(objects)

.PHONY: install
install: ${target} ${target}.1
	install -m 755 ${target} /usr/local/bin
	install -m 755 -d /usr/local/share/man/man1
	install -m 644 ${target}.1 /usr/local/share/man/man1

.PHONY: uninstall
uninstall:
	rm -f /usr/local/bin/${target} /usr/local/share/man/man1/${target}.1

README: ${target}.1
	MANWIDTH=77 man --nh --nj ./${target}.1 | col -b > $@

$(target): $(objects)

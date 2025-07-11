all: lib init user

lib:
	$(MAKE) -f Makefile.lib

init: lib
	$(MAKE) -f Makefile.init

user: lib
	$(MAKE) -f Makefile.user

clean:
	$(MAKE) -f Makefile.lib clean
	$(MAKE) -f Makefile.init clean
	$(MAKE) -f Makefile.user clean
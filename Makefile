PYTHON ?= python3
VERSION ?= dev
DIST_DIR ?= dist

.PHONY: all check dist clean

all: wrapper/ddraw.dll

wrapper/ddraw.dll: wrapper/alpha_patch.c wrapper/alpha_patch.h wrapper/ddraw_proxy.c wrapper/ddraw.def wrapper/build.sh
	./wrapper/build.sh

check: wrapper/ddraw.dll
	$(PYTHON) -m unittest -v test_alpha_patch test_patcher

dist: check
	$(PYTHON) scripts/package_release.py --version "$(VERSION)" --output "$(DIST_DIR)"

clean:
	rm -f wrapper/alpha_patch.o wrapper/ddraw_proxy.o wrapper/ddraw.dll
	rm -rf "$(DIST_DIR)"

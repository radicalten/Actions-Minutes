.PHONY: all build qemu test host clean
all: build
build:
	bash scripts/build.sh
qemu: build
	python3 scripts/run_qemu.py
test: build
	python3 scripts/run_desmume.py
host:
	python3 scripts/test_host.py
clean:
	rm -rf work artifacts suite.nds

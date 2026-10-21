JOBS ?= 4
ENGINE_FLAGS ?=
ENGINE = vendor/fteqw/engine
QCC = $(abspath $(ENGINE)/qclib/fteqcc.bin)
.PHONY: all engine client server qcc game maps test
all: engine game maps
engine: server client
server:
	$(MAKE) -C $(ENGINE) -j$(JOBS) sv-rel $(ENGINE_FLAGS)
client:
	$(MAKE) -C $(ENGINE) -j$(JOBS) gl-rel $(ENGINE_FLAGS)
qcc:
	$(MAKE) -C $(ENGINE)/qclib -j$(JOBS)
game: qcc
	cd game/src && $(QCC) -srcfile progs.src
	cp game/src/qwprogs.dat game/qwprogs.dat
maps:
	python3 tools/make_maps.py game/maps
test: qcc server
	python3 tools/test.py

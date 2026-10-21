JOBS ?= 4
CMAKE ?= cmake
JOLT_CMAKE_FLAGS ?=
ENGINE_FLAGS ?=
ENGINE = vendor/fteqw/engine
QCC = $(abspath $(ENGINE)/qclib/fteqcc.bin)
.PHONY: all engine client server qcc game maps jolt test test-jolt
all: engine jolt game maps
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
jolt:
	$(CMAKE) -S physics -B build/jolt -DCMAKE_BUILD_TYPE=Release $(JOLT_CMAKE_FLAGS)
	$(CMAKE) --build build/jolt -j$(JOBS)
	$(CMAKE) --install build/jolt --prefix $(abspath $(ENGINE)/release)
maps:
	python3 tools/make_maps.py game/maps
	python3 tools/make_physics_models.py game/progs
test: qcc server
	python3 tools/test.py

test-jolt: qcc server jolt
	python3 tools/test_jolt.py

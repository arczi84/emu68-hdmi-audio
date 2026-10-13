TOOLCHAIN ?=
CC = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)m68k-amigaos-gcc
SFDC = $(if $(TOOLCHAIN),$(TOOLCHAIN)/)sfdc
CFLAGS = -std=gnu99 -O2 -m68020 -Wall -Wextra -Werror -Ibuild/include
COPY_DIR ?= build/install

# devicetree.resource and mailbox.resource are git submodules (../devicetree.resource, ../mailbox.resource): their SFD files
# generate the C stubs (proto/, inline/, clib/) into build/include, with the sfdc of the amiga-gcc toolchain.
# gic400.library (../gic400.library) ships its headers ready made. Initialise them with: git submodule update --init
# (not --recursive: mailbox.resource has a devicetree.resource of its own, not needed here).
GEN = build/include
define gen_stubs
$(shell mkdir -p $(GEN)/clib $(GEN)/proto $(GEN)/inline &&   $(SFDC) ../$(1).resource/sfd/$(1)_lib.sfd --mode=clib --output=$(GEN)/clib/$(1)_protos.h &&   $(SFDC) ../$(1).resource/sfd/$(1)_lib.sfd --mode=proto --output=$(GEN)/proto/$(1).h &&   $(SFDC) ../$(1).resource/sfd/$(1)_lib.sfd --mode=macros --output=$(GEN)/inline/$(1).h)
endef
$(if $(wildcard $(GEN)/proto/devicetree.h),,$(call gen_stubs,devicetree))
$(if $(wildcard $(GEN)/proto/mailbox.h),,$(call gen_stubs,mailbox))

.PHONY: all copy-exe test test-cli test-dma68k test-stream68k clean package
all: copy-exe copy-ahi

copy-exe: dist/HDMITest
	mkdir -p "$(COPY_DIR)"
	cp dist/HDMITest "$(COPY_DIR)/HDMITest"

dist/HDMITest: src/hdmi_backend.h src/main.c src/audio.h src/tone.h src/clock_reply.h src/dma_plan.h src/dma_control.h src/stream.h
	mkdir -p dist
	$(CC) $(CFLAGS) src/main.c -o $@

test:
	mkdir -p build
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/audio_test.c -o build/audio_test
	./build/audio_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/clock_test.c -o build/clock_test
	./build/clock_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/dma_test.c -o build/dma_test
	./build/dma_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/stream_test.c -o build/stream_test
	./build/stream_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/ring_test.c -o build/ring_test
	./build/ring_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/pcm_fast_test.c -o build/pcm_fast_test
	./build/pcm_fast_test
	cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined,address tests/gain_test.c -o build/gain_test
	./build/gain_test

test-cli: all
	python3 tests/cli_test.py

build/dma68k_test: tests/dma_test.c src/dma_plan.h src/dma_control.h
	mkdir -p build
	$(CC) $(CFLAGS) tests/dma_test.c -o $@

test-dma68k: build/dma68k_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C", "68020", "build/dma68k_test"]))'

build/stream68k_test: tests/stream_test.c src/stream.h src/audio.h src/tone.h src/dma_plan.h
	mkdir -p build
	$(CC) $(CFLAGS) tests/stream_test.c -o $@

test-stream68k: build/stream68k_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C", "68020", "build/stream68k_test"]))'

package: all test
	cp README.txt dist/
	python3 package.py

clean:
	rm -f dist/HDMITest dist/README.txt build/audio_test build/clock_test build/dma_test build/dma68k_test
	rm -f build/stream_test build/stream68k_test


.PHONY: copy-ahi test-ahi package-ahi
AHI_FLAGS = $(CFLAGS) -Ivendor/ahi -I../gic400.library/include
AHI_DEPS = ahi/ring.h ahi/refill.h ahi/pacing.h ahi/buffering.h ahi/watchdog.h ahi/guard.h ahi/status.h ahi/lifetime.h ahi/lifetime.c ahi/lifetime.S ahi/pcm.h src/hdmi_backend.h src/audio.h src/stream.h src/dma_control.h src/dma_plan.h src/clock_reply.h

dist/emu68hdmi.audio: ahi/entry.S ahi/driver.c ahi/runtime.c $(AHI_DEPS)
	mkdir -p dist
	$(CC) $(AHI_FLAGS) -fno-builtin -nostdlib ahi/entry.S ahi/driver.c ahi/runtime.c ahi/lifetime.c ahi/lifetime.S -lgcc -o $@

dist/HDMIAHITest: ahi/HDMIAHITest.c ahi/diag.h ahi/status.h ahi/status.S src/tone.h
	mkdir -p dist
	$(CC) $(AHI_FLAGS) ahi/HDMIAHITest.c ahi/status.S -o $@

dist/Emu68HDMI: ahi/modefile.py
	python3 ahi/modefile.py

copy-ahi: dist/emu68hdmi.audio dist/HDMIAHITest dist/Emu68HDMI ahi/README.txt
	mkdir -p "$(COPY_DIR)/Emu68HDMI-AHI/Devs/AHI" "$(COPY_DIR)/Emu68HDMI-AHI/Devs/AudioModes"
	cp dist/emu68hdmi.audio "$(COPY_DIR)/Emu68HDMI-AHI/Devs/AHI/"
	cp dist/Emu68HDMI "$(COPY_DIR)/Emu68HDMI-AHI/Devs/AudioModes/"
	cp dist/HDMIAHITest "$(COPY_DIR)/HDMIAHITest"
	cp dist/HDMIAHITest ahi/README.txt "$(COPY_DIR)/Emu68HDMI-AHI/"

build/ahi_abi_test: ahi/abi_test.c ahi/abi_test.S ahi/status.S ahi/status.h dist/emu68hdmi.audio
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/abi_test.c ahi/abi_test.S ahi/status.S -o $@

test-ahi: build/ahi_abi_test build/ahi_pcm_test dist/Emu68HDMI
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_abi_test"]))'
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_pcm_test"]))'

build/ahi_pcm_test: ahi/pcm_test.c ahi/pcm.h src/audio.h src/dma_plan.h
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/pcm_test.c -o $@

package-ahi: all
	python3 ahi/package.py

.PHONY: copy-unit-test
all: copy-unit-test

dist/HDMIUnitTest: ahi/HDMIUnitTest.c src/tone.h
	mkdir -p dist
	$(CC) $(AHI_FLAGS) ahi/HDMIUnitTest.c -o $@

copy-unit-test: dist/HDMIUnitTest
	mkdir -p "$(COPY_DIR)/Emu68HDMI-AHI"
	cp dist/HDMIUnitTest "$(COPY_DIR)/HDMIUnitTest"
	cp dist/HDMIUnitTest "$(COPY_DIR)/Emu68HDMI-AHI/HDMIUnitTest"

.PHONY: copy-diag
all: copy-diag

dist/HDMIDiag: ahi/HDMIDiag.c ahi/diag.h ahi/status.h ahi/status.S
	mkdir -p dist
	$(CC) $(AHI_FLAGS) ahi/HDMIDiag.c ahi/status.S -o $@

copy-diag: dist/HDMIDiag
	mkdir -p "$(COPY_DIR)/Emu68HDMI-AHI"
	cp dist/HDMIDiag "$(COPY_DIR)/HDMIDiag"
	cp dist/HDMIDiag "$(COPY_DIR)/Emu68HDMI-AHI/HDMIDiag"

.PHONY: test-watchdog
test-watchdog: build/ahi_watchdog_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_watchdog_test"]))'

build/ahi_watchdog_test: ahi/watchdog_test.c ahi/watchdog.h ahi/status.h src/stream.h src/dma_control.h
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/watchdog_test.c -o $@

.PHONY: copy-unit-prefs
all: copy-unit-prefs

dist/SetHDMIUnit0: ahi/SetHDMIUnit0.c
	mkdir -p dist
	$(CC) $(CFLAGS) ahi/SetHDMIUnit0.c -o $@

copy-unit-prefs: dist/SetHDMIUnit0
	mkdir -p "$(COPY_DIR)"
	cp dist/SetHDMIUnit0 "$(COPY_DIR)/SetHDMIUnit0"

.PHONY: test-ahi-buffering
test-ahi-buffering: build/ahi_buffering_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_buffering_test"]))'

build/ahi_buffering_test: ahi/buffering_test.c ahi/buffering.h ahi/watchdog.h ahi/status.h src/stream.h
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/buffering_test.c -o $@

.PHONY: test-ahi-pacing
test-ahi-pacing: build/ahi_pacing_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_pacing_test"]))'

build/ahi_pacing_test: ahi/pacing_test.c ahi/pacing.h ahi/buffering.h ahi/pcm.h src/stream.h
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/pacing_test.c -o $@

.PHONY: test-ahi-refill
test-ahi-refill: build/ahi_refill_test build/ahi_publish_test
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_refill_test"]))'
	python3 -c 'from amitools.tools.vamos import main; import sys; sys.exit(main(["-C","68020","build/ahi_publish_test"]))'

build/ahi_refill_test: ahi/refill_test.c ahi/refill.h ahi/pacing.h ahi/pcm.h ahi/buffering.h ahi/watchdog.h src/stream.h
	mkdir -p build
	$(CC) $(AHI_FLAGS) ahi/refill_test.c -o $@

build/ahi_publish_impl.h: src/hdmi_backend.h
	mkdir -p build
	python3 -c 'from pathlib import Path; s=Path("src/hdmi_backend.h").read_text(); Path("$@").write_text(s[s.index("static int stream_publish("):])'

build/ahi_publish_test: ahi/publish_test.c build/ahi_publish_impl.h ahi/buffering.h src/stream.h src/dma_control.h
	$(CC) $(AHI_FLAGS) ahi/publish_test.c -o $@

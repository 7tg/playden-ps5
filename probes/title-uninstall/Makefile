# Build with: scripts/with-env.sh make -C probes/title-uninstall
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

uninstall-probe.elf: main.c
	$(CC) -Wall -Werror -O2 -o $@ main.c -lSceIpmi -lSceAppInstUtil -lSceUserService -lSceSystemService

clean:
	rm -f uninstall-probe.elf

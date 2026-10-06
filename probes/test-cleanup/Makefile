# Build with: scripts/with-env.sh make -C probes/test-cleanup
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

test-cleanup.elf: main.c
	$(CC) -Wall -Werror -O2 -o $@ main.c

clean:
	rm -f test-cleanup.elf

#include <zephyr/toolchain.h>
#include "credentials.h"

/*
 * Not const: a const array goes to flash, which user threads can read.
 * Aligned like the settings table entries, so the key sits at a whole entry index.
 */
static char device_key[32] __aligned(16) = "EC-PRIVATE-KEY:8f3a91c25d7e04b6";

/* Only used by main to simulate an attacker who knows the key's address. */
const void *credentials_key_addr(void)
{
	return device_key;
}

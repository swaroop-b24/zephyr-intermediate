#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/* Const, so it is in flash: readable by the parser with or without isolation. */
static const char settings[][SETTING_SIZE] __aligned(16) = {
	"led=off",
	"interval=10",
	"mode=auto",
};

/*
 * Parses one command received from the network.
 * Supported command: "get=<n>", returns setting number n.
 */
void parse_command(const char *cmd)
{
	if (strncmp(cmd, "get=", 4) == 0) {
		long index = strtol(cmd + 4, NULL, 10);
		char value[SETTING_SIZE];

		/* Bug: the index comes from the packet and is not checked against the table size. */
		memcpy(value, settings[index], sizeof(value));
		printk("parser: setting %ld = %.16s\n", index, value);
		return;
	}

	printk("parser: unknown command '%s'\n", cmd);
}

/* Only used by main to simulate an attacker who knows the table's address. */
const void *parser_settings_addr(void)
{
	return settings;
}

#ifndef PARSER_H
#define PARSER_H

#define SETTING_SIZE 16

/* Parses one command received from the network. */
void parse_command(const char *cmd);

/* Returns the address of the settings table. */
const void *parser_settings_addr(void);

#endif

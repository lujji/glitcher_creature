#ifndef CLI_H
#define CLI_H

/**
 * Initialized command line interface
 */
void cli_init();

/**
 * Process received character
 */
void cli_process(char *buf, int len);

#endif // CLI_H

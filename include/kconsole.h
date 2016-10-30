/*
 * kconsole.h
 *
 *  Created on: 13.07.2016 ã.
 *      Author: Admin
 */

#ifndef INCLUDE_KCONSOLE_H_
#define INCLUDE_KCONSOLE_H_

#include "types.h"
#include <stdint.h>

void kcon_process_char(char c);
void kcon_prompt();
void kcon_parse_command(char *cmd);
void kcon_initialize();

/* Command handlers */
HRESULT __cmd_hello(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_shutdown(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_echo(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_kinfo(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_ls(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_hexcat(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_cat(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_help(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_vmmapdump(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_exec(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_ps(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_int81(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_play(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_cd(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_startwcs(char *cmd_line, char **args, uint32_t argc);
HRESULT __cmd_scanpci(char *cmd_line, char **args, uint32_t argc);

#endif /* INCLUDE_KCONSOLE_H_ */

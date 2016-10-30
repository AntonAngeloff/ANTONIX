/*
 * kconsole.c
 *
 *  Created on: 13.07.2016 ã.
 *      Author: Admin
 */

/**
 * @brief KConsole is simple kernel-mode console system, which is aimed to be used
 * for debugging in kernel mode only. It doesn't depend on the VFS.
 *
 * It is different from the virtual tty's which are used by user-space applications
 * through syscalls. Virtual tty's should emulate real terminal devices like VT100.
 * They are located in the VFS and work with stdin and stdout streams.
 */
#include <kconsole.h>
#include <string.h>
#include <vga.h>
#include <mm.h>
#include <kstdio.h>
#include <mm_virt.h>
#include <elf.h>
#include <scheduler.h>
#include <vfs.h>
#include <url_utils.h>
#include "drivers/pci_bus.h"
#include "subsystems/nxa.h"
#include "subsystems/nxgi.h"
#include "subsystems/henjin.h"

typedef struct {
	char *cmd;
	char *desc;
	char *usage;
	HRESULT (*handler)(char *cmd, char **args, uint32_t argc);
} K_CMD_HANDLER;

static K_CMD_HANDLER cmd_list[] = {
		{
				.cmd = "hello",
				.desc = "Request greeting from console.",
				.usage = NULL,
				.handler = __cmd_hello
		},
		{
				.cmd = "shutdown",
				.desc = "Shuts down the system.",
				.usage = NULL,
				.handler = __cmd_shutdown
		},
		{
				.cmd = "echo",
				.desc = "Writes arguments to standard output.",
				.usage = "echo [arg0 ...]",
				.handler = __cmd_echo
		},
		{
				.cmd = "kinfo",
				.desc = "Displays general information about the kernel.",
				.usage = "",
				.handler = __cmd_kinfo
		},
		{
				.cmd = "ls",
				.desc = "Lists directories and files inside a particular directory.",
				.usage = "ls [dir]",
				.handler = __cmd_ls
		},
		{
				.cmd = "cat",
				.desc = "Writes the contents of a file to the standard output. Note: it will also print an ASCII cat, if no parameters are given.",
				.usage = "cat [filename]",
				.handler = __cmd_cat
		},
		{
				.cmd = "hexcat",
				.desc = "Writes the contents of a file to the standard output as byte sequence, using hexadecimal notation. Default value for `start_addr` is 0 and for `length` is 32.",
				.usage = "hexcat [filename] [start_addr] [length]",
				.handler = __cmd_hexcat
		},
		{
				.cmd = "help",
				.desc = "Lists available commands and displays information about particular one on demand.",
				.usage = "help [command]",
				.handler = __cmd_help
		},
		{
				.cmd = "vmmapdump",
				.desc = "Lists virtual memory regions for kernel or specific userspace process.",
				.usage = "vmmapdump [pid]",
				.handler = __cmd_vmmapdump
		},
		{
				.cmd = "exec",
				.desc = "Executes an ELF binary program, located in the virtual file system, in a newly created process.",
				.usage = "exec [filename]",
				.handler = __cmd_exec
		},
		{
				.cmd = "ps",
				.desc = "Lists running processes.",
				.usage = "ps",
				.handler = __cmd_ps
		},
		{
				.cmd = "int81",
				.desc = "Performs software task switch to next task (yielding), by invoking interrupt 0x81.",
				.usage = "int30",
				.handler = __cmd_int81
		},
		{
				.cmd = "play",
				.desc = "Plays a PCM file throughout the NXA API.",
				.usage = "play <filename> [sample_rate] [channel_cnt] [bit_depth] ",
				.handler = __cmd_play
		},
		{
				.cmd = "cd",
				.desc = "Changes current working directory. Pseudo-directory \"..\" can be used to navigate to parent directory.",
				.usage = "cd <directory_name>",
				.handler = __cmd_cd
		},
		{
				.cmd = "startwcs",
				.desc = "Launches the experimental Henjin window compositor.",
				.usage = "startwcs",
				.handler = __cmd_startwcs
		},
		{
				.cmd = "scanpci",
				.desc = "Scans the PCI bus and lists attached devices.",
				.usage = "scanpci",
				.handler = __cmd_scanpci
		},

		{
				.cmd = NULL,
				.handler = NULL
		}
};

#define LINE_BUFFER_SIZE	1024

static char line_buffer[1024];
static uint32_t line_buffer_idx;
static char cwd[1024];

void kcon_process_char(char c)
{
	if (c != '\n') {
		if (line_buffer_idx >= LINE_BUFFER_SIZE-1) {
			vga_printf("KConsole: line buffer overflowed.\n");
			return;
		}

		/* Add to line buffer */
		line_buffer[line_buffer_idx++] = c;

		/* Print to screen */
		char s[2] = {c, 0};
		vga_print(s);

		return;
	}

	vga_print("\n");

	line_buffer[line_buffer_idx] = '\0';
	kcon_parse_command(line_buffer);

	if (line_buffer_idx > 0) {
		/* Add new line */
		vga_printf("\n");
	}

	/* Write new prompt */
	kcon_prompt();

	/* Clear line buffer */
	line_buffer[0] = '\0';
	line_buffer_idx = 0;
}

void kcon_prompt() {
	vga_printf("#%s> ", cwd);
}

void str_remove_empty_substrings(char **substr_arr, int *cnt)
{
	int c = *cnt;
	char **arr = substr_arr;

	for (int i=c-1; i>=0; i--) {
		if (*arr[i] == '\0') {
			/* Remove element */
			for (int j=i; j<c-1; j++) {
				arr[j] = arr[j+1];
			}

			c--;
		}
	}

	*cnt = c;
}

void kcon_parse_command(char *cmd)
{
	char **cmd_decomposed;
	int cmd_args;

	str_explode(cmd, ' ', &cmd_decomposed, &cmd_args);
	str_remove_empty_substrings(cmd_decomposed, &cmd_args);

	if (cmd_args == 0) {
		/* Empty line */
		return;
	}

	char *cmd_name = cmd_decomposed[0];

	/* Iterate command handlers to find proper handler for current command */
	for (int i=0; cmd_list[i].cmd != NULL; i++) {
		if (strcmp(cmd_name, cmd_list[i].cmd) == 0) {
			/* Command handler found */
			char **cmd_arg_ptr = cmd_args > 1 ? &cmd_decomposed[1] : NULL;

			/* Invoke handler */
			HRESULT hr = cmd_list[i].handler(cmd, cmd_arg_ptr, cmd_args-1);
			if (FAILED(hr)) {
				switch (hr) {
				case E_INVALIDARG:
					vga_printf("Invalid arguments for command \"%s\"\n", cmd_name);
					/* TODO: print usage message */
					break;
				default: HalKernelPanic("Kernel console: command failed.");
				}
			}

			/* Cleanup resources */
			goto finally;
		}
	}

	vga_printf("Unknown command \"%s\"\n", cmd_name);

finally:
	str_explode_cleanup(&cmd_decomposed, cmd_args);
}

void kcon_initialize()
{
	line_buffer[0] = '\0';
	line_buffer_idx = 0;
	strcpy(cwd, "/");
}

HRESULT __cmd_hello(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);
	UNUSED_ARG(argc);

	vga_printf("Hello!\n");
	return S_OK;
}

HRESULT __cmd_shutdown(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);
	UNUSED_ARG(argc);

	if (argc != 0) {
		return E_INVALIDARG;
	}

	HalKernelPanic("Shutting down by user request.");
	return S_OK;
}

HRESULT __cmd_echo(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(args);

	/* Lets implement the first and one of the simplest console commands */
	if (argc > 0) {
		vga_print(strchr(cmd_line, ' ')+1);
	}

	vga_print("\n");
	return S_OK;
}

HRESULT __cmd_kinfo(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);
	UNUSED_ARG(argc);

	k_printf("You are running %s v%d.%d\n", KERNEL_NAME, KERNEL_VERSION_MAJ, KERNEL_VERSION_MIN);
	k_printf("ANTONIX is a tiny hobby/educational kernel targeted for IA-32 architecture. Is not based, neither reuses source code from another existing OS.\n\n");

	/* Print kernel location in memory */
	uint32_t k_start, k_end;
	mm_get_kernel_physical_location(&k_start, &k_end);
	vga_printf("Kernel physical location: [%x..%x]\n", k_start, k_end);

	mm_get_kernel_virtual_location(&k_start, &k_end);
	vga_printf("Kernel virtual location:  [%x..%x]\n", k_start, k_end);

	vga_printf("Kernel image size: %dkb\n", (k_end - k_start) / 1024);

	return S_OK;
}

HRESULT __cmd_ls(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	char *dir;

	/* If no argument is given, list CWD */
	switch (argc) {
	case 0:
		dir = cwd;
		break;
	case 1:
		dir = args[0];
		break;
	default:
		return E_INVALIDARG;
	}

	/* List directories */
	K_DIR_STREAM 	*d;
	HRESULT 		hr;
	char			dirname[VFS_MAX_DIRNAME_LENGTH];

	strcpy(dirname, dir);
	url_exclude_trailing_path_delimeter(dirname);

	hr = k_opendir(dirname, &d);
	if (FAILED(hr)) {
		vga_printf("Failed to open directory \"%s\". Error: %s\n", dir, hr_to_str(hr));
		return S_FALSE;
	}

	char name[MAX_FILENAME_LENGTH];
	K_FS_NODE_INFO info;
	int cnt=0;

	while (SUCCEEDED(k_readdir(d, name, &info))) {
		if (info.node_type & FS_NODE_TYPE_DIRECTORY) {
			/* Directory */
			vga_printf("%s/ ", name);
		}else if (info.node_type == FS_NODE_TYPE_FILE || info.node_type == FS_NODE_TYPE_MOUNTPOINT) {
			/* File */
			vga_printf("%s ", name);
		}else {
			/* Special file */
			vga_printf("@%s ", name);
		}

		cnt++;
	}

	k_closedir(&d);

	/* If not sub-items, notify that directory is empty */
	if (cnt == 0) {
		vga_print("Directory is empty.");
	}

	vga_print("\n");
	return S_OK;
}

/**
 * Prints file contents to screen
 */
HRESULT __cmd_cat(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	/* Validate argument count */
	if (argc > 1) {
		return E_INVALIDARG;
	}

	if (argc == 0) {
		/* If no args, print a ascii cat (joke) */
		char *cat =
				"\n \
				 \\`*-.                   \n \
				  )  _`-.                \n \
				 .  : `. .               \n \
				 : _   '  \\              \n \
				 ; *` _.   `*-._         \n \
				 `-.-'          `-.      \n \
				   ;       `       `.    \n \
				   :.       .        \\   \n \
				   . \\  .   :   .-'   .  \n \
				   '  `+.;  ;  '      :  \n \
				   :  '  |    ;       ;-.\n \
				   ; '   : :`-:     _.`* ;\n \
		        .*' /  .*' ; .*`- +'  `*'\n \
				`*-*   `*-*  `*-*'       \n";

		vga_print(cat);
		return S_OK;
	}

	char str[MAX_FILENAME_LENGTH];

	if (args[0][0] == PATH_DELIMITER) {
		/* If argument starts with path delimiter, threat it as
		 * an absolute file path.
		 */
		strcpy(str, args[0]);
	}else {
		/* Otherwise concatenate CWD with argument filename
		 */
		int l = strlen(cwd);
		strcpy(str, cwd);
		strcpy(str + l, args[0]);
	}

	K_STREAM *f;
	HRESULT hr;

	hr = k_fopen(str, FILE_OPEN_READ, &f);
	if (FAILED(hr)) {
		vga_printf("Failed to open file \"%s\". Error: %s (%x)\n", str, hr_to_str(hr), hr);
		return S_FALSE;
	}

	char buff[1025];
	size_t bytes;

	while (SUCCEEDED(k_fread(f, 1024, buff, &bytes))) {
		/* Insert null terminator */
		buff[bytes] = '\0';

		/* Print buffer */
		vga_printf(buff);
	}

	k_fclose(&f);
	return S_OK;
}

HRESULT __cmd_hexcat(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	/* Validate argument count */
	if (argc > 1) {
		return E_INVALIDARG;
	}

	char str[MAX_FILENAME_LENGTH];
	uint32_t skip_blocks = 2;
	uint32_t length = 64;

	if (args[0][0] == PATH_DELIMITER) {
		/* If argument starts with path delimiter, threat it as
		 * an absolute file path.
		 */
		strcpy(str, args[0]);
	}else {
		/* Otherwise concatenate CWD with argument filename
		 */
		int l = strlen(cwd);
		strcpy(str, cwd);
		strcpy(str + l, args[0]);
	}

	K_STREAM *f;
	HRESULT hr;

	hr = k_fopen(str, FILE_OPEN_READ, &f);
	if (FAILED(hr)) {
		vga_printf("Failed to open file \"%s\". Error: %s (%x)\n", str, hr_to_str(hr), hr);
		return S_FALSE;
	}

	#define READ_BLOCK_SIZE 32
	char buff[READ_BLOCK_SIZE];
	size_t bytes, i;
	char *matrix = "0123456789ABCDEF";

	while (SUCCEEDED(k_fread(f, READ_BLOCK_SIZE, buff, &bytes))) {
		if (skip_blocks > 0) {
			skip_blocks--;
			continue;
		}

		for (i=0; i<bytes; i++) {
			uint8_t v = (uint8_t)buff[i];
			char str[5] = "0x00";
			str[2] = matrix[v >> 4];
			str[3] = matrix[v & 0xF];

			k_printf("%s ", str);

			if (i%8 == 7) {
				k_printf("\n");
			}
		}

		if (length <= READ_BLOCK_SIZE) {
			break;
		}
		length -= READ_BLOCK_SIZE;
	}

	k_fclose(&f);
	return S_OK;
}


/**
 * Displays information about usage of commands
 */
HRESULT __cmd_help(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	if (argc > 1) {
		return E_INVALIDARG;
	}

	if (argc == 0) {
		vga_print("Listing all available commands.\nType \"help <command>\" for additional information.\n\n");

		/* List all commands */
		int i = 0;
		int w = 80 / 4; //VGA text screen is 80 chas wide.

		while (cmd_list[i].cmd != NULL) {
			int pad = w - strlen(cmd_list[i].cmd);

			vga_printf(cmd_list[i].cmd);
			if (i % 4 == 3) {
				vga_print("\n");
			} else {
				while (pad-- > 0) vga_print(" ");
			}

			i++;
		}

		vga_print("\n");
		return S_OK;
	}

	/* Find command */
	int i=0, id=-1;

	while (cmd_list[i].cmd != NULL) {
		if (strcmp(args[0], cmd_list[i].cmd) == 0) {
			id = i;
			break;
		}

		i++;
	}

	if (id == -1) {
		vga_printf("Invalid argument.\n");
		return S_OK;
	}

	/* Print information about specific command */
	vga_printf("Description:\n \t%s\n", cmd_list[i].desc);

	if (cmd_list[i].usage) {
		/* Print usage info, if available */
		vga_printf("Usage:\n \t%s\n", cmd_list[i].usage);
	}

	return S_OK;
}

HRESULT __cmd_vmmapdump(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);

	if (argc > 1) {
		return E_INVALIDARG;
	}

	K_PROCESS *proc;

	if (argc == 1) {
		uint32_t pid = 0x65;
		HRESULT hr =sched_find_proc(pid, &proc);

		if (FAILED(hr)) {
			vga_printf("Process with PID %x not found.\n", pid);
			return S_OK;
		}
	} else {
		sched_get_process_by_id(0, &proc);
	}

	size_t i, c;
	vmm_get_region_count(proc, &c);

	/* Dump kernel vm regions */
	for (i=0; i<c; i++) {
		K_VMM_REGION r;
		HRESULT hr = vmm_get_region(proc, i, &r);

		if (FAILED(hr)) {
			vga_printf("Failed to retrieve information about region n.$d\n", i);
			return hr;
		}

		char *usage_str, *access_str;

		switch ((uint32_t)r.usage) {
			case USAGE_KERNELHEAP: usage_str = "kernel-heap"; break;
			case USAGE_USERHEAP: usage_str = "user-heap"; break;
			case USAGE_KERNELSTACK: usage_str = "kernel-stack"; break;
			case USAGE_USERSTACK: usage_str = "user-stack"; break;
			case (USAGE_CODE | USAGE_USER): usage_str = "code-segment"; break;
			case (USAGE_DATA | USAGE_USER): usage_str = "data-segment"; break;
			case USAGE_TEMP: usage_str = "temp"; break;
			default: usage_str = "unknown";
		}

		switch (r.access) {
			case ACCESS_READ: access_str = "read"; break;
			case ACCESS_WRITE: access_str = "write"; break;
			case ACCESS_READWRITE: access_str = "read-write"; break;
			default: access_str = "unknown";
		}

		vga_printf("%d. %x(phys) -> %x(virt)\n", i, r.phys_addr, r.virt_addr);
		vga_printf("   Size: %d (%d kb)	Usage: %s(%d) Access: %s\n", r.region_size, r.region_size / 1024, usage_str, r.usage, access_str);
		vga_print("\n");
	}

	vga_printf("Total: %d region(s).", c);
	return S_OK;
}

HRESULT __cmd_exec(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	if (argc != 1) {
		return E_INVALIDARG;
	}

	/* Open executable */
	K_STREAM *s;
	HRESULT hr;

	hr = k_fopen(args[0], FILE_OPEN_READ, &s);
	if (FAILED(hr)) {
		vga_printf("Failed to open file.");
		return S_OK;
	}

	uint32_t pid;
	hr = elf_execute(s, &pid);
	if (FAILED(hr)) vga_printf("Failed to execute file.");

	hr = k_fclose(&s);
	if (FAILED(hr)) HalKernelPanic("Failed to close file.");

	/* Block current thread until executed process exits */
	do {
		K_PROCESS *p;
		hr = sched_find_proc(pid, &p);

		if (SUCCEEDED(hr)) {
			sched_update_sw();
		}
	} while (hr != E_NOTFOUND);

	return S_OK;
}

HRESULT __cmd_ps(char *cmd_line, char **args, uint32_t argc) {
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);

	if (argc != 0) {
		return E_INVALIDARG;
	}

	uint32_t cnt = sched_get_process_count();
	if (cnt > 0) {
		vga_print("Id \tPID  \tThreads\tRegions\n");
	}

	for (uint32_t i=0; i<cnt; i++) {
		K_PROCESS *p;
		HRESULT hr;

		hr = sched_get_process_by_id(i, &p);
		if (SUCCEEDED(hr)) {
			vga_printf("%d. \t%d \t%x        \t%x\n", i+1, p->id, p->thread_count, p->region_count);
		}
	}

	vga_printf("\nTotal: %d\n", cnt);
	return S_OK;
}

HRESULT __cmd_int81(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);

	if (argc != 0) {
		return E_INVALIDARG;
	}

	sched_update_sw();
	vga_printf("Software rescheduling performed successfully.\n");

	return S_OK;
}

HRESULT __cmd_play(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	if (argc < 1 || argc > 4) {
		return E_INVALIDARG;
	}

	HRESULT 		hr;
	char			*filename = args[0];
	K_STREAM		*f = NULL;
	uint32_t		buff_size = 64*1024, bytes, gran_size=32*1024;
	uint8_t			*buff = kmalloc(buff_size);
	uint32_t		sample_cntr = 0;
	uint32_t		samples_per_sec;

	K_AUDIO_SESSION	*session;
	K_AUDIO_FORMAT	fmt = {0};

	if (buff == NULL) {
		vga_printf("Failed to allocate buffer.\n");
		goto finally;
	}

	/* Set default parameters */
	fmt.sample_rate = 22050;
	fmt.channels = 1;
	fmt.bit_depth = 8;

	/* Get additional arguments */
	if (argc > 1) {
		//Get sample rate;
	}
	if (argc > 2) {
		//Get channel cnt
	}
	if (argc > 3) {
		//Get sample size
	}

	/* Open fail */
	hr = k_fopen(filename, FILE_OPEN_READ, &f);
	if (FAILED(hr)) {
		vga_printf("Failed to open file.\n");
		goto finally;
	}

	/* Read first block */
	//k_fread(f, buff_size, buff, &bytes);

	K_VFS_NODE * n = f->priv_data;

	/* Open NXA session */
//	hr = nxa_open_session(&fmt, buff, bytes, &session);
	hr = nxa_open_session(&fmt, n->content, n->desc.size, &session);
	if (FAILED(hr)) {
		vga_printf("Failed to open NXA session.\n");
		goto finally;
	}
//
//	/*
//	 * Read-back format settings
//	 */
//	nxa_get_format(session, &fmt);
//	samples_per_sec = fmt.sample_rate * fmt.channels;
//
//	/* Loop until full file is played */
//	while (TRUE) {
//		uint32_t free_size;
//
//		hr = nxa_get_free_buffer_space(session, &free_size);
//		if (FAILED(hr)) {
//			vga_printf("Error occurred while buffering audio.\n");
//			goto finally;
//		}
//
//		if (free_size < gran_size) {
////			sched_update_sw();
//			vga_printf("buffer full.. ");
//			//vga_printf("buffer overflow: free_size(%x) < buff_size(%x)\n", free_size, buff_size/2);
//			continue;
//		}
//
//		hr = k_fread(f, gran_size, buff, &bytes);
//		if (FAILED(hr) && hr != E_ENDOFSTR) {
//			vga_printf("Error while reading from file.\n");
//			goto finally;
//		}else if (hr == E_ENDOFSTR) {
//			/* End of file */
//			vga_printf("End of file reached.\n");
//		}
//
//		sample_cntr += bytes / fmt.block_size;
//		uint32_t msecs = 1000 * sample_cntr / samples_per_sec;
//
////		vga_printf("%d:%d:%d (cntr: %d)\n", msecs / 60000, msecs / 1000 % 60, msecs % 1000, sample_cntr);
//
////		memset(buff, 0, buff_size);
//
//		if (bytes > 0) {
////			vga_printf("!!queue crc=%x\n", temp_crc(buff, bytes));
//			hr = nxa_queue_audio(session, buff, bytes);
////			vga_printf("audio si queued(%x bytes)\n", bytes);
//			if (FAILED(hr)) {
//				vga_printf("Error occurred while buffering audio.\n");
//				goto finally;
//			}
//		}
//
//		if (hr == E_ENDOFSTR) {
//			break;
//		}
//	}

finally:
	/* Free buffer */
	if (buff != NULL) {
		kfree(buff);
	}

	/* Close file */
	if (f != NULL) {
		k_fclose(&f);
	}

	vga_printf("begore nxa_close_session()\n");
	/* Close session */
	if (session != NULL) {
		nxa_close_session(&session, TRUE);
	}

	return S_OK;
}

/*
 * Traditional CD (change directory) command. Changes current working
 * directory (CWD).
 */
HRESULT __cmd_cd(char *cmd_line, char **args, uint32_t argc)
{
	UNUSED_ARG(cmd_line);

	/* Validate argument count */
	if (argc != 1) {
		return E_INVALIDARG;
	}

	K_DIR_STREAM 	*ds;
	uint32_t		offs;
	char 			*newdir = args[0];
	char 			*s;
	char 			newpath[VFS_MAX_DIRNAME_LENGTH];
	char			newpath_tmp[VFS_MAX_DIRNAME_LENGTH];
	HRESULT			hr;

	if (newdir[0] == VFS_PATH_DELIMITER) {
		/* Handle change to absolute path */
		strcpy(newpath_tmp, newdir);
	} else {
		/* Interpret relative path */
		offs = strlen(cwd);

		strcpy(newpath_tmp, cwd);
		s = newpath_tmp;
		strcpy(s+offs, newdir);
	}

	/* Normalize path */
	hr = url_normalize(newpath_tmp, newpath);
	if (FAILED(hr)) {
		k_printf("Invalid directory name/path.\n");
	}

	/* Make sure new directory exists */
	hr = k_opendir(newpath, &ds);
	if (FAILED(hr)) {
		k_printf("Directory \"%s\" does not exist.\n", newdir);
		return S_OK;
	}

	k_closedir(&ds);

	/* Set new CWD */
	strcpy(cwd, newpath);
	url_append_trailing_path_delimeter(cwd);

	return S_OK;
}

HRESULT __cmd_scanpci_cb(K_PCI_CONFIG_ADDRESS addr, uint32_t vendor_id, uint32_t device_id, void *user)
{
	UNUSED_ARG(user);

	const char *device = pci_get_device_name(vendor_id, device_id);

	if (strcmp(device, "Unknown") == 0) {
		k_printf("Bus %d, slot %d, function %d: Unknown(%x:%x)\n",
					addr.bus_id,
					addr.device_id,
					addr.function_id,
					vendor_id,
					device_id
		);
	} else {
		k_printf("Bus %d, slot %d, function %d: %s\n",
					addr.bus_id,
					addr.device_id,
					addr.function_id,
					device
		);
	}
	return S_OK;
}

/*
 * Scans the PCI bus controller and prints info about attached devices.
 */
HRESULT __cmd_scanpci(char *cmd_line, char **args, uint32_t argc)
{
	HRESULT	hr;
	K_PCI_SCAN_PARAMS params;

	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);
	UNUSED_ARG(argc);

	/* Setup params */
	params.cb 	= __cmd_scanpci_cb;
	params.user 	= NULL;
	params.class_selector = PCI_CLASS_SELECTOR_ALL;
	params.subclass_selector = PCI_SUBCLASS_SELECTOR_ALL;

	/* Scan */
	hr = pci_scan(&params);
	if (FAILED(hr)) {
		k_printf("Failed to scan PCI bus.\n");
		return S_OK;
	}

	return S_OK;
}

/*
 * Launches the experimental Window Composition Server
 */
HRESULT __cmd_startwcs(char *cmd_line, char **args, uint32_t argc)
{
//	NXGI_BITMAP 			*screen;
//	NXGI_GRAPHICS_CONTEXT 	*gc;
//	HRESULT					hr;

	UNUSED_ARG(cmd_line);
	UNUSED_ARG(args);
	UNUSED_ARG(argc);

	henjin_start_server();

	return S_OK;
}

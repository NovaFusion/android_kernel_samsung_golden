/*
 * Copyright (c) 2010, The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Google, Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *  Adopted from lsof.c from the base Android project to display lsof information 
 *  to the console during kernel panic.  The following modifications are made to 
 *  the original lsof.c source:
 *
 *  1.  /proc file system accesses are made through syscall interfaces
 *  2.  removed USER and TYPE fields, which always prints only "???"
 *  3.  COMMAND is obtained from the /proc/<id>/stat file instead of 
 *      /proc/<id>/cmdline
 */

#include <linux/types.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>

#include <linux/dirent.h>

#define BUF_MAX 20
#define LSOF_PATH_MAX 1024
#define CMD_DISPLAY_MAX 24

struct pid_info_t {
    pid_t pid;

    char cmdline[CMD_DISPLAY_MAX+2];

    char path[LSOF_PATH_MAX];
    ssize_t parent_length;
};

static struct pid_info_t info;

struct map_entry_t {

	char device[10];
	size_t offset;
	long int inode;
	char file[LSOF_PATH_MAX];
};
static int read_line(int fd, char *buf, int max)
{
	int cnt = 0;
	int ret = 0;
	char chr = '\0';;

	if (fd>=0){
		while ((cnt<max)&&((ret = sys_read(fd,buf, 1)) > 0))
		{
			cnt++;
			if (*buf == '\n')
				break;
			buf++;
		}
	}
	else {
		return -1;
	}

	if (cnt >= max) {
		buf[max-1] = '\0';

		/* discard the rest of the line */
		while (chr != '\n')
			sys_read(fd,&chr, 1);
	}
		

	return cnt;
	
}

static int get_field(char* line, int field_num, char* buf, int buf_size, int *move_size)
{
	char *ptr;
	int field;
	int copied = 0;

	*buf='\0';

	ptr=line;
	
	while(( *ptr== ' ') || ( *ptr== '\t') || ( *ptr== '\n'))
		ptr++;

	for (field=1; field<field_num; ptr++)
	{
		if (*ptr == '\n') break;

		if (( *ptr== ' ') || ( *ptr== '\t'))
		{
			field++;
			ptr++;
			while (( *ptr== ' ') || ( *ptr== '\t'))
				ptr++;

			ptr--;
		}
	}

	while(( *ptr!= ' ') && ( *ptr!= '\t') && ( *ptr!= '\n') && ( copied < buf_size - 1))
	{
		*buf++ = *ptr++;
		copied++;
	}
	*buf = '\0';

	if(move_size!=0) {
		*move_size = (int) (ptr - line);
	}
	
	return copied;
}

static void print_header(void)
{
    printk(KERN_EMERG "[Open file information (lsof)]\n");

    printk(KERN_EMERG "%-26s %5s %4s %10s %9s %10s %s\n",
		"COMMAND",
		"PID",
		"FD",
		"DEVICE",
		"SIZE/OFF",
		"NODE",
		"NAME");
}

static void print_type(char *type, struct pid_info_t* info)
{
		static ssize_t link_dest_size;
		static char link_dest[LSOF_PATH_MAX];
	
		strncat(info->path, type, sizeof(info->path));

		if ((link_dest_size = sys_readlink(info->path, link_dest, sizeof(link_dest)-1)) < 0) {
			snprintf(link_dest, sizeof(link_dest), "%s (readlink: error)", info->path);
			goto out;
		} else {
			link_dest[link_dest_size] = '\0';
		}
	
		// Things that are just the root filesystem are uninteresting (we already know)
		if (!strcmp(link_dest, "/"))
			goto out;
	
		printk(KERN_EMERG "%-26s %5d %4s %10s %9s %10s %s\n", info->cmdline, info->pid, type,
				"???", "???", "???", link_dest);
	
	out:
		info->path[info->parent_length] = '\0';

}


// Prints out all file that have been memory mapped
static void print_maps(struct pid_info_t* info)
{
	char buf[LSOF_PATH_MAX + 100];
	size_t offset;
	char device[10];
	long int inode;
	char file[LSOF_PATH_MAX];
	char field[32];
	
	int fd;
	int read_size;
	int move_size;
	char* buf_ptr = buf;

	strncat(info->path, "maps", sizeof(info->path));

	fd = sys_open(info->path, O_RDONLY, 0);

	if (fd >=0)
	{
		while(( read_size = read_line(fd, buf, LSOF_PATH_MAX + 100)) > 0)
		{
			buf_ptr = buf;

			get_field(buf_ptr, 3,field ,32, &move_size);
			offset = simple_strtoul(field,NULL,16);
			buf_ptr += move_size;

			get_field(buf_ptr,1,device,10, &move_size);
			buf_ptr+= move_size;
			
			get_field(buf_ptr,1,field,32, &move_size);
			inode = simple_strtoul(field,NULL,10);
			buf_ptr+=move_size;
			
			get_field(buf_ptr,1,file,LSOF_PATH_MAX, &move_size);
			buf_ptr+=move_size;

			if (inode == 0 || !strcmp(device, "00:00"))
				continue;

			printk(KERN_EMERG "%-26s %5d %4s %10s %9zd %10ld %s\n", info->cmdline, info->pid, "mem",
					device, offset, inode, file);
	
			//printk(KERN_EMERG "%-9s %5d %10s %4s %9s %18s %9zd %10ld %s\n", info->cmdline, info->pid, "???", "mem",
			//		"???", device, offset, inode, file);
		}
	}

	sys_close(fd);
	info->path[info->parent_length] = '\0';

}

// Prints out all open file descriptors
static void print_fds(struct pid_info_t* info)
{
	int fd;
	struct linux_dirent64 *dirp;
	struct linux_dirent64 buf[BUF_MAX];

	int num;
	static char* fd_path = "fd/";
	int previous_length ;
	
	strncat(info->path, fd_path, sizeof(info->path));

	previous_length = info->parent_length;
	info->parent_length += strlen(fd_path);


	fd = sys_open(info->path, O_RDONLY, 0);

	if (fd >= 0) {
	
		dirp = buf;
		num = sys_getdents64(fd, dirp, sizeof(buf));
		
		while (num > 0) {
			while (num > 0) {

				if ( strcmp(dirp->d_name, ".") && strcmp(dirp->d_name, ".."))
					print_type(dirp->d_name, info);

				num -= dirp->d_reclen;
				dirp = (void *)dirp + dirp->d_reclen;
			}
			
			dirp = buf;
			memset(buf, 0, sizeof(buf));
			num = sys_getdents64(fd, dirp, sizeof(buf));
		}
	
	}

	sys_close(fd);
	info->parent_length = previous_length;
	info->path[info->parent_length] = '\0';

}

static void get_cmd(pid_t pid, char* cmd, int cmd_size)
{
	char path[32];
	char buf[128];
	int fd;

	 snprintf(path, sizeof(path), "/proc/%d/stat", pid);

	fd = sys_open(path, O_RDONLY, 0);

	if (fd >= 0) {
		if (sys_read(fd, buf, 128) > 0)
			get_field(buf, 2, cmd, cmd_size, NULL);
		
		/* strip brackets */
		if (*cmd == '(') {
			while( *(cmd+1) && (*(cmd+1)!=')')) {
				*cmd = *(cmd+1);
				cmd++;
			}
			*(cmd) = '\0';
		}
	}

}
static void lsof_dumpinfo(pid_t pid)
{

    memset(&info, 0, sizeof(info));
	
    info.pid = pid;
    snprintf(info.path, sizeof(info.path), "/proc/%d/", pid);
    info.parent_length = strlen(info.path);

    /* read from stat file instead of cmdline, since reading from cmdline requires scheduling 
         and cannot function in panic */
    get_cmd(pid, info.cmdline, CMD_DISPLAY_MAX+2);

    // Read each of these symlinks
    print_type("cwd", &info);
    print_type("exe", &info);
    print_type("root", &info);

    print_fds(&info);
    print_maps(&info);
}

static inline int isdigit(int ch)
{
	return (ch >= '0') && (ch <= '9');
}

static int nm2id(char * name, int *id)
{
	int tid;

	for (*id = tid = 0; *name; name++) {

		if (!isdigit(*name))
		{
			return 0;
		}
		tid = tid * 10 + (int)(*name - '0');
	}
	*id = tid;
	return 1;	
}


void sec_disp_lsof_info(void)
{
	int fd;
	unsigned long old_fs;
	int pid;
	struct linux_dirent64 *dirp;
	struct linux_dirent64 buf[BUF_MAX];
	int num;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fd = sys_open("/proc", O_RDONLY, 0);

	if (fd >= 0) {
		print_header();

		dirp = buf;
		num = sys_getdents64(fd, dirp, sizeof(buf));
		
		while (num > 0) {
			while (num > 0) {
				if (nm2id(dirp->d_name, &pid)) {
					lsof_dumpinfo(pid);
				}
				num -= dirp->d_reclen;
				dirp = (void *)dirp + dirp->d_reclen;
			}
			
			dirp = buf;
			memset(buf, 0, sizeof(buf));
			num = sys_getdents64(fd, dirp, sizeof(buf));
		}
		printk(KERN_EMERG "\n");
	}
	set_fs(old_fs);
}


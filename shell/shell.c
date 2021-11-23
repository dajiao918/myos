#include "stdint.h"
#include "global.h"
#include "syscall.h"
#include "stdio.h"
#include "debug.h"
#include "file.h"
#include "buildin_cmd.h"
#include "string.h"

#define cmd_len 128
#define MAX_ARG_NR 16

static char cmd_line[cmd_len] = {0};
char final_path[MAX_PATH_LEN] = {0};
char cwd_cache[64] = {0};

void print_prompt(void)
{
    printf("[Mr's chen@localhost %s]$", cwd_cache);
}

static void readline(char *buf, int32_t count)
{
    ASSERT(buf != NULL && count > 0);
    char *pos = buf;
    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count)
    {
        switch (*pos)
        {
        case '\n':
        case '\r':
            *pos = 0;
            putchar('\n');
            return;
        case '\b':
            if (buf[0] != '\b')
            {
                pos--;
                putchar('\b');
            }
            break;
        default:
            putchar(*pos);
            pos++;
            break;
        }
    }
    printf("readline: can't find enter_key int the cmd_line,max num of char is 128\n");
}

static int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    ASSERT(cmd_str != NULL);
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR)
    {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    int argc = 0;
    char *next = cmd_str;
    while (*next)
    {
        //跳过多个token相连的时候
        while (*next == token)
        {
            next++;
        }
        //跳过之后字符串结束，那么跳出循环
        if (*next == 0)
        {
            break;
        }
        //将next指针存入argv数组中
        argv[argc] = next;
        //遍历到token为止，将token变为0,这样才能截取到单独的参数
        while (*next != token && *next != 0)
        {
            next++;
        }
        if (*next)
        {
            *next++ = 0;
        }
        if (argc > MAX_ARG_NR)
        {
            return -1;
        }
        argc++;
    }
    return argc;
}

char *argv[MAX_ARG_NR];

void myshell(void)
{
    cwd_cache[0] = '/';
    while (1)
    {
        print_prompt();
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, cmd_len);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0)
        {
            continue;
        }
        int argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1){
            printf("arguments exceed!\n");
            continue;
        }
        if (!strcmp("ls", argv[0])){
            buildin_ls(argc, argv);
        }
        else if (!strcmp("cd", argv[0])){
            if (buildin_cd(argc, argv) != NULL){
                memset(cwd_cache, 0, MAX_PATH_LEN);
                strcpy(cwd_cache, final_path);
            }
        }
        else if (!strcmp("pwd", argv[0])){
            buildin_pwd(argc, argv);
        }
        else if (!strcmp("ps", argv[0])){
            buildin_ps(argc, argv);
        }
        else if (!strcmp("clear", argv[0])){
            buildin_clear(argc, argv);
        }
        else if (!strcmp("mkdir", argv[0])){
            buildin_mkdir(argc, argv);
        }
        else if (!strcmp("rmdir", argv[0])){
            buildin_rmdir(argc, argv);
        }
        else if (!strcmp("rm", argv[0])){
            buildin_rm(argc, argv);
        }
        else{
            int32_t pid = fork();
            if(pid) {
                while(1);
            } else {
                //将路径变为绝对路径
                make_clear_abs_path(argv[0],final_path);
                argv[0] = final_path;
                struct stat file_stat;
                memset(&file_stat,0,sizeof(struct stat));
                if(stat(argv[0],&file_stat) == -1) {
                    printf("my_shell:cannot access %s,No such file or directory\n",argv[0]);
                } else {
                    execv(argv[0],argv);
                }
                while(1);
            }
        }
        int32_t arg_idx = 0;
        while(arg_idx < MAX_ARG_NR) {
            argv[arg_idx] = NULL;
            arg_idx ++;
        }
    }
}
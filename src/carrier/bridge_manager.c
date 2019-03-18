/*
 * bridge_manager.c
 *
 *  Created on: 2019年3月7日
 *      Author: nmsoccer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <slog/slog.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include "proc_bridge.h"
#include "carrier_lib.h"
#include "manager_lib.h"

extern int errno;
#define CARRIER_MANAGER_LOG_FILE "manager.log"
#define MANAGER_CMD_BUFF_LEN 1024

/***********INPUT CMD*********/
#define CMD_STR_EXIT "EXIT"
#define CMD_STR_STAT "STAT"
#define CMD_STR_CONN_ERR "PROB-CONN"
#define CMD_STR_SYS_ERR "PROB-SYS"
#define CMD_STR_ANY_ERR "PROB-ANY"
#define CMD_STR_PING "PING"

/************END**************/


typedef struct _carrier_manager_env_t
{
	int proc_id;
	char name_space[PROC_BRIDGE_NAME_SPACE_LEN];
	int slogd;
	bridge_hub_t *phub;
	int bd;	//bridge descriptor
	int fd[2];	//pipe
	FILE *fp_out;
	FILE *fp_in;

}carrier_manager_env_t;

static carrier_manager_env_t myenv;
static carrier_manager_env_t *penv = &myenv;

static int show_help(void)
{
	printf("usage:./manager -i <proc id> -N <name space>\n");
	printf("-i <proc_id> :manager's proc_id. default is 1\n");
	printf("-N <name_space> :name_space of proc bridge\n");
	return 0;
}

static int show_cmd();
static int parent_process(pid_t pid);
static int parent_parse_cmd(char *cmd);
static int child_process();
static int child_parse_cmd(manager_pipe_pkg_t *ppkg);
static int strip_exra_space(char *str);
static int print_rsp_stat(manager_cmd_rsp_t *prsp);
static int print_rsp_err(manager_cmd_rsp_t *prsp);
static int print_rsp_proto(manager_cmd_rsp_t *prsp);

int main(int argc , char **argv)
{
	SLOG_OPTION slog_option;
	pid_t pid;
	int opt;
	int ret;
	int slogd;

	/***Init*/
	memset(penv , 0 , sizeof(carrier_manager_env_t));
	penv->fp_out = stdout;
	penv->fp_in = stdin;

	if(argc < 2)
	{
		show_help();
		return 0;
	}

	/***Open Log*/
	memset(&slog_option , 0 , sizeof(slog_option));
	strncpy(slog_option.type_value._local.log_name , CARRIER_MANAGER_LOG_FILE , sizeof(slog_option.type_value._local.log_name));
	penv->slogd = slog_open(SLT_LOCAL , SL_DEBUG , &slog_option , NULL);
	if(penv->slogd < 0)
	{
		printf("open slog %s failed!\n" , CARRIER_MANAGER_LOG_FILE);
		return -1;
	}

	/***Arg Check*/
	slogd = penv->slogd;
	if(argc <= 0)
	{
		slog_log(slogd , SL_ERR , "Error,argc <= 0");
		return -1;
	}

	/*获取参数*/
	while((opt = getopt(argc , argv , "N:i:h")) != -1)
	{
		switch(opt)
		{
		case 'i':
			penv->proc_id = atoi(optarg);
			break;
		case 'N':
			if(optarg)
				strncpy(penv->name_space , optarg , sizeof(penv->name_space));
			break;
		case 'h':
			show_help();
			return 0;
		}
	}
	if(penv->proc_id <= 0)
	{
		slog_log(slogd , SL_ERR , "Error:Bad proc id arg!");
		return -1;
	}
	if(strlen(penv->name_space) <= 0)
	{
		slog_log(slogd , SL_ERR , "Error:name_space is NUll!");
		return -1;
	}
	slog_log(slogd , SL_INFO , "name_space:%s proc_id:%d" , penv->name_space , penv->proc_id);

	/***open bridge*/
	penv->bd = open_bridge(penv->name_space , penv->proc_id , penv->slogd);
	penv->phub = bd2bridge(penv->bd);
	if(penv->bd < 0 || !penv->phub)
	{
		slog_log(slogd , SL_ERR , "open bridge of %s:%d failed!" , penv->name_space , penv->proc_id);
		return -1;
	}
	slog_log(slogd , SL_INFO , "open bridge success! bd:%d" , penv->bd);

	/***Create Pipe*/
	ret = pipe2(penv->fd , O_NONBLOCK);
	if(ret < 0)
	{
		slog_log(slogd , SL_ERR , "create pipe failed! err:%s" , strerror(errno));
		return -1;
	}

	/***Print Name*/
	if(strlen(penv->phub->proc_name) > 0)
	{
		fprintf(penv->fp_out , "####################################################\n");
		fprintf(penv->fp_out , "#%51s\n" , "#");
		fprintf(penv->fp_out , "#%51s\n" , "#");
		fprintf(penv->fp_out , "%35s\n" , penv->phub->proc_name);
		fprintf(penv->fp_out , "#%51s\n" , "#");
		fprintf(penv->fp_out , "#%51s\n" , "#");
		fprintf(penv->fp_out , "###################################################\n");
	}
	/***Fork*/
	pid = fork();
	if(pid < 0)
	{
		slog_log(slogd , SL_ERR , "fork failed!err:%s" , strerror(errno));
		return -1;
	}
	else if(pid == 0)	//do child thing
	{
		return child_process();
	}


	parent_process(pid);
	return 0;
}

static int show_cmd()
{
	fprintf(penv->fp_out , "+--------CMD LIST--------+\n");
	fprintf(penv->fp_out , "【%s】 <proc_name> ping to check \n" , CMD_STR_PING);
	fprintf(penv->fp_out , "【%s】 <proc_name>[*] get status of <proc_name>[*]\n" , CMD_STR_STAT);
	//fprintf(penv->fp_out , "|STAT-ALL get all carrier status\n");
	fprintf(penv->fp_out , "【%s】 <proc_name>[*] show probable connection problems  of <proc_name>[*]\n" , CMD_STR_CONN_ERR);
	fprintf(penv->fp_out , "【%s】 <proc_name>[*] show probable sys problems  of <proc_name>[*]\n" , CMD_STR_SYS_ERR);
	fprintf(penv->fp_out , "【%s】 <proc_name>[*] show all probable problems of net or sys of <proc_name>[*]\n" , CMD_STR_ANY_ERR);
	fprintf(penv->fp_out , "【%s】 exit manager tool\n" , CMD_STR_EXIT);
	fprintf(penv->fp_out , "+--------------------------+\n");
	return 0;
}

/********parent process*******/
static int parent_process(pid_t pid)
{
	struct termios term;
	int status;
	int slogd = penv->slogd;
	int ret = -1;
	char cmd_buff[MANAGER_CMD_BUFF_LEN] = {0};
	int input_fd = -1;
	//show cmd
	show_cmd();
	slog_log(slogd  , SL_INFO , "parents starts... pid:%d" , getpid());
	//close pipe
	close(penv->fd[0]);

	//get input_fd
	input_fd = fileno(penv->fp_in);

	//set sepcial character
	ret = tcgetattr(input_fd , &term);
	if(ret < 0)
		slog_log(slogd , SL_ERR , "process tcgetattr failed! err:%s" , strerror(errno));
	else
	{
		term.c_cc[VERASE] = '\b';
		ret = tcsetattr(input_fd , TCSANOW , &term);
		if(ret < 0)
			slog_log(slogd , SL_ERR , "tcsetattr failed! err:%s" , strerror(errno));
	}


	while(1)
	{
		memset(cmd_buff , 0 ,sizeof(cmd_buff));
		/***wait child*/
		ret = waitpid(pid , &status , WNOHANG);
		if(ret > 0)
		{
			slog_log(slogd , SL_INFO , "child %d exit..." , ret);
			//exit too
			fprintf(penv->fp_out , "bye...\n");
			break;
		}
		else if(ret < 0)
		{
			slog_log(slogd , SL_ERR , "wait meets an error:%s" , strerror(errno));
		}

		/***Get Cmd*/
		fgets(cmd_buff , sizeof(cmd_buff) , stdin);
		//printf("parent get cmd:%s" , cmd_buff);

		/***Parse Cmd*/
		cmd_buff[strlen(cmd_buff)-1] = 0; //strip \n
		parent_parse_cmd(cmd_buff);
	}

	return 0;
}


static int parent_parse_cmd(char *str_cmd)
{
	int ret = -1;
	int is_exit = 0;
	manager_pipe_pkg_t pipe_pkg;
	manager_pipe_pkg_t *ppkg = &pipe_pkg;
	char src[MANAGER_CMD_BUFF_LEN] = {0};
	char *p = NULL;

	/***Arg Check*/
	if(!str_cmd || strlen(str_cmd)<=0)
	{
		show_cmd();
		return 0;
	}

	/***GET cmd & arg*/
	memset(ppkg , 0 , sizeof(manager_pipe_pkg_t));
	strncpy(src , str_cmd , sizeof(src));
	p = strchr(src , ' ');

	if(!p)	//only cmd
		strncpy(ppkg->cmd , src , sizeof(ppkg->cmd));
	else
	{
		p[0] = 0;
		p++;
		strncpy(ppkg->cmd , src , sizeof(ppkg->cmd));
		strncpy(ppkg->arg , p , sizeof(ppkg->arg));
	}

	/***Parse Cmd*/
	do
	{

		//PING
		if(strcmp(ppkg->cmd , CMD_STR_PING) == 0)
		{
			strip_exra_space(ppkg->arg);
			if(strchr(ppkg->arg , ' '))
			{
				fprintf(penv->fp_out , "Error:cmd 【%s】 only accept one arg!\n" , ppkg->cmd);
				return -1;
			}
				//check arg
			if(strlen(ppkg->arg) <= 0)
			{
				fprintf(penv->fp_out , "Error:cmd 【%s】 needs a arg of <proc_name>\n" , ppkg->cmd);
				return -1;
			}

			break;
		}

		//STAT,SHOW-ERR
		if(strcmp(ppkg->cmd , CMD_STR_STAT) == 0 || strcmp(ppkg->cmd , CMD_STR_ANY_ERR)==0 ||
			strcmp(ppkg->cmd , CMD_STR_CONN_ERR)==0 || strcmp(ppkg->cmd , CMD_STR_SYS_ERR)==0)
		{
			strip_exra_space(ppkg->arg);
			//printf("cmd:%s arg:%s\n" , ppkg->cmd  , ppkg->arg);
				//check multi arg
			if(strchr(ppkg->arg , ' '))
			{
				fprintf(penv->fp_out , "Error:cmd 【%s】 only accept one arg!\n" , ppkg->cmd);
				return -1;
			}
				//check arg
			if(strlen(ppkg->arg) <= 0)
			{
				fprintf(penv->fp_out , "Error:cmd 【%s】 needs a arg like <proc_name>*\n" , ppkg->cmd);
				return -1;
			}

				//post to child
			break;
		}


		//STAT-ALL
		if(strcmp(ppkg->cmd , "STAT-ALL") == 0)
		{
			//printf("cmd:%s arg:%s\n" , ppkg->cmd  , ppkg->arg);
			break;
		}


		//EXIT
		if(strcmp(ppkg->cmd , CMD_STR_EXIT) == 0)
		{
			is_exit = 1;
			break;
		}

		//unknown
		show_cmd();
		return 0;
	}while(0);

	/***Write to Pipe*/
	ret = write(penv->fd[1] , ppkg , sizeof(manager_pipe_pkg_t));
	if(ret < 0)
	{
		printf("parent writs to pipe failed! err:%s\n" , strerror(errno));
	}
	else
	{
		//printf("parent writes to pipe %d\n" , ret);
	}

	/***Whethor Out*/
	if(is_exit)
	{
		sleep(1);
		close_bridge(penv->bd);
		fprintf(penv->fp_out , "bye...\n");
		exit(0);
	}
	return 0;
}

/********child process*******/
static int child_process()
{
	manager_cmd_rsp_t rsp;

	manager_pipe_pkg_t pipe_pkg;
	manager_pipe_pkg_t *ppkg = &pipe_pkg;
	int ret = -1;
	int readed = 0;
	int should_read = 0;
	int slogd = penv->slogd;

	slog_log(slogd , SL_INFO , "child starts... pid:%d" , getpid());
	close(penv->fd[1]);
	while(1)
	{
		usleep(10000);
		/***Read Cmd*/
		should_read = sizeof(manager_pipe_pkg_t) - readed;
		ret = read(penv->fd[0] , &((char *)ppkg)[readed] , should_read);
		if(ret < 0)
		{
			if(errno == EAGAIN)
			{
				//fprintf(penv->fp_out , "read pipe no more data!\n");
				//sleep(1);
			}
			else
			{
				fprintf(penv->fp_out , "read pipe failed! err:%s\n" , strerror(errno));
				break;
			}
		}
		else if(ret == 0)
		{
			printf("peer closed!\n");
			break;
		}
		else if(ret == should_read)	//full pkg
		{
			//printf("child read %d from pipe! cmd:%s arg:%s\n" , ret , ppkg->cmd , ppkg->arg);
			readed = 0;	//try next new pkg
			child_parse_cmd(ppkg);
		}
		else	//not enugh
		{
			//printf("child read %d from pipe!\n" ,ret);
			readed += ret;
		}

		/***Read Bridge*/
		ret = recv_from_bridge(penv->bd , (char *)&rsp , sizeof(rsp) , 5);
		if(ret <= 0)
			continue;

		//print
		printf("read bridge pkg! recved:%d recv_len:%d type:%d\n" , ret , sizeof(rsp) , rsp.type);
		switch(rsp.type)
		{
		case MANAGER_CMD_STAT:
			print_rsp_stat(&rsp);
		break;
		case MANAGER_CMD_ERR:
			print_rsp_err(&rsp);
		break;
		case MANAGER_CMD_PROTO:
			print_rsp_proto(&rsp);
		break;
		default:
		break;
		}

	}// end while


	return 0;
}

static int child_parse_cmd(manager_pipe_pkg_t *ppkg)
{
	manager_cmd_req_t cmd_req;
	int ret = -1;

	slog_log(penv->slogd , SL_INFO , "[%s %s]" , ppkg->cmd , ppkg->arg);
	/***Handle Cmd*/
	do
	{
		//PING
		if(strcmp(ppkg->cmd , CMD_STR_PING) == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_PROTO;
			cmd_req.data.stat.type = CMD_PROTO_T_PING;
			strncpy(cmd_req.data.proto.arg , ppkg->arg , MANAGER_CMD_ARG_LEN);
			break;
		}

		//STAT
		if(strcmp(ppkg->cmd , CMD_STR_STAT) == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_STAT;
			cmd_req.data.stat.type = CMD_STAT_T_PART;
			strncpy(cmd_req.data.stat.cmd , ppkg->cmd , MANAGER_CMD_NAME_LEN);
			strncpy(cmd_req.data.stat.arg , ppkg->arg , MANAGER_CMD_ARG_LEN);
			break;
		}

		//STAT-ALL
		if(strcmp(ppkg->cmd , "STAT-ALL") == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_STAT;
			cmd_req.data.stat.type = CMD_STAT_T_ALL;
			break;
		}

		//CONN-ERR
		if(strcmp(ppkg->cmd , CMD_STR_CONN_ERR) == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_ERR;
			cmd_req.data.err.type = CMD_ERR_T_CONN;
			strncpy(cmd_req.data.err.cmd , ppkg->cmd , MANAGER_CMD_NAME_LEN);
			strncpy(cmd_req.data.err.arg , ppkg->arg , MANAGER_CMD_ARG_LEN);
			break;
		}

		//SYS-ERR
		if(strcmp(ppkg->cmd , CMD_STR_SYS_ERR) == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_ERR;
			cmd_req.data.err.type = CMD_ERR_T_SYS;
			strncpy(cmd_req.data.err.cmd , ppkg->cmd , MANAGER_CMD_NAME_LEN);
			strncpy(cmd_req.data.err.arg , ppkg->arg , MANAGER_CMD_ARG_LEN);
			break;
		}

		//ANY-ERR
		if(strcmp(ppkg->cmd , CMD_STR_ANY_ERR) == 0)
		{
			memset(&cmd_req , 0 , sizeof(cmd_req));
			cmd_req.type = MANAGER_CMD_ERR;
			cmd_req.data.err.type = CMD_ERR_T_ALL;
			strncpy(cmd_req.data.err.cmd , ppkg->cmd , MANAGER_CMD_NAME_LEN);
			strncpy(cmd_req.data.err.arg , ppkg->arg , MANAGER_CMD_ARG_LEN);
			break;
		}

		//EXIT
		if(strcmp(ppkg->cmd , CMD_STR_EXIT) == 0)
		{
			fprintf(penv->fp_out , "exit...\n");
			exit(0);
		}
	}
	while(0);

	/***Send*/
	ret = send_to_bridge(penv->bd , penv->proc_id , (char *)&cmd_req , sizeof(manager_cmd_req_t));
	if(ret != 0)
	{
		fprintf(penv->fp_out , "send to bridge failed!\n");
		return -1;
	}
	else
		//fprintf(penv->fp_out , "send to bridge success!\n");

	return 0;
}

//去除str首尾空格，并将其余位置空格压缩
static int strip_exra_space(char *str)
{
	char tmp_buff[MANAGER_CMD_BUFF_LEN] = {0};
	char prev = ' ';
	char *p = str;
	int i = 0;

	//strip space
	while(*p)
	{
		//replace \t
		if(*p == '\t')
			*p = ' ';

		//non-space
		if(*p != ' ')
		{
			tmp_buff[i++] = *p;
			prev = *p++;
			continue;
		}

		//space
		if(prev != ' ')	//前置字符不是空格 则该空格保留
		{
			tmp_buff[i++] = *p;
			prev = *p++;
			continue;
		}

		//去除多余空格
		p++;
	}

	//strip last space
	if(tmp_buff[strlen(tmp_buff)-1] == ' ')
		tmp_buff[strlen(tmp_buff)-1] = 0;


	//copy back
	memset(str , 0 , strlen(str));
	strncpy(str , tmp_buff , strlen(tmp_buff));	//strlen(tmp_buff) <= strlen(str)
	return 0;
}

static int print_rsp_stat(manager_cmd_rsp_t *prsp)
{
	cmd_stat_rsp_t *psub = NULL;
	/***Arg Check*/
	if(!prsp)
		return -1;

	/***Init*/
	psub = &prsp->data.stat;

	/***Print Head*/
	fprintf(penv->fp_out , "==========================\n");
	fprintf(penv->fp_out , "【%s %s】\n" , psub->req.cmd , psub->req.arg);
	fprintf(penv->fp_out , "==========================\n");

	/***Print Manager Stat*/
	if(prsp->manage_stat != MANAGE_STAT_OK)
	{
		fprintf(penv->fp_out , "Sorry Manager is not available! now is:%s\n" , prsp->manage_stat==MANAGE_STAT_INIT?"initing":"wrong");
		return 0;
	}

	/***Print Body*/
	if(psub->count > 0)
		print_manage_item_list(psub->seq+1 , psub->item_list , psub->count , penv->fp_out);
	else
		fprintf(penv->fp_out , "No Result Found!  Please check!\n");
	return 0;
}

static int print_rsp_err(manager_cmd_rsp_t *prsp)
{
	cmd_err_rsp_t *psub = NULL;
	/***Arg Check*/
	if(!prsp)
		return -1;

	/***Init*/
	psub = &prsp->data.err;

	/***Print Head*/
	fprintf(penv->fp_out , "==========================\n");
	fprintf(penv->fp_out , "【%s %s】\n" , psub->req.cmd , psub->req.arg);
	fprintf(penv->fp_out , "==========================\n");

	/***Print Manager Stat*/
	if(prsp->manage_stat != MANAGE_STAT_OK)
	{
		fprintf(penv->fp_out , "Sorry Manager is not available! now is:%s\n" , prsp->manage_stat==MANAGE_STAT_INIT?"initing":"wrong");
		return 0;
	}

	/***Print Body*/
	if(psub->count > 0)
		print_manage_item_list(psub->seq+1 , psub->item_list , psub->count , penv->fp_out);
	else
		fprintf(penv->fp_out , "No Result Found!  Please check!\n");
	return 0;
}

static int print_rsp_proto(manager_cmd_rsp_t *prsp)
{
	cmd_proto_rsp_t *psub = NULL;
	char cmd[MANAGER_CMD_NAME_LEN] = {0};
	/***Arg Check*/
	if(!prsp)
		return -1;

	/***Init*/
	psub = &prsp->data.proto;

	/**Convert*/
	switch(psub->type)
	{
	case CMD_PROTO_T_PING:
		strncpy(cmd , "PING" , sizeof(cmd));
		break;
	default:
		strncpy(cmd , "???" , sizeof(cmd));
		break;
	}

	/***Print Head*/
	fprintf(penv->fp_out , "==========================\n");
	fprintf(penv->fp_out , "【%s %s】\n" , cmd , psub->arg);
	fprintf(penv->fp_out , "==========================\n");

	/***Print Manager Stat*/
	if(prsp->manage_stat != MANAGE_STAT_OK)
	{
		fprintf(penv->fp_out , "Sorry Manager is not available! now is:%s\n" , prsp->manage_stat==MANAGE_STAT_INIT?"initing":"wrong");
		return 0;
	}

	/***Print Body*/
	if(psub->result < 0)
	{
		fprintf(penv->fp_out , "No Result Found!  Please check!\n");
		return -1;
	}

	//success
	switch(psub->type)
	{
	case CMD_PROTO_T_PING:
		fprintf(penv->fp_out , "PONG\n");
		break;
	default:
		break;
	}

	return 0;
}


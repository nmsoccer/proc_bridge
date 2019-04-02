/*
 * test_proc.c
 *
 *  Created on: 2013-12-25
 *      Author: Administrator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <proc_bridge/proc_bridge.h>

static char name_space[PROC_BRIDGE_NAME_SPACE_LEN] = {0};

static int show_help(void)
{
	printf("-i: proc id!\n");
	printf("-t: target id!\n");
	printf("-N: name_space \n");
	printf("-c: as a client\n");
	printf("-s: as a server\n");
	return 0;
}


int main(int argc , char **argv)
{
	char send_buff[128] = {0};
	char recv_buff[128] = {0};
	//bridge_hub_t *phub;
	int bd = -1;

	int opt;
	int proc_id = 0;
	int target_id = 0;
	int ret;
	char role = 0; //0:server 1:client
	int sender = 0;

	/***Arg Check*/
	if(argc <= 0)
	{
		fprintf(stdout , "Error,argc <= 0\n");
		return -1;
	}

	/*获取参数*/
	while((opt = getopt(argc , argv , "N:i:t:hcs")) != -1)
	{
		switch(opt)
		{
		case 'i':
			proc_id = atoi(optarg);
			break;
		case 't':
			if(optarg)
				target_id = atoi(optarg);
			break;
		case 'N':
			strncpy(name_space , optarg , sizeof(name_space));
			break;
		case 'c':
			role = 1;
			printf("act as a client...\n");
			break;
		case 's':
			role = 0;
			printf("act as a server...\n");
			break;
		case'h':
			show_help();
			return 0;
		}
	}

	//参数检查
	if(proc_id<=0)
	{
		fprintf(stdout , "Error:proc id not set!\n");
		return -1;
	}
	if(role==1 && target_id<=0)
	{
		fprintf(stdout , "Error:target id not set!\n");
		return -1;
	}
	if(strlen(name_space) <= 0)
	{
		fprintf(stdout , "Error:name_space null!\n");
		return -1;
	}

	/***打开bridge*/
	bd = open_bridge(name_space , proc_id , -1);
	if(bd < 0)
	{
		fprintf(stdout , "Error: open bridge failed!\n");
		return -1;
	}

	/***Handle*/
	if(role == 1) //client
	{
		while(1)
		{
			memset(recv_buff , 0 , sizeof(recv_buff));
			memset(send_buff , 0 , sizeof(send_buff));
			if(fgets(send_buff , sizeof(send_buff) , stdin) == NULL)
				break;
			send_buff[strlen(send_buff)-1] = 0;	/*去除\n*/

			/*send*/
			ret = send_to_bridge(bd , target_id , send_buff , strlen(send_buff));
			if(ret < 0)
			{
				fprintf(stdout , "[client]send to bridge failed! ret:%d,send_buff:%s\n" , ret , send_buff);
			}
			else
			{
				//fprintf(stdout , "send to bridge success! send_buff:%s\n" , send_buff);
			}

			sleep(1); //wait for msg arrived
			/*read*/
			ret = recv_from_bridge(bd , recv_buff , sizeof(recv_buff) , NULL , -1);
			if(ret < 0)
			{
				fprintf(stdout , "[client]recv from bridge failed! ret:%d\n" , ret);
			}
			else
			{
				fprintf(stdout , "%s\n" , recv_buff);
			}

		}
	}
	else //server
	{
		while(1)
		{
			memset(recv_buff , 0 , sizeof(recv_buff));
			memset(send_buff , 0 , sizeof(send_buff));
			/*read*/
			ret = recv_from_bridge(bd , recv_buff , sizeof(recv_buff) , &sender , -1);
			if(ret < 0)
			{
				//fprintf(stdout , "recv from bridge failed! ret:%d\n" , ret);
				usleep(2000);
				continue;
			}

			//fprintf(stdout , "[server ]recv from bridge success! recved:%d , msg:%s sender:%d \n" , ret , recv_buff , sender);
			snprintf(send_buff , sizeof(send_buff) , "[server]: %s\n" , recv_buff);
			/*send*/
			ret = send_to_bridge(bd , sender , send_buff , strlen(send_buff));
			if(ret < 0)
			{
				fprintf(stdout , "[server]send to bridge %d failed! ret:%d,send_buff:%s\n" , sender , ret , send_buff);
			}
			else
			{
				//fprintf(stdout , "send to bridge success! send_buff:%s\n" , send_buff);
			}

		}
	}

	return 0;
}

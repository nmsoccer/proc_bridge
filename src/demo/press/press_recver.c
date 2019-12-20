/*
 * press_recver.c
 *
 *  Created on: 2019年11月28日
 *      Author: soullei
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <proc_bridge/proc_bridge.h>
#include <slog/slog.h>

extern int errno;

#define MAX_MEMORY_CALLOC (1024*1024)
static int bd = -1;
static int my_proc = 31000;
static char *name_space = "press";
static char output_file[128] = {0};


static int show_help(void)
{
	printf("-f input file name\n");
	printf("-P pkg size as valid\n");
	printf("-S send file size\n");
	return 0;
}

int main(int argc , char **argv)
{
	int opt = -1;
	int pkg_size = 0;
	int fd = -1;
	int ret = -1;
	char *file_buff = NULL;

	int recv_index = 0;
	int sleep_times = 0;
	int recv_times = 0;
	int need_recv = 0;
	int file_size = 0;
	int buf_size = 0;
	struct timeval tv;
	long start_ts = 0;
	long end_ts = 0;

	int sld = -1;
	char *slog_name = "recv.log";
	SLOG_OPTION log_option;
	//char log_buff[1024] = {0}; //only for text-content

	printf("press recver starts...\n");
	if(argc <= 0)
	{
		printf("argc <=0 \n");
		return 0;
	}

	//get opt
	while((opt = getopt(argc , argv , "P:f:S:")) != -1)
	{
		switch(opt)
		{
		case 'P':
			pkg_size = atoi(optarg);
		break;
		case 'f':
			strncpy(output_file , optarg , sizeof(output_file));
		break;
		case 'S':
			file_size = atoi(optarg);
		break;
		default:
			show_help();
		break;
		}
	}

	if(strlen(output_file)<=0 || pkg_size<=0 || file_size<=0)
	{
		printf("arg illegal!\n");
		show_help();
		return 0;
	}
	printf("try to test press using:%s and pkg-size:%d\n" , output_file , pkg_size);
	slog_log(sld , SL_DEBUG , "try to test press using:%s and pkg-size:%d" , output_file , pkg_size);

	//open sld
	memset(&log_option , 0 , sizeof(log_option));
	log_option.log_size = 50*1024*1024;
	log_option.log_degree = SLD_MILL;
	strncpy(log_option.type_value._local.log_name , slog_name , sizeof(log_option.type_value._local.log_name));
	sld = slog_open(SLT_LOCAL , SL_DEBUG , &log_option , NULL);
	if(sld < 0)
	{
		printf("open %s failed!\n" , slog_name);
		return 0;
	}

	//open file
	//为了避免文件IO造成的损失 先存内存再写
	fd = open(output_file , O_WRONLY|O_TRUNC , 0);
	if(fd < 0)
	{
		printf("open %s failed! err:%s\n" , output_file , strerror(errno));
		return -1;
	}


	//calloc memory
	buf_size = file_size + 1024;
	file_buff = (char *)calloc(1 , buf_size);
	if(!file_buff)
	{
		printf("calloc memory failed!\n");
		return -1;
	}

	/***BRIDGE*/
	bd = open_bridge(name_space , my_proc , sld);
	if(bd<0)
	{
		printf("open bridge failed!\n");
		return -1;
	}

	//recv
	while(1)
	{
		if(recv_index >= file_size)
		{
			printf("read complete!\n");
			break;
		}

		ret = recv_from_bridge(bd , &file_buff[recv_index] , buf_size-recv_index , NULL , -1);
		if(ret<0 && recv_index==0)	//not started
		{
			usleep(100000);
			continue;
		}

		if(ret < 0)
		{
			if(ret == -2)
			{
				usleep(1000);
				sleep_times++;
				continue;
			}

			if(ret == -3)
			{
				printf("recv failed! index:%d\n" , recv_index);
				return 0;
			}
		}

		if(recv_index == 0)
		{
			gettimeofday(&tv , NULL);
			printf("started:%lu:%lu\n" , tv.tv_sec , tv.tv_usec);
			slog_log(sld , SL_DEBUG , "started:%lu:%lu" , tv.tv_sec , tv.tv_usec);
			start_ts = tv.tv_sec*100 + tv.tv_usec/10000;
		}
		//recv
		//write(fd , &file_buff[recv_index] , ret);

		//log
		//memcpy(log_buff , &file_buff[recv_index] , ret);
		//log_buff[ret] = 0;

		//upt
		recv_index += ret;
		if(ret != pkg_size)
		{
			printf("not recv pkg_size! index:%d ret:%d\n" , recv_index , ret);
		}
		recv_times++;
		//printf("[%d] %d\n" , recv_times , recv_index);
		if(recv_times % 200 == 0)
			write(1 , "." , 1);
		//slog_log(sld , SL_DEBUG , "[%d]<%d>(%d)" , recv_times , recv_index , ret);
	}

	//result
	gettimeofday(&tv , NULL);
	printf("ended:%lu:%lu\n" , tv.tv_sec , tv.tv_usec);
	printf("recv_times:%d sleep_times:%d\n" , recv_times , sleep_times);
	slog_log(sld , SL_DEBUG , "ended:%lu:%lu recv_times:%d sleep_times:%d" , tv.tv_sec , tv.tv_usec , recv_times , sleep_times);
	end_ts = tv.tv_sec*100 + tv.tv_usec/10000;
	if(end_ts != start_ts)
	{
		printf("cost:%ld ms qps:%ld per 10ms\n" , (end_ts-start_ts)*10 , recv_times/(end_ts-start_ts));
		slog_log(sld , SL_DEBUG , "cost:%ld ms qps:%ld per 10ms" , (end_ts-start_ts)*10 , recv_times/(end_ts-start_ts));
	}
	fflush(stdout);

	write(fd , file_buff , file_size);
	free(file_buff);
	close(fd);
	return 0;
}

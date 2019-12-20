/*
 * press_sender.c
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
static int bd = -1;
static int my_proc = 21000;
static int recv_proc = 31000;
static char *name_space = "press";
static char input_file[128] = {0};

static int show_help(void)
{
	printf("-f input file name\n");
	printf("-P pkg size\n");
	return 0;
}



int main(int argc , char **argv)
{
	int opt = -1;
	int pkg_size = 0;
	int fd = -1;
	struct stat file_stat;
	int ret = -1;
	char *file_buff = NULL;
	int send_index = 0;

	int sleep_times = 0;
	int send_times = 0;
	int sleep_degree = 1;
	int need_send = 0;
	int file_size = 0;
	struct timeval tv;
	long start_ts = 0;
	long end_ts = 0;

	int sld = -1;
	char *slog_name = "send.log";
	SLOG_OPTION log_option;
	//char log_buff[1024] = {0}; //only for text-content

	printf("press sender starts...\n");
	if(argc <= 0)
	{
		printf("argc <=0 \n");
		return 0;
	}

	//get opt
	while((opt = getopt(argc , argv , "P:f:")) != -1)
	{
		switch(opt)
		{
		case 'P':
			pkg_size = atoi(optarg);
		break;
		case 'f':
			strncpy(input_file , optarg , sizeof(input_file));
		break;
		default:
			show_help();
		break;
		}
	}

	if(strlen(input_file)<=0 || pkg_size<=0)
	{
		printf("arg illegal!\n");
		show_help();
		return 0;
	}
	printf("try to test press using:%s and pkg-size:%d\n" , input_file , pkg_size);

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
	//为了避免文件IO造成的损失 先把文件撸进内存
	fd = open(input_file , O_RDONLY , 0);
	if(fd < 0)
	{
		printf("open %s failed! err:%s\n" , input_file , strerror(errno));
		return -1;
	}

	//stat file
	ret = fstat(fd , &file_stat);
	if(ret < 0)
	{
		printf("stat %s failed! err:%s\n" , input_file , strerror(errno));
		return -1;
	}
	file_size = file_stat.st_size;
	printf("%s size:%d\n" , input_file , file_size);

	//read file
	file_buff = (char *)calloc(1 , file_stat.st_size+4);
	if(!file_buff)
	{
		printf("calloc memory failed!\n");
		return -1;
	}

	ret = read(fd , file_buff , file_size);
	if(ret != file_stat.st_size)
	{
		printf("read filed %d:%d not enough!\n" , ret , file_size);
		return -1;
	}
	printf("read %s success! ret:%d\n" , input_file , ret);

	/***BRIDGE*/
	bd = open_bridge(name_space , my_proc , sld);
	if(bd<0)
	{
		printf("open bridge failed!\n");
		return -1;
	}

	//send
	gettimeofday(&tv , NULL);
	printf("started:%lu:%lu\n" , tv.tv_sec , tv.tv_usec);
	slog_log(sld , SL_DEBUG , "started:%lu:%lu" , tv.tv_sec , tv.tv_usec);
	start_ts = tv.tv_sec*100 + tv.tv_usec/10000;
	while(1)
	{
		if(send_index >= file_size)
		{
			printf("totally sended!\n");
			break;
		}
		need_send = (file_size-send_index)>=pkg_size?pkg_size:(file_size-send_index);
		ret = send_to_bridge(bd , recv_proc , &file_buff[send_index] , need_send);

		if(ret != 0)
		{
			slog_log(sld , SL_DEBUG , "send error:%d" , ret);
			sleep_times++;
			usleep(sleep_degree * 10000);	//sleep sleep_degree * 10ms 防止发送过快导致缓存不足从而丢包
			sleep_degree++;
			slog_log(sld , SL_DEBUG , "sleep_degree:%d\n" , sleep_degree);
			continue;
		}

		//log
		//memcpy(log_buff , &file_buff[send_index] , need_send);
		//log_buff[need_send] = 0;

		//upt
		sleep_degree /= 2;
		sleep_degree = (sleep_degree<=0)?1:sleep_degree;
		send_times++;
		send_index += need_send;
		//slog_log(sld , SL_DEBUG , "[%d]<%d>(%d)%s" , send_times , send_index , need_send , log_buff);
	}

	//result
	gettimeofday(&tv , NULL);
	printf("ended:%lu:%lu\n" , tv.tv_sec , tv.tv_usec);
	printf("send_times:%d sleep_times:%d\n" , send_times , sleep_times);
	slog_log(sld , SL_DEBUG , "ended:%lu:%lu send_times:%d sleep_times:%d" , tv.tv_sec , tv.tv_usec , send_times , sleep_times);
	end_ts = tv.tv_sec*100 + tv.tv_usec/10000;
	if(end_ts != start_ts)
	{
		printf("cost:%ldms qps:%ld per 10ms\n" , (end_ts-start_ts)*10 , send_times/(end_ts-start_ts));
		slog_log(sld  ,SL_DEBUG , "cost:%ldms qps:%ld per 10ms" , (end_ts-start_ts)*10 , send_times/(end_ts-start_ts));
	}

	free(file_buff);
	close(fd);
	return 0;
}

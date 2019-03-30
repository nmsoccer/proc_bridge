/*
 * carrier_base.c
 *
 *  Created on: 2019年3月25日
 *      Author: nmsoccer
 */
#include <slog/slog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "proc_bridge.h"
#include "carrier_base.h"

extern int errno;
/*
 * 添加一个ticker
 * -1:failed 0:success
 */
int append_carrier_ticker(carrier_env_t *penv , CARRIER_TICK func_tick , char type , long long tick_period ,
		char *ticker_name , void *arg)
{
	int slogd = -1;
	time_ticker_t *pticker = NULL;
	long long expire_ms = 0;
	if(!penv || !func_tick)
		return -1;
	slogd = penv->slogd;

	/***Alloc*/
	pticker = calloc(1 , sizeof(time_ticker_t));
	if(!pticker)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! alloc ticker failed! err:%s" , __FUNCTION__ , strerror(errno));
		return -1;
	}
	expire_ms = get_curr_ms() + tick_period;

	pticker->type = type;
	pticker->tick_period = tick_period;
	pticker->expire_ms = expire_ms;
	if(ticker_name)
		strncpy(pticker->ticker_name , ticker_name , sizeof(pticker->ticker_name));
	pticker->func = func_tick;
	pticker->arg = arg;

	/***Append*/
	pticker->next = penv->tick_list.head.next;
	penv->tick_list.head.next = pticker;
	pticker->prev = &penv->tick_list.head;
	if(pticker->next)
		pticker->next->prev = pticker;

	if(penv->tick_list.latest_expire_ms == 0)
		penv->tick_list.latest_expire_ms = expire_ms;
	else
		penv->tick_list.latest_expire_ms = (expire_ms<penv->tick_list.latest_expire_ms)?expire_ms:penv->tick_list.latest_expire_ms;

	penv->tick_list.count++;
	slog_log(slogd , SL_INFO , "<%s> success! count:%d ticker:%s type:%s period:%lld trig:%lld latest_expire_ms:%lld" , __FUNCTION__ , penv->tick_list.count ,
			pticker->ticker_name , type==1?"singe":"circle",tick_period , expire_ms , penv->tick_list.latest_expire_ms);
	return 0;
}

int iter_time_ticker(carrier_env_t *penv)
{
	tick_list_t *ptick_list = NULL;
	time_ticker_t *pticker = NULL;
	time_ticker_t *ptmp = NULL;
	long long curr_ms = get_curr_ms();
	long long new_latest_expire = 0;
	int slogd = -1;

	slogd = penv->slogd;
	ptick_list = &penv->tick_list;

	/***Expired*/
	if(ptick_list->count<=0 || ptick_list->latest_expire_ms > curr_ms)
		return 0;

	/***EXE TICKER*/
	new_latest_expire = 0x7FFFFFFFFFFLL;
	pticker = ptick_list->head.next;
	while(pticker)
	{
		//expired
		if(pticker->expire_ms <= curr_ms)
		{
			//exe ticker
			pticker->func(pticker->arg);

			//SINGLE-SHOT
			if(pticker->type == TIME_TICKER_T_SINGLE_SHOT)
			{
				//try to destroy
				ptmp = pticker->next;
				pticker->prev->next = ptmp;
				if(ptmp)
					ptmp->prev = pticker->prev;
				ptick_list->count--;

				slog_log(slogd , SL_INFO , "<%s> destroy ticker:%s type:%d count:%d" , __FUNCTION__ , pticker->ticker_name , pticker->type ,
						ptick_list->count);
				free(pticker);
				pticker = ptmp;
				continue;
			}
			else	//CIRCLE
			{
				pticker->expire_ms = curr_ms + pticker->tick_period;
				if(pticker->expire_ms < new_latest_expire)
					new_latest_expire = pticker->expire_ms;

				slog_log(slogd , SL_DEBUG , "<%s> expired tricker %s resets to %lld and new_latest_expire:%lld" , __FUNCTION__ , pticker->ticker_name ,
						pticker->expire_ms , new_latest_expire);
			}

		}
		else	//no expire
		{
			if(pticker->expire_ms < new_latest_expire)
				new_latest_expire = pticker->expire_ms;

			slog_log(slogd , SL_VERBOSE , "<%s> no expire ticker:%s expired at %lld and  new_latest_expire:%lld" , __FUNCTION__ ,
					pticker->ticker_name , pticker->expire_ms , new_latest_expire);
		}

		pticker = pticker->next;
	}

	ptick_list->latest_expire_ms = new_latest_expire;
	slog_log(slogd , SL_VERBOSE , "<%s> finish! ticker count:%d new_latest:%lld" , __FUNCTION__ , ptick_list->count , ptick_list->latest_expire_ms);
	return (get_curr_ms() - curr_ms);
}


//get curr ms
long long get_curr_ms()
{
	int ret = -1;
	struct timeval tv;
	ret = gettimeofday(&tv , NULL);
	if(ret < 0)
		return -1;
	return ((long long)tv.tv_sec*1000)+tv.tv_usec/1000;
}

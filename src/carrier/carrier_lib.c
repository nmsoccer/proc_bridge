/*
 * carrier_lib.c
 *
 *  Created on: 2019年2月21日
 *      Author: nmsoccer
 */
#include <sys/socket.h>
#include <stlv/stlv.h>
#include <slog/slog.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "carrier_lib.h"
#include "manager_lib.h"

extern int errno;
static int send_msg_event(carrier_env_t *penv , int type , void *arg1 , void *arg2);
static int send_msg_error(carrier_env_t *penv , int type ,  void *arg1 , void *arg2);
static int filt_manager_proc_id(target_info_t *ptarget_info , target_detail_t *manager[] , int len);
static int inner_send_pkg(target_detail_t *ptarget , bridge_package_t *ppkg , int pkg_len , int slogd);
static int handle_msg_event(manager_info_t *pmanage , int from , carrier_msg_t *pmsg);
static int handle_msg_error(manager_info_t *pmanage , int from , carrier_msg_t *pmsg);
static int do_manage_cmd_stat(carrier_env_t *penv , manager_cmd_req_t *preq);
static int do_manage_cmd_error(carrier_env_t *penv , manager_cmd_req_t *preq);
static int do_manage_cmd_proto(carrier_env_t *penv , manager_cmd_req_t *preq);

/*
 * append_recv_channel
 * 添加一个package到recv_channel里
 * @phub:该进程打开的bridge
 * @buff
 * @return:
 * -1：错误
 * -2：接收缓冲区满
 * 0:success
 */
int append_recv_channel(bridge_hub_t *phub , char *pstpack)
{
	char *recv_channel = NULL;
	int empty_space = 0;
	int send_count;
	int copyed = 0;
	int tail_pos;
	int channel_len;

	/***Arg Check*/
	if(!phub ||!pstpack)
	{
		return -1;
	}

	//1.检查发送区是否满
	/*
	if(phub->recv_full == 1)
	{
		return -2;
	}*/
	recv_channel = GET_RECV_CHANNEL(phub);

	//2.检查空闲空间
	channel_len = phub->recv_buff_size;
	send_count = sizeof(bridge_package_head_t) + ((bridge_package_t *)pstpack)->pack_head.data_len;

	/*tail 在head之后，则考察tail<->end + start<->head的长度*/
	if(phub->recv_tail >= phub->recv_head)
	{
		//检查空闲空间
		empty_space = channel_len - phub->recv_tail + phub->recv_head;
	}
	else	/*tail在head之前，则考察tail<->head*/
	{
		empty_space = phub->recv_head - phub->recv_tail;
	}
	if(empty_space -1 < send_count)	////预留1B 防止head==tail&&equeue == full
		return -3;

	//5.从buff拷贝
	tail_pos = phub->recv_tail;
	/*tail 在head之后，则考察tail<->end + start<->head的长度*/
	if(tail_pos >= phub->recv_head)
	{
		//最后剩余空间足够
		if((channel_len - tail_pos) >= send_count)
		{
			memcpy(&recv_channel[tail_pos] , pstpack , send_count);
			tail_pos += send_count;
			tail_pos %= channel_len;
		}
		else//最后剩余空间不够
		{
			copyed = channel_len - tail_pos;
			memcpy(&recv_channel[tail_pos] , pstpack , copyed);
			memcpy(&recv_channel[0] , &pstpack[copyed] , send_count - copyed);
			tail_pos = 0 + send_count - copyed;
		}

	}
	else	/*tail在head之前，则考察tail<->head*/
	{
		memcpy(&recv_channel[tail_pos] , pstpack , send_count);
		tail_pos += send_count;
	}


	//6.在写完内存之后再修改tail指针，因为read会比较tail指针位置。否则会出现同步错误
	/*这样写会有同步错误
	phub->recv_tail = tail_pos;
	if(phub->recv_head == phub->recv_tail)
		phub->recv_full = 1;
	*/
	/*bugfix2 取消标记
	if(phub->recv_head == tail_pos)
		phub->recv_full = 1;
	*/
	phub->recv_tail = tail_pos;

	phub->recving_count++;

	phub->recved_count++;
	if(phub->recved_count >= 0xFFFFF000)
			phub->recved_count = 0;

	return 0;

}

/*
 * 将proc_info的内容解析到proc_entry里
 * @proc_info:[@proc_name&proc_id&ip_addr&port]
 * @slogd:slog descriptor
 * @return:
 * -1:failed; 0:success
 */
int parse_proc_info(char *proc_info , proc_entry_t *pentry , int slogd)
{
	char *item_start;
	char *seg_start;
	char *seg_pos;

	/***Arg Check*/
	if(!proc_info || strlen(proc_info)<=2)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! proc_info null!" , __FUNCTION__);
		return -1;
	}
	if(!pentry)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! entry null!" , __FUNCTION__);
		return -1;
	}

	if(proc_info[0] != '@')	//必须以@开头
	{
		slog_log(slogd , SL_ERR , "<%s> failed! illegal proc_info:%s without '@' at first!" , __FUNCTION__ , proc_info);
		return -1;
	}

	slog_log(slogd , SL_DEBUG , "<%s> parse:%s" , __FUNCTION__ , proc_info);
	/***handle each item*/
	item_start = &proc_info[1];

	seg_start = item_start; /*取proc_name*/
	seg_pos = strchr(seg_start , '&');
	if(seg_pos == NULL)
	{
		slog_log(slogd , SL_ERR , "<%s> Error:Can not get proc_name from proc_info:%s" , __FUNCTION__ , proc_info);
		return -1;
	}
	seg_pos[0] = 0;
	strncpy(pentry->name , seg_start , sizeof(pentry->name));

	seg_pos++; /*取proc_id*/
	seg_start = seg_pos;
	seg_pos = strchr(seg_start , '&');
	if(seg_pos == NULL)
	{
		slog_log(slogd , SL_ERR , "<%s> Error:Can not get proc_id from proc_info:%s" , __FUNCTION__ , proc_info);
		return -1;
	}
	seg_pos[0] = 0;
	pentry->proc_id = atoi(seg_start);


	seg_pos++; /*取ip_addr*/
	seg_start = seg_pos;
	seg_pos = strchr(seg_start , '&');
	if(seg_pos == NULL)
	{
		slog_log(slogd , SL_ERR , "<%s> Error:Can not get ip_addr from proc_info:%s" , __FUNCTION__ , proc_info);
		return -1;
	}
	seg_pos[0] = 0;
	strncpy(pentry->ip_addr , seg_start , sizeof(pentry->ip_addr));


	seg_pos++; /*取port*/
	seg_start = seg_pos;
	pentry->port = atoi(seg_start);

	slog_log(slogd , SL_DEBUG , "<%s> success! name:%s id:%d ip:%s port:%d" , __FUNCTION__ , pentry->name , pentry->proc_id ,
			pentry->ip_addr , pentry->port);
	return 0;
}

/*
 * 清空某个channel的target发送缓冲区
 * -1:错误
 *  0:成功
 */
int flush_target(target_detail_t *ptarget , int slogd)
{
	int ret = 0;
	int result = 0;
	//ptarget is non-null

	slog_log(slogd , SL_VERBOSE , "%s is sending package to %d. rest_count:%d latest_ts:%ld" , __FUNCTION__ , ptarget->proc_id ,
						ptarget->ready_count , ptarget->latest_ts);

	//send
	ret = send(ptarget->fd ,  ptarget->buff , ptarget->tail , 0);

	//send failed
	if(ret < 0)
	{
		switch(errno)
		{
		case EAGAIN:	//socket发送缓冲区满，稍后再试
		//case EWOULDBLOCK:
			slog_log(slogd , SL_INFO , "%s send failed for socket buff full!" , __FUNCTION__);
		break;
		default:
			slog_log(slogd , SL_ERR , "%s send failed for err:%s. and will reset connection." , __FUNCTION__ , strerror(errno));
			//此时出现错误，应主动关闭链接，用于清除本端和对端的缓冲区数据，防止错误包雪崩。并在后续进行重连
			close(ptarget->fd);
			ptarget->fd = -1;
			ptarget->connected = 0;
			ptarget->buff = ptarget->main_buff;
			ptarget->tail = 0;
			result = -1;
		break;
		}

		return result;
	}

	//send all of data
	if(ret >= ptarget->tail)
	{
		slog_log(slogd , SL_VERBOSE , "%s flush all buff success!" , __FUNCTION__ );
		ptarget->tail = 0;
		ptarget->latest_ts = 0;
		ptarget->ready_count = 0;
		return 0;
	}

	//send part of data
	slog_log(slogd , SL_VERBOSE , "%s flush part of buff! sended:%d all:%d" , __FUNCTION__ , ret , ptarget->tail);
	if(ptarget->buff == ptarget->main_buff)
	{
		memcpy(ptarget->back_buff , &ptarget->buff[ret] , ptarget->tail-ret);
		ptarget->buff = ptarget->back_buff;
	}
	else
	{
		memcpy(ptarget->main_buff , &ptarget->buff[ret] , ptarget->tail-ret);
		ptarget->buff = ptarget->main_buff;
	}
	ptarget->tail = ptarget->tail - ret;
	return 0;
}

/*
 *向manager发送carrier消息
 *@msg:refer CR_MSG_xx
 *@type:refer MSG_xx_xx
 *@arg1-n:other info
 *@return -1:failed 0:success
 */
int send_carrier_msg(carrier_env_t *penv , int msg , int type , void *arg1 , void *arg2)
{
	int ret = -1;
	int slogd = -1;

	/***Arg Check*/
	if(!penv)
		return -1;
	if(msg < CR_MSG_MIN || msg > CR_MSG_MAX)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! msg:%d illegal!" , __FUNCTION__ , msg);
		return -1;
	}
	slogd = penv->slogd;

	switch(msg)
	{
	case CR_MSG_EVENT:
		ret = send_msg_event(penv , type , arg1 , arg2);
		break;
	case CR_MSG_ERROR:
		ret = send_msg_error(penv , type , arg1 , arg2);
		break;
	default:
		slog_log(slogd , SL_ERR , "<%s> failed! msg:%d illegal!" , __FUNCTION__ , msg);
		return -1;
	}

	return ret;
}

/*
 * 根据proc_id获取对应的target
 */
target_detail_t *proc_id2_target(target_info_t *ptarget_info , int proc_id)
{
	target_detail_t *ptarget = NULL;

	/***Arg Check*/
	if(!ptarget_info || proc_id < 0)
		return NULL;

	/***Search*/
	ptarget = ptarget_info->head.next;
	while(ptarget)
	{
		if(ptarget->proc_id == proc_id)
			break;

		ptarget = ptarget->next;
	}

	return ptarget;
}

/*
 *carrier间的内部通信协议
 *@proto:refer INNER_PROTO_xx
 *@arg1-n:other info
 *@return -1:failed 0:success
 */
int send_inner_proto(carrier_env_t *penv , target_detail_t *ptarget , int proto ,  void *arg1 , void*arg2)
{
	bridge_package_t *ppkg;
	char pack_buff[GET_PACK_LEN(sizeof(inner_proto_t))] = {0};
	inner_proto_t *preq;
	int slogd = -1;
	int ret = -1;

	/***Arg Check*/
	if(!penv || !ptarget)
		return -1;

	/***Init*/
	ppkg = (bridge_package_t *)pack_buff;
	preq = (inner_proto_t *)ppkg->pack_data;
	slogd = penv->slogd;

	/***Fill Req*/
	preq->type = proto;

	switch(proto)
	{
	case INNER_PROTO_PING:
		break;
	case INNER_PROTO_PONG:
		strncpy(preq->data.proc_name , (char *)arg1 , sizeof(preq->data.proc_name));
		break;
	case INNER_PROTO_VERIFY_REQ:
		strncpy(preq->data.verify_key , (char *)arg1 , sizeof(preq->data.verify_key));
		break;
	case INNER_PROTO_CONN_TRAFFIC_REQ:
		strncpy(preq->arg , (char *)arg1 , sizeof(preq->arg));
		break;
	case INNER_PROTO_CONN_TRAFFIC_RSP:
		strncpy(preq->arg , (char *)arg1 , sizeof(preq->arg));
		memcpy(&preq->data.traffic_list , (traffic_list_t *)arg2 , sizeof(traffic_list_t));
		break;
	default:
		slog_log(slogd , SL_ERR , "<%s> failed! proto:%d illegal!" , __FUNCTION__ , proto);
		return -1;
	}

	/***Fill Pkg*/
	ppkg->pack_head.data_len = sizeof(inner_proto_t);
	ppkg->pack_head.recver_id = ptarget->proc_id;
	ppkg->pack_head.sender_id = penv->proc_id;
	ppkg->pack_head.send_ts = time(NULL);
	ppkg->pack_head.pkg_type = BRIDGE_PKG_TYPE_INNER_PROTO;

	/***Send*/
	ret = inner_send_pkg(ptarget , ppkg , sizeof(pack_buff) , slogd);
	if(ret < 0)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! proto:%d ret:%d" , __FUNCTION__ , proto , ret);
		return -1;
	}
	else
		slog_log(slogd , SL_DEBUG, "<%s> success! proto:%d ret:%d" , __FUNCTION__ , proto , ret);

	return 0;
}


/*
 * 接收inner_proto并处理之
 */
int recv_inner_proto(carrier_env_t *penv , client_info_t *pclient , char *package)
{
	traffic_list_t traffic_list;

	char arg[MANAGER_CMD_ARG_LEN] = {0};
	char *ptmp = NULL;
	bridge_package_t *ppkg = NULL;
	inner_proto_t *preq = NULL;
	target_detail_t *ptarget = NULL;
	target_detail_t *ptarget_from = NULL;

	char bridge_pack_buff[GET_PACK_LEN(sizeof(manager_cmd_rsp_t))] = {0};
	bridge_package_t *pbridge_pkg;
	manager_cmd_rsp_t *prsp;
	cmd_proto_rsp_t *pproto_rsp;
	int slogd = -1;
	int ret = -1;
	char select_all = 0;
	char found = 0;
	int i = 0;

	/***Arg Check*/
	if(!penv || !package)
		return -1;

	/***Init*/
	ppkg = (bridge_package_t *)package;
	preq = (inner_proto_t *)ppkg->pack_data;
	slogd = penv->slogd;

	pbridge_pkg = (bridge_package_t *)bridge_pack_buff;
	pbridge_pkg->pack_head.data_len = sizeof(manager_cmd_rsp_t);
	pbridge_pkg->pack_head.send_ts = time(NULL);
	pbridge_pkg->pack_head.sender_id = penv->proc_id;
	pbridge_pkg->pack_head.recver_id = penv->proc_id;

	prsp = (manager_cmd_rsp_t *)pbridge_pkg->pack_data;
	if(penv->pmanager)
		prsp->manage_stat = penv->pmanager->stat;
	prsp->type = MANAGER_CMD_PROTO;
	pproto_rsp = &prsp->data.proto;

	/***Handle*/
	slog_log(slogd , SL_INFO , "<%s> proto:%d src:%d ts:%ld" , __FUNCTION__  , preq->type , ppkg->pack_head.sender_id ,
			ppkg->pack_head.send_ts);

	switch(preq->type)
	{
	case INNER_PROTO_PING:
		if(!pclient->verify)
			break;
		ptarget_from = proc_id2_target(penv->ptarget_info , pclient->proc_id);	//get target
		if(!ptarget_from || ptarget_from->connected!=TARGET_CONN_DONE)
		{
			slog_log(slogd , SL_ERR , "<%s> Connection from Ping:%d may not ready!" , __FUNCTION__ , pclient->proc_id);
			break;
		}

		//send back
		send_inner_proto(penv , ptarget_from , INNER_PROTO_PONG , penv->proc_name , NULL);
	break;
	case INNER_PROTO_PONG:
		if(!pclient->verify)
			break;
		slog_log(slogd  , SL_DEBUG , "<%s> pong from %d:%s" , __FUNCTION__ , pclient->proc_id , preq->data.proc_name);
		if(penv->proc_id > MANAGER_PROC_ID_MAX)	//非manager不向上层传递
			break;

		if(penv->phub->attached < 2)	//uuper closed
			break;

		pproto_rsp->type = CMD_PROTO_T_PING;
		pproto_rsp->result = 0;
		strncpy(pproto_rsp->arg , preq->data.proc_name , sizeof(pproto_rsp->arg));
			//send to manager
		ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
		slog_log(slogd , SL_INFO , "<%s> append recv channel ret:%d result:%d" , __FUNCTION__ , ret , pproto_rsp->result);
		break;

	case INNER_PROTO_VERIFY_REQ:
		slog_log(slogd , SL_INFO , "<%s> recv verfiy key:%s from  %d<%s:%d>" , __FUNCTION__ , preq->data.verify_key , pclient->fd ,
				pclient->client_ip , pclient->client_port);
		ret = do_verify_key(penv , preq->data.verify_key , sizeof(preq->data.verify_key));
		if(ret == 0)
		{
			pclient->verify = 1;
			pclient->proc_id = ppkg->pack_head.sender_id;
			slog_log(slogd , SL_INFO , "<%s> verify success! client proc_id:%d located %d<%s:%d>" , __FUNCTION__ , pclient->proc_id ,
					pclient->fd ,	pclient->client_ip , pclient->client_port);
		}
		else
			slog_log(slogd , SL_ERR , "<%s> verify failed! " , __FUNCTION__);
	break;

	case INNER_PROTO_CONN_TRAFFIC_REQ:
		if(!pclient->verify)
					break;
		//from
		ptarget_from = proc_id2_target(penv->ptarget_info , pclient->proc_id);	//get target
		if(!ptarget_from || ptarget_from->connected!=TARGET_CONN_DONE)
		{
			slog_log(slogd , SL_ERR , "<%s> Connection from GET-TRAFFIC:%d may not ready!" , __FUNCTION__ , pclient->proc_id);
			break;
		}

		//arg 后缀匹配
		strncpy(arg , preq->arg , sizeof(arg));
		if(strcmp(arg , "*") == 0)
			select_all = 1;
		else
		{
			ptmp = strchr(arg , '*');
			if(ptmp)
				ptmp[0] = 0;
		}

		//select
		memset(&traffic_list , 0 , sizeof(traffic_list));
		strncpy(traffic_list.owner , penv->proc_name , sizeof(traffic_list.owner));
		ptarget = penv->ptarget_info->head.next;
		while(ptarget)
		{
			//match
			if(select_all || strncmp(arg , ptarget->target_name , strlen(arg))==0)
			{
				found = 1;
				strncpy(traffic_list.names[traffic_list.count] , ptarget->target_name , PROC_ENTRY_NAME_LEN);
				memcpy(&traffic_list.lists[traffic_list.count] , &ptarget->traffic , sizeof(conn_traffic_t));
				traffic_list.count++;

				//rotate
				if(traffic_list.count >= MAX_CONN_TRAFFIC_PER_PKG)
				{
					ret = send_inner_proto(penv , ptarget_from , INNER_PROTO_CONN_TRAFFIC_RSP , preq->arg , (char *)&traffic_list);
					if(ret < 0)
						slog_log(slogd , SL_ERR , "<%s> send Refreshing INNER_PROTO_CONN_TRAFFIC_RSP from %d failed!" , __FUNCTION__ , pclient->proc_id);
					//refresh
					traffic_list.count = 0;
					strncpy(traffic_list.owner , penv->proc_name , sizeof(traffic_list.owner));
				}
			}

			ptarget = ptarget->next;
		}
		//final pkg
		if(found==0 || traffic_list.count>0)
		{
			ret = send_inner_proto(penv , ptarget_from , INNER_PROTO_CONN_TRAFFIC_RSP , preq->arg , (char *)&traffic_list);
			if(ret < 0)
				slog_log(slogd , SL_ERR , "<%s> send Final INNER_PROTO_CONN_TRAFFIC_RSP from %d failed!" , __FUNCTION__ , pclient->proc_id);
		}
	break;

	case INNER_PROTO_CONN_TRAFFIC_RSP:
		if(!pclient->verify)
			break;
		slog_log(slogd  , SL_DEBUG , "<%s> rsp from %d. arg is:%s count:%d" , __FUNCTION__ , pclient->proc_id , preq->arg , preq->data.traffic_list.count);
		if(penv->proc_id > MANAGER_PROC_ID_MAX)	//非manager不向上层传递
			break;

		if(penv->phub->attached < 2)	//uuper closed
			break;

		pproto_rsp->type = CMD_PROTO_T_TRAFFIC;
		pproto_rsp->result = 0;
		snprintf(pproto_rsp->arg , sizeof(pproto_rsp->arg) , "%s %s" , preq->data.traffic_list.owner , preq->arg);
		memcpy(&pproto_rsp->traffic_list , &preq->data.traffic_list , sizeof(traffic_list));
			//send to manager
		ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
		slog_log(slogd , SL_INFO , "<%s> append recv channel ret:%d result:%d" , __FUNCTION__ , ret , pproto_rsp->result);

	break;

	default:
		slog_log(slogd , SL_ERR , "<%s> illegal proto:%d" , __FUNCTION__ , preq->type);
	break;
	}

	return 0;
}

/*
 * 控制进程专属的处理逻辑
 *
 */
int manager_handle(manager_info_t *pmanager , char *package , int slogd)
{
	bridge_package_t *pkg = NULL;
	carrier_msg_t *pmsg = NULL;
	int ret = -1;

	/***Arg Check*/
	if(!pmanager || !package)
		return -1;

	/***Parse Info*/
	pkg = (bridge_package_t *)package;
	pmsg = (carrier_msg_t *)pkg->pack_data;
	slog_log(slogd , SL_INFO , "<%s> recv msg. from:%d msg:%d ts:%ld" , __FUNCTION__ , pkg->pack_head.sender_id , pmsg->msg ,
			pkg->pack_head.send_ts);

	/***Switch*/
	switch(pmsg->msg)
	{
	case CR_MSG_EVENT:
		ret = handle_msg_event(pmanager , pkg->pack_head.sender_id , pmsg);
		break;
	break;
	case CR_MSG_ERROR:
		ret = handle_msg_error(pmanager , pkg->pack_head.sender_id , pmsg);
		break;
	default:
		slog_log(slogd , SL_ERR , "<%s> recv illegal msg:%d" , __FUNCTION__ , pmsg->msg);
		return -1;
	}

	return ret;
}

/*
 * 初始化manager的管理列表
 * @return:-1 failed 0:success
 */
int init_manager_item_list(carrier_env_t *penv)
{
	int slogd = -1;
	manager_info_t *pmanager = NULL;
	target_info_t *ptarget_info = NULL;
	target_detail_t *ptarget = NULL;
	manage_item_t *pitem = NULL;
	int i = 0;
	long curr_ts = time(NULL);

	/***Arg Check*/
	if(!penv)
		return -1;
	slogd = penv->slogd;
	pmanager = penv->pmanager;
	ptarget_info = penv->ptarget_info;
	if(!pmanager)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! manager NULL!" , __FUNCTION__);
		return -1;
	}
	pmanager->stat = MANAGE_STAT_BAD;
	if(!ptarget_info)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! target_info NULL!" , __FUNCTION__);
		return -1;
	}

	if(ptarget_info->target_count <= 0)
	{
		slog_log(slogd , SL_INFO , "<%s> finish! target zero!" , __FUNCTION__);
		pmanager->stat = MANAGE_STAT_OK;
		return 0;
	}

	/***Calc Item List*/
	pmanager->item_count = ptarget_info->target_count;
	pmanager->item_list = calloc(pmanager->item_count , sizeof(manage_item_t));
	if(!pmanager->item_list)
	{
		slog_log(slogd , SL_ERR , "<%s> alloc %d item failed! err:%s", __FUNCTION__ , pmanager->item_count , strerror(errno));
		pmanager->stat = MANAGE_STAT_BAD;
		return -1;
	}

	/***Init*/
	ptarget = ptarget_info->head.next;
	while(ptarget)
	{
		pitem = &pmanager->item_list[i++];
		//set info
		pitem->flag = MANAGE_ITEM_FLG_VALID;
		pitem->my_conn_stat = ptarget->connected;
		strncpy(pitem->proc.name , ptarget->target_name , sizeof(pitem->proc.name));
		strncpy(pitem->proc.ip_addr , ptarget->ip_addr , sizeof(pitem->proc.ip_addr));
		pitem->proc.proc_id = ptarget->proc_id;
		pitem->proc.port = ptarget->port;
		pitem->latest_update = curr_ts;
		//loop
		ptarget = ptarget->next;
	}

	pmanager->stat = MANAGE_STAT_OK;
	print_manage_info(penv);
	return 0;
}

/*
 * 重建manager的管理列表
 * 用于在动态加载配置文件之后
 */
int rebuild_manager_item_list(carrier_env_t *penv)
{
	int slogd = -1;
	manager_info_t *pmanager = NULL;
	target_info_t *ptarget_info = NULL;
	target_detail_t *ptarget = NULL;
	manage_item_t *pitem = NULL;

	manage_item_t *pnew_list = NULL;
	manage_item_t *pnew_item = NULL;
	int pos = 0;
	int new_count = 0;

	int i = 0;
	int copied = 0;
	long curr_ts = time(NULL);


	/***Arg Check*/
	if(!penv)
		return -1;
	if(penv->proc_id > MANAGER_PROC_ID_MAX)
		return 0;


	slogd = penv->slogd;
	pmanager = penv->pmanager;
	ptarget_info = penv->ptarget_info;
	if(!pmanager)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! manager NULL!" , __FUNCTION__);
		return -1;
	}
	pmanager->stat = MANAGE_STAT_BAD;
	if(!ptarget_info)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! target_info NULL!" , __FUNCTION__);
		return -1;
	}

	if(ptarget_info->target_count <= 0)
	{
		slog_log(slogd , SL_INFO , "<%s> finish! target zero!" , __FUNCTION__);
			//clear
		pmanager->stat = MANAGE_STAT_OK;
		if(pmanager->item_list)
			free(pmanager->item_list);
		pmanager->item_count = 0;
		pmanager->item_list = NULL;
		return 0;
	}

	/***Rebuild*/
	//1.alloc new list
	new_count = ptarget_info->target_count;
	pnew_list = calloc(new_count , sizeof(manage_item_t));
	if(!pnew_list)
	{
		slog_log(slogd , SL_ERR , "<%s> caloc new item_list failed!new count:%d" , __FUNCTION__ , new_count);
		pmanager->stat = MANAGE_STAT_BAD;
		return -1;
	}

	//2.put new taget in
	ptarget = ptarget_info->head.next;
	pos = 0;
	while(ptarget)
	{
		pnew_item = &pnew_list[pos];
		//set info
		pnew_item->flag = MANAGE_ITEM_FLG_VALID;
		pnew_item->my_conn_stat = ptarget->connected;
		strncpy(pnew_item->proc.name , ptarget->target_name , sizeof(pnew_item->proc.name));
		strncpy(pnew_item->proc.ip_addr , ptarget->ip_addr , sizeof(pnew_item->proc.ip_addr));
		pnew_item->proc.proc_id = ptarget->proc_id;
		pnew_item->proc.port = ptarget->port;
		pnew_item->latest_update = curr_ts;
		//try append
		for(i=0; i<pmanager->item_count; i++)
		{
			pitem = &pmanager->item_list[i];
			//只有proc_id相同且地址没有变动 则迁移旧数据
			if(pitem->proc.proc_id==pnew_item->proc.proc_id && pitem->proc.port==pnew_item->proc.port &&
				strncmp(pitem->proc.ip_addr,pnew_item->proc.ip_addr , PROC_ENTRY_IP_LEN)==0)
			{
				memcpy(&pnew_item->run_stat , &pitem->run_stat , sizeof(pnew_item->run_stat));
				memcpy(&pnew_item->conn_stat , &pitem->conn_stat , sizeof(pnew_item->conn_stat));
				slog_log(slogd , SL_INFO , "<%s> copy proc_id:%d info!" , __FUNCTION__ , pitem->proc.proc_id);
				copied++;
				break;
			}
		}

		//loop
		ptarget = ptarget->next;
		pos++;
	}

	//3.update
	slog_log(slogd , SL_INFO , "<%s> success! old item_count:%d new item_count:%d copied:%d" , __FUNCTION__ , pmanager->item_count ,
			new_count , copied);
	pmanager->stat = MANAGE_STAT_OK;
	pmanager->item_count = new_count;
	free(pmanager->item_list);
	pmanager->item_list = pnew_list;

	/***Print*/
	print_manage_info(penv);
	return 0;
}

/*
 * 打印当前报表
 */
int print_manage_info(carrier_env_t *penv)
{
	manager_info_t *pmanager = NULL;
	long curr_ts = time(NULL);
	FILE *fp = NULL;

	/***Arg Check*/
	if(!penv)
		return -1;
	if(penv->proc_id > MANAGER_PROC_ID_MAX)
		return -1;

	pmanager = penv->pmanager;
	if(!pmanager)
		return -1;

	fp = fopen(MANAGER_REPORT_LOG , "w+");
	if(!fp)
	{
		slog_log(penv->slogd , SL_ERR , "<%s> failed! open report file:%s failed!" , __FUNCTION__ , MANAGER_REPORT_LOG);
		return -1;
	}
	//Print Head
	fprintf(fp , "========================[CARRIER STATS]@%s========================\n" , format_time_stamp(curr_ts));

	/***Print Body*/
	do
	{
		//basic
		fprintf(fp , "[COUNT]:%d\n" , pmanager->item_count);
		fprintf(fp , "[STATUS]:%s\n" , (pmanager->stat==MANAGE_STAT_INIT)?"init":(pmanager->stat==MANAGE_STAT_OK)?"normal":"error");
		/*
		if(pmanager->stat == MANAGE_STAT_INIT)
			fprintf(fp , "init\n");
		else if(pmanager->stat==MANAGE_STAT_OK)
			fprintf(fp , "normal\n");
		else if(pmanager->stat == MANAGE_STAT_BAD)
			fprintf(fp , "error\n");
		else
			fprintf(fp , "unknown\n");
		*/

		if(!pmanager->item_list)
			break;

		//print item list
		print_manage_item_list(1 , pmanager->item_list , pmanager->item_count , fp);
		break;
	}
	while(0);

	//fprintf(fp , "========================[END]@@@========================\n");
	fclose(fp);
	return 0;
}

int print_manage_item_list(int starts , manage_item_t *item_list , int count , FILE *fp)
{
	int i = 0;
	manage_item_t *pitem = NULL;
	half_bridge_info_t *pinfo = NULL;
    char buff[128] = {0};
    char buff2[128] = {0};

	if(!item_list || count<=0 || !fp)
		return -1;

	//each item
	for(i=0; i<count; i++)
	{
		pitem = &item_list[i];
		fprintf(fp , "----------[%d]----------\n" , i+starts);
		fprintf(fp , "%-20s %s\n" , "<FLAG>" , pitem->flag==MANAGE_ITEM_FLG_NONE?"abandon":"valid");
		fprintf(fp , "%-20s %s\n" , "<UPDATE>" , format_time_stamp(pitem->latest_update));
		fprintf(fp , "%-20s %s:%d\n" , "<PROC>" , pitem->proc.name , pitem->proc.proc_id);
		fprintf(fp , "%-20s %s:%d\n" , "<ADDR>" , pitem->proc.ip_addr , pitem->proc.port);
		fprintf(fp , "%-20s %s\n" , "<MY-CONNECT>" , (pitem->my_conn_stat==TARGET_CONN_NONE||pitem->my_conn_stat==TARGET_CONN_PROC)?
							"[connecting]":"connected");

			//basic run stat
		fprintf(fp, "<RUN>\n");
		if(pitem->run_stat.power.start_time > 0)
			fprintf(fp , "*started:%s\n" , format_time_stamp(pitem->run_stat.power.start_time));
		if(pitem->run_stat.power.shut_time > 0)
			fprintf(fp , "*shut down:%s\n" , format_time_stamp(pitem->run_stat.power.shut_time));
		if(pitem->run_stat.reload_info.reload_time > 0)
		{
			fprintf(fp , "*reload config file:%s result:%s\n" , format_time_stamp(pitem->run_stat.reload_info.reload_time) ,
					pitem->run_stat.reload_info.result==0?"success":"[failed]");
		}
		if(pitem->run_stat.upper_stat.check_time > 0)
		{
			if(pitem->run_stat.upper_stat.running == MANAGE_UPPER_LOSE)
				fprintf(fp , "*ERROR:upper process may not run:%s\n" , format_time_stamp(pitem->run_stat.upper_stat.check_time));
			else
				fprintf(fp , "%s:%s\n" , pitem->run_stat.upper_stat.running==MANAGE_UPPER_RUNNING?"*upper running":"*[upper unknown]" ,
						format_time_stamp(pitem->run_stat.upper_stat.check_time));
		}
			//net stat
		fprintf(fp , "<CONNECT>\n");
		if(pitem->conn_stat.stat == REMOTE_CONNECTING)
		{
			fprintf(fp , "*WARNING:still in connecting to <%s:%d>[%s:%d]:%s\n" , pitem->conn_stat.data.connecting.proc.name ,
					pitem->conn_stat.data.connecting.proc.proc_id , pitem->conn_stat.data.connecting.proc.ip_addr , pitem->conn_stat.data.connecting.proc.port ,
					format_time_stamp(pitem->conn_stat.ts));
		}
		else if(pitem->conn_stat.stat == REMOTE_CONNECT_ALL)
		{
			fprintf(fp , "*connect all:%s\n" , format_time_stamp(pitem->conn_stat.ts));
		}

			//bridge stat
		fprintf(fp, "<BRIDGE>\n");
		if(pitem->run_stat.bridge_stat.check_time > 0)
		{
			fprintf(fp , "*updated on:%s\n" , format_time_stamp(pitem->run_stat.bridge_stat.check_time));
			fprintf(fp , "%6s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-20s %-10s %-20s \n\n" , " " , "size" , "head" , "tail" , "opted" , "opting" , "max_size" , "min_size" , "ave_size" ,
					"dropped" , "latest_drop" , "reseted" ,  "latest_reset");

			pinfo = &pitem->run_stat.bridge_stat.info.send;
			memset(buff , 0 , sizeof(buff));
			memset(buff2 , 0 , sizeof(buff2));
			buff[0] = buff2[0] = '-';
			if(pinfo->latest_drop > 0)
				snprintf(buff , sizeof(buff) , "%s" , format_time_stamp(pinfo->latest_drop));
			if(pinfo->latest_reset > 0)
				snprintf(buff2 , sizeof(buff2) , "%s" , format_time_stamp(pinfo->latest_reset));
			fprintf(fp , "%6s %-10d %-10d %-10d %-10u %-10d %-10d %-10d %-10d %-10u %-20s %-10u %-20s\n" , ">>SEND" , pinfo->total_size , pinfo->head , pinfo->tail ,
					pinfo->handled , pinfo->handing , pinfo->min_pkg_size , pinfo->max_pkg_size , pinfo->ave_pkg_size , pinfo->dropped , buff ,
					pinfo->reset_connect , buff2);

			pinfo = &pitem->run_stat.bridge_stat.info.recv;
			memset(buff , 0 , sizeof(buff));
			memset(buff2 , 0 , sizeof(buff2));
			buff[0] = buff2[0] = '-';
			if(pinfo->latest_drop > 0)
				snprintf(buff , sizeof(buff) , "%s" , format_time_stamp(pinfo->latest_drop));
			if(pinfo->latest_reset > 0)
				snprintf(buff2 , sizeof(buff2) , "%s" , format_time_stamp(pinfo->latest_reset));
			fprintf(fp , "%6s %-10d %-10d %-10d %-10u %-10d %-10d %-10d %-10d %-10u %-20s %-10u %-20s\n" , ">>RECV" , pinfo->total_size , pinfo->head , pinfo->tail ,
					pinfo->handled , pinfo->handing , pinfo->min_pkg_size , pinfo->max_pkg_size , pinfo->ave_pkg_size , pinfo->dropped , buff ,
					pinfo->reset_connect , buff2);
		}
	}
	return 0;
}

manage_item_t *get_manage_item_by_id(carrier_env_t *penv , int proc_id)
{
	manager_info_t *pmanager;
	int i = 0;
	/***Arg Check*/
	if(!penv)
		return NULL;
	if(penv->proc_id > MANAGER_PROC_ID_MAX)	//only for manager
		return NULL;

	pmanager = penv->pmanager;
	if(!pmanager || !pmanager->item_list)
		return NULL;

	/***Search*/
	for(i=0; i<pmanager->item_count; i++)
	{
		if(pmanager->item_list[i].proc.proc_id == proc_id)
			return &pmanager->item_list[i];
	}
	return NULL;
}

/*
 * 处理来自manager tool 的包
 */
int handle_manager_cmd(carrier_env_t *penv , void *data)
{
	manager_cmd_req_t *preq = data;
	int slogd = -1;
	/***Arg Check*/
	if(!penv || !preq)
		return -1;

	slogd = penv->slogd;
	slog_log(slogd , SL_INFO , "<%s> cmd_req:type:%d" , __FUNCTION__ , preq->type);

	/***Basic Check*/
	if(!penv->pmanager)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! pmanager NULL!" , __FUNCTION__);
		return -1;
	}


	/***Handle*/
	do
	{
		if(preq->type == MANAGER_CMD_STAT)
		{
			do_manage_cmd_stat(penv , preq);
			break;
		}

		if(preq->type == MANAGER_CMD_ERR)
		{
			do_manage_cmd_error(penv , preq);
			break;
		}

		if(preq->type == MANAGER_CMD_PROTO)
		{
			do_manage_cmd_proto(penv , preq);
			break;
		}
	}
	while(0);
	return 0;
}

//生成校验key
int gen_verify_key(carrier_env_t *penv , char *key , int key_len)
{
	int i = 0;

	for(i=0; i<strlen(penv->name_space) && i<key_len; i++)
	{
		key[i] = penv->name_space[i] + 1;
	}
	slog_log(penv->slogd , SL_DEBUG , "<%s> name_space:%s key:%s" , __FUNCTION__ , penv->name_space , key);
	return 0;
}
//校验key
int do_verify_key(carrier_env_t *penv , char *key , int  key_len)
{
	int i = 0;
	for(i=0; i<strlen(penv->name_space)&&i<key_len; i++)
	{
		if((key[i]-1) != penv->name_space[i])
			return -1;
	}
	slog_log(penv->slogd , SL_DEBUG , "<%s> name_space:%s key:%s" , __FUNCTION__ , penv->name_space , key);
	return 0;
}


//send msg-event
static int send_msg_event(carrier_env_t *penv , int type , void *arg1 , void *arg2)
{
	target_detail_t  *manager_list[MANAGER_PROC_ID_MAX] = {NULL};
	bridge_package_t *ppkg;
	char pack_buff[GET_PACK_LEN(sizeof(carrier_msg_t))] = {0};
	int count = 0;
	int i = 0;
	carrier_msg_t *pmsg = NULL;
	msg_event_t *pevent = NULL;
	long curr_ts = 0;
	int ret = -1;
	int from = penv->proc_id;
	target_info_t *ptarget_info = penv->ptarget_info;
	int slogd = penv->slogd;

	/***Arg Check*/
	if(type<MSG_EVENT_T_MIN || type>MSG_EVENT_T_MAX)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! type:%d illegal!" , __FUNCTION__ , type);
		return -1;
	}

	//manager only send specific msg
	if(from <= MANAGER_PROC_ID_MAX)
	{
		;
	}

	/***Search Manager*/
	count = filt_manager_proc_id(ptarget_info , manager_list , MANAGER_PROC_ID_MAX);
	slog_log(slogd , SL_VERBOSE , "<%s> filt manager valid count:%d" , __FUNCTION__ , count);
	if(count <= 0)
	{
		slog_log(slogd  ,SL_ERR , "<%s> no valid manager found! type:%d" , __FUNCTION__ , type);
		return -1;
	}

	curr_ts = time(NULL);
	/***Fill Msg*/
	ppkg = (bridge_package_t *)pack_buff;
	ppkg->pack_head.send_ts = curr_ts;
	ppkg->pack_head.data_len = sizeof(carrier_msg_t);
	ppkg->pack_head.sender_id = from;
	ppkg->pack_head.pkg_type = BRIDGE_PKG_TYPE_CR_MSG;

	pmsg = (carrier_msg_t *)&pack_buff[BRIDGE_PACK_HEAD_LEN];
	pmsg->msg = CR_MSG_EVENT;
	pmsg->ts = curr_ts;

	/***Set Event*/
	pevent = &pmsg->data.event;
	pevent->type = type;
	switch(pevent->type)
	{
	case MSG_EVENT_T_RELOAD:
		pevent->data.value = *(int *)arg1;
	break;
	case MSG_EVENT_T_START:
		pevent->data.lvalue = *(long *)arg1;
	break;
	case MSG_EVENT_T_CONNECTING:
		memcpy(&pevent->data.one_proc , (proc_entry_t *)arg1 , sizeof(proc_entry_t));
	break;
	case MSG_EVENT_T_REPORT_STATISTICS:
		memcpy(&pevent->data.stat , (msg_event_stat_t *)arg1 , sizeof(msg_event_stat_t));
	break;
	default:
	break;
	}

	/***Send Every Manager*/
	for(i=0; i<count; i++)
	{
		if(!manager_list[i])
			continue;
		if(from == manager_list[i]->proc_id)	//if manager do not send to itself!
			continue;
		ppkg->pack_head.recver_id = manager_list[i]->proc_id;
		ret = inner_send_pkg(manager_list[i] , ppkg , sizeof(pack_buff) , slogd);

		slog_log(slogd , SL_VERBOSE , "<%s> to manager:%d type:%d ret:%d" , __FUNCTION__ , manager_list[i]->proc_id , type , ret);
	}

	return 0;
}

//send msg-error
static int send_msg_error(carrier_env_t *penv , int type , void *arg1 , void *arg2)
{
	target_detail_t  *manager_list[MANAGER_PROC_ID_MAX] = {NULL};
	bridge_package_t *ppkg;
	char pack_buff[GET_PACK_LEN(sizeof(carrier_msg_t))] = {0};
	int count = 0;
	int i = 0;
	carrier_msg_t *pmsg = NULL;
	msg_error_t *perror = NULL;
	long curr_ts = 0;
	int ret = -1;
	int from = penv->proc_id;
	target_info_t *ptarget_info = penv->ptarget_info;
	 int slogd = penv->slogd;

	/***Arg Check*/
	if(type<MSG_ERR_T_MIN || type>MSG_ERR_T_MAX)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! type:%d illegal!" , __FUNCTION__ , type);
		return -1;
	}

	//manager do not send msg generally
	if(from <= MANAGER_PROC_ID_MAX)
	{
		if(type==MSG_ERR_T_UPPER_LOSE)
			return 0;
	}

	/***Search Manager*/
	count = filt_manager_proc_id(ptarget_info , manager_list , MANAGER_PROC_ID_MAX);
	slog_log(slogd , SL_DEBUG , "<%s> filt manager valid count:%d" , __FUNCTION__ , count);
	if(count <= 0)
	{
		slog_log(slogd  ,SL_ERR , "<%s> no valid manager found! type:%d" , __FUNCTION__ , type);
		return -1;
	}

	curr_ts = time(NULL);
	/***Fill Msg*/
	ppkg = (bridge_package_t *)pack_buff;
	ppkg->pack_head.send_ts = curr_ts;
	ppkg->pack_head.data_len = sizeof(carrier_msg_t);
	ppkg->pack_head.sender_id = from;
	ppkg->pack_head.pkg_type = BRIDGE_PKG_TYPE_CR_MSG;

	pmsg = (carrier_msg_t *)&pack_buff[BRIDGE_PACK_HEAD_LEN];
	pmsg->msg = CR_MSG_ERROR;
	pmsg->ts = curr_ts;

	/***Set Event*/
	perror = &pmsg->data.error;
	perror->type = type;
	switch(perror->type)
	{
	case MSG_ERR_T_LOST_CONN:
		if(arg1)
			memcpy(&perror->data.one_proc , (proc_entry_t *)arg1 , sizeof(proc_entry_t));
	break;
	default:
	break;
	}

	/***Send Every Manager*/
	for(i=0; i<count; i++)
	{
		if(!manager_list[i])
			continue;
		if(from == manager_list[i]->proc_id)	//if manager do not send to itself!
			continue;
		ppkg->pack_head.recver_id = manager_list[i]->proc_id;
		ret = inner_send_pkg(manager_list[i] , ppkg , sizeof(pack_buff) , slogd);

		slog_log(slogd , SL_VERBOSE , "<%s> to manager:%d type:%d ret:%d" , __FUNCTION__ , manager_list[i]->proc_id , type , ret);
	}

	return 0;
}

/*
 * 过滤出manager的proc_id
 * @manager[]:manager的数组
 * @len:数组的最大长度
 * @return -1:error >=0 manager的个数
 */
static int filt_manager_proc_id(target_info_t *ptarget_info , target_detail_t *manager[] , int len)
{
	target_detail_t *ptarget = NULL;
	int count = 0;
	int i = 0;

	/***Arg Check*/
	if(!ptarget_info)
		return -1;
	if(len <= 0)
		return 0;
	if(ptarget_info->target_count <= 0)
		return 0;

	ptarget = ptarget_info->head.next;
	/***Search*/
	while(ptarget)
	{
		if(ptarget->proc_id>=MANAGER_PROC_ID_MIN && ptarget->proc_id<=MANAGER_PROC_ID_MAX &&
			ptarget->connected == TARGET_CONN_DONE)
		{
			if(i < len)
				manager[i++] = ptarget;

			count++;
		}
		ptarget = ptarget->next;
	}

	return count;
}

static int inner_send_pkg(target_detail_t *ptarget , bridge_package_t *ppkg , int pkg_len , int slogd)
{
	int ret = -1;
	unsigned int stlv_len = 0;

	/***Arg Check*/
	if(!ptarget || !ppkg)
		return -1;
	if(pkg_len <= 0)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! pack_len:%d illegal!" , __FUNCTION__ , pkg_len);
		return -1;
	}


	/***Try Flush Target*/
	if(ptarget->tail > 0)
	{
		slog_log(slogd , SL_VERBOSE , "%s is sending old package to %d. data_len:%d" , __FUNCTION__ , ptarget->proc_id ,ptarget->tail);
		ret = flush_target(ptarget , slogd);
		if(ret < 0)	//出现无法恢复错误，则已经重置链接 丢弃当前包
			return -1;
	}

	/***Send Current Pack*/
	//1.如果缓冲区未空，则说明当前不能发送，在STLV包之后投入缓冲区
	if(ptarget->tail > 0)
	{
		//剩余缓冲区空间不足以放入则丢弃了
		if((sizeof(ptarget->main_buff) - ptarget->tail) < (STLV_PACK_SAFE_LEN(pkg_len)))
		{
			slog_log(slogd , SL_ERR , "<%s> drop package. flush buff imcomplete. but target buff left space is too small! left:%d proper:%d" ,
					__FUNCTION__ , sizeof(ptarget->main_buff) - ptarget->tail , STLV_PACK_SAFE_LEN(pkg_len));
			return -1;
		}

		//pack
		stlv_len = STLV_PACK_ARRAY(&ptarget->buff[ptarget->tail] , ppkg , pkg_len);
		if(stlv_len == 0)
		{
			slog_log(slogd , SL_ERR , "<%s> flush buff imcomplete and drop package for stlv pack failed!" , __FUNCTION__);
			return -1;
		}

		//upate
		slog_log(slogd , SL_VERBOSE , "<%s> flush buff imcomplete and saved to buff success!" , __FUNCTION__);
		ptarget->tail += stlv_len;
		return 0;
	}

	//2.缓冲区已空，或可发送
	//打包
	stlv_len = STLV_PACK_ARRAY(ptarget->buff , ppkg , pkg_len);
	if(stlv_len == 0)
	{
		slog_log(slogd , SL_ERR , "<%s> drop package for stlv pack failed!" , __FUNCTION__);
		return -1;
	}

	//发送
	ptarget->tail = stlv_len;
	ptarget->latest_ts = time(NULL);
	ptarget->ready_count = 1;
	slog_log(slogd , SL_VERBOSE , "<%s> is sending curr package to %d data_len:%d" , __FUNCTION__ , ptarget->proc_id ,ptarget->tail);
	flush_target(ptarget , slogd);
	return 0;
}

static int handle_msg_event(manager_info_t *pmanage , int from , carrier_msg_t *pmsg)
{
	msg_event_t *pevent = NULL;
	proc_entry_t *pproc_entry = NULL;
	int slogd;
	int msg_log;
	int ret = 0;
	manage_item_t *pitem = NULL;
	long curr_ts = time(NULL);

	/***Arg Check*/
	if(!pmanage || from<=0 || !pmsg)
		return -1;

	slogd  = pmanage->penv->slogd;
	msg_log = pmanage->msg_slogd;
	pevent = &pmsg->data.event;
	slog_log(slogd , SL_INFO , "<%s> proc from:%d type:%d" , __FUNCTION__ , from , pevent->type);

	/***Handle*/
	pitem = get_manage_item_by_id(pmanage->penv , from);
	if(!pitem)
	{
		slog_log(slogd , SL_ERR , "<%s> get item from %d failed! type:%d" , __FUNCTION__ , from , pevent->type);
		return -1;
	}
	pitem->latest_update = curr_ts;

	switch(pevent->type)
	{
	case MSG_EVENT_T_CONNECT_ALL:
		slog_log(msg_log , SL_DEBUG , EVENT_PRINT_PREFFIX" proc %d connected all!" , from);

		pitem->conn_stat.stat = REMOTE_CONNECT_ALL;
		pitem->conn_stat.ts = pmsg->ts;

	break;
	case MSG_EVENT_T_SHUTDOWN:
		slog_log(msg_log , SL_INFO , EVENT_PRINT_PREFFIX" proc %d is shutting down at %ld !" ,from , pmsg->ts);

		pitem->my_conn_stat = TARGET_CONN_PROC;	//will reconnect
			//clear all
		memset(&pitem->conn_stat , 0 , sizeof(pitem->conn_stat));
		memset(&pitem->run_stat , 0 , sizeof(pitem->run_stat));
		pitem->run_stat.power.shut_time = pmsg->ts;

	break;
	case MSG_EVENT_T_RELOAD:
		slog_log(msg_log , SL_INFO , EVENT_PRINT_PREFFIX" proc %d reload at %ld and result:%d" , from , pmsg->ts , pevent->data.value);

		pitem->run_stat.reload_info.reload_time = pmsg->ts;
		pitem->run_stat.reload_info.result = pevent->data.value;
	break;
	case MSG_EVENT_T_START:
		slog_log(msg_log , SL_INFO , EVENT_PRINT_PREFFIX" proc %d started at %ld !" , from , pevent->data.lvalue);
		memset(&pitem->run_stat.power , 0 , sizeof(pitem->run_stat));
		pitem->run_stat.power.start_time = pevent->data.lvalue;
	break;
	case MSG_EVENT_T_CONNECTING:
		pproc_entry = &pevent->data.one_proc;
		slog_log(msg_log , SL_INFO , EVENT_PRINT_PREFFIX" proc %d is still in connect to <%s:%d>[%s:%d]" , from , pproc_entry->name ,
				pproc_entry->proc_id , pproc_entry->ip_addr , pproc_entry->port);

		pitem->conn_stat.stat = REMOTE_CONNECTING;
		pitem->conn_stat.ts = pmsg->ts;
		memcpy(&pitem->conn_stat.data.connecting.proc , pproc_entry , sizeof(proc_entry_t));
	break;
	case MSG_EVENT_T_UPPER_RUNNING:
		slog_log(msg_log , SL_DEBUG , EVENT_PRINT_PREFFIX" proc %d upper process running!" , from);

		pitem->run_stat.upper_stat.running = MANAGE_UPPER_RUNNING;
		pitem->run_stat.upper_stat.check_time = pmsg->ts;
	break;
	case MSG_EVENT_T_REPORT_STATISTICS:
		slog_log(msg_log , SL_DEBUG , EVENT_PRINT_PREFFIX" proc %d report statistics!" , from);
		pitem->run_stat.bridge_stat.check_time = pmsg->ts;
		memcpy(&pitem->run_stat.bridge_stat.info , &pevent->data.stat.bridge_info , sizeof(bridge_info_t));
	break;
	default:
		slog_log(slogd , SL_ERR , EVENT_PRINT_PREFFIX" proc %d send an unknown event:%d"  , __FUNCTION__ , from , pevent->type);
		ret = -1;
		break;
	}

	return ret;
}

//arg no check
static int handle_msg_error(manager_info_t *pmanage , int from , carrier_msg_t *pmsg)
{
	msg_error_t *perror = NULL;
	int slogd;
	int msg_log;
	int ret = 0;
	manage_item_t *pitem = NULL;
	long curr_ts = time(NULL);

	/***Arg Check*/
	if(!pmanage || from<=0 || !pmsg)
		return -1;

	slogd  = pmanage->penv->slogd;
	msg_log = pmanage->msg_slogd;
	perror = &pmsg->data.error;
	slog_log(slogd , SL_DEBUG , "<%s> proc from:%d type:%d" , __FUNCTION__ , from , perror->type);

	/***Handle*/
	pitem = get_manage_item_by_id(pmanage->penv , from);
	if(!pitem)
	{
		slog_log(slogd , SL_ERR , "<%s> get item from %d failed! type:%d" , __FUNCTION__ , from , perror->type);
		return -1;
	}
	pitem->latest_update = curr_ts;

	switch(perror->type)
	{
	case MSG_ERR_T_LOST_CONN:
		slog_log(msg_log , SL_INFO , ERROR_PRINT_PREFIX" proc %d lost  connect from %s:%d" , from , perror->data.one_proc.ip_addr ,
				perror->data.one_proc.port);
	break;
	case MSG_ERR_T_UPPER_LOSE:
		slog_log(msg_log , SL_ERR , EVENT_PRINT_PREFFIX" proc %d upper process may shut down!" , from);

		pitem->run_stat.upper_stat.running = MANAGE_UPPER_LOSE;
		pitem->run_stat.upper_stat.check_time = pmsg->ts;
	break;
	default:
		slog_log(slogd , SL_ERR , ERROR_PRINT_PREFIX" proc %d send an unknown event:%d"  , from , perror->type);
		ret = -1;
		break;
	}

	return ret;
}

char *format_time_stamp(long ts)
{
	static char time_buff[256] = {0};
	struct tm *ptm = NULL;

	/***Handle*/
	ptm = localtime(&ts);
	if(!ptm)
	{
		time_buff[0] = 0;
		return time_buff;
	}

	//construct
	memset(time_buff , 0 , sizeof(time_buff));
	snprintf(time_buff , sizeof(time_buff) , "%4d-%02d-%02d %02d:%02d:%02d" , ptm->tm_year+1900 , ptm->tm_mon+1 , ptm->tm_mday ,
			ptm->tm_hour , ptm->tm_min , ptm->tm_sec);
	return time_buff;
}

static int do_manage_cmd_stat(carrier_env_t *penv , manager_cmd_req_t *preq)
{
	char bridge_pack_buff[GET_PACK_LEN(sizeof(manager_cmd_rsp_t))];
	bridge_package_t *pbridge_pkg;
	manager_cmd_rsp_t *prsp;
	cmd_stat_req_t *pstat_req = NULL;
	cmd_stat_rsp_t *pstat_rsp = NULL;
	manager_info_t *pmanager = NULL;
	manage_item_t *pitem = NULL;
	int i;
	int slogd = - 1;
	char *p = NULL;
	char stat_all = 0;
	int ret = -1;
	int valid_count = 0;

	/***Arg Check*/
	if(!penv || !preq ||!penv->pmanager)
		return -1;

	/***Init*/
	pstat_req = &preq->data.stat;
	pmanager = penv->pmanager;
	slogd = penv->slogd;

	pbridge_pkg = (bridge_package_t *)bridge_pack_buff;
	pbridge_pkg->pack_head.data_len = sizeof(manager_cmd_rsp_t);
	pbridge_pkg->pack_head.send_ts = time(NULL);
	pbridge_pkg->pack_head.sender_id = penv->proc_id;
	pbridge_pkg->pack_head.recver_id = penv->proc_id;

	prsp = (manager_cmd_rsp_t *)&pbridge_pkg->pack_data[0];
	prsp->type = preq->type;

	pstat_rsp = &prsp->data.stat;
	memcpy(&pstat_rsp->req , pstat_req , sizeof(cmd_stat_req_t));
	pstat_rsp->count = pstat_rsp->last_update = pstat_rsp->total_count = pstat_rsp->seq = 0;

	/***check manager*/
	prsp->manage_stat = pmanager->stat;
	if(prsp->manage_stat != MANAGE_STAT_OK)
		goto _final_send;

	/***Handle*/
	switch(pstat_req->type)
	{
	case CMD_STAT_T_PART:
	case CMD_STAT_T_ALL:
		if(pstat_req->type == CMD_STAT_T_PART)
		{
			//check *
			p = strchr(pstat_req->arg , '*');
			if(p)
			{
				if(strcmp(pstat_req->arg , "*")== 0)	//stat * equal stat-all cmd
					stat_all =1;
				p[0] = 0;
			}
		}
		else
			stat_all = 1;

		//fill info
		for(i=0; i<pmanager->item_count; i++)
		{
			pitem = &pmanager->item_list[i];

			//not match
			if(!stat_all && strncmp(pstat_req->arg , pitem->proc.name , strlen(pstat_req->arg)) != 0)
				continue;

			//matched
			memcpy(&pstat_rsp->item_list[pstat_rsp->count] , pitem , sizeof(manage_item_t));
			pstat_rsp->count++;
			valid_count++;

			//rotate
			if(pstat_rsp->count >= MANAGER_STAT_MAX_ITEM)
			{
				ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
				slog_log(slogd , SL_DEBUG , "<%s> rotate append recv channel ret:%d" , __FUNCTION__ , ret);
				//refresh
				pstat_rsp->count = 0;
				pstat_rsp->seq = valid_count;
			}

		}

_final_send:
		//send last
		if(valid_count==0 || pstat_rsp->count>0)
		{
			ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
			slog_log(slogd , SL_DEBUG , "<%s> last append recv channel ret:%d count:%d" , __FUNCTION__ , ret , pstat_rsp->count);
		}

	break;
	//case CMD_STAT_T_ALL:
	//break;
	default:
		slog_log(slogd , SL_ERR , "<%s> failed! illegal type:%d" , pstat_req->type);
	break;
	}
	return 0;
}

static int do_manage_cmd_error(carrier_env_t *penv , manager_cmd_req_t *preq)
{
	char bridge_pack_buff[GET_PACK_LEN(sizeof(manager_cmd_rsp_t))];
	bridge_package_t *pbridge_pkg;
	manager_cmd_rsp_t *prsp;
	cmd_err_req_t *psub_req = NULL;
	cmd_err_rsp_t *psub_rsp = NULL;
	manager_info_t *pmanager = NULL;
	manage_item_t *pitem = NULL;
	int i;
	int slogd = - 1;
	char *p = NULL;
	char all_name = 0;
	int ret = -1;
	int valid_count = 0;
	char need_append = 0;

	/***Arg Check*/
	if(!penv || !preq ||!penv->pmanager)
		return -1;

	/***Init*/
	psub_req = &preq->data.err;
	pmanager = penv->pmanager;
	slogd = penv->slogd;

	pbridge_pkg = (bridge_package_t *)bridge_pack_buff;
	pbridge_pkg->pack_head.data_len = sizeof(manager_cmd_rsp_t);
	pbridge_pkg->pack_head.send_ts = time(NULL);
	pbridge_pkg->pack_head.sender_id = penv->proc_id;
	pbridge_pkg->pack_head.recver_id = penv->proc_id;

	prsp = (manager_cmd_rsp_t *)&pbridge_pkg->pack_data[0];
	prsp->type = preq->type;

	psub_rsp = &prsp->data.err;
	memcpy(&psub_rsp->req , psub_req , sizeof(cmd_err_req_t));
	psub_rsp->count = psub_rsp->last_update = psub_rsp->total_count = psub_rsp->seq = 0;

	/***Type*/
	if(psub_req->type < CMD_ERR_T_CONN || psub_req->type > CMD_ERR_T_ALL)
	{
		slog_log(slogd , SL_ERR , "<%s> failed! illegal type:%d" , psub_req->type);
		return -1;
	}

	/***check manager*/
	prsp->manage_stat = pmanager->stat;
	if(prsp->manage_stat != MANAGE_STAT_OK)
		goto _final_send;

	/***Handle*/
	//arg *
	p = strchr(psub_req->arg , '*');
	if(p)
	{
		if(strcmp(psub_req->arg , "*")== 0)	//stat * equal stat-all cmd
			all_name =1;
		p[0] = 0;
	}


	//fill info
	for(i=0; i<pmanager->item_count; i++)
	{
		pitem = &pmanager->item_list[i];
		need_append = 0;

		//check name
		if(!all_name && strncmp(psub_req->arg , pitem->proc.name , strlen(psub_req->arg)) != 0)
			continue;

		//check type
		do
		{
			//net error
			if(pitem->my_conn_stat!=TARGET_CONN_DONE || pitem->conn_stat.stat!=REMOTE_CONNECT_ALL)
			{
				if(psub_req->type==CMD_ERR_T_CONN || psub_req->type==CMD_ERR_T_ALL)
				{
					need_append = 1;
					break;
				}
			}

			//sys error
			if(pitem->run_stat.power.shut_time>0 || (pitem->run_stat.upper_stat.running!=MANAGE_UPPER_RUNNING &&
				pitem->proc.proc_id > MANAGER_PROC_ID_MAX))
			{
				if(psub_req->type==CMD_ERR_T_SYS || psub_req->type==CMD_ERR_T_ALL)
				{
					need_append = 1;
					break;
				}
			}
		}while(0);

		//should append?
		if(!need_append)
			continue;

		//append
		memcpy(&psub_rsp->item_list[psub_rsp->count] , pitem , sizeof(manage_item_t));
		psub_rsp->count++;
		valid_count++;

		//rotate
		if(psub_rsp->count >= MANAGER_STAT_MAX_ITEM)
		{
			ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
			slog_log(slogd , SL_DEBUG , "<%s> rotate append recv channel ret:%d" , __FUNCTION__ , ret);
			//refresh
			psub_rsp->count = 0;
			psub_rsp->seq = valid_count;
		}

	}

_final_send:
	//send last
	if(valid_count==0 || psub_rsp->count>0)
	{
		ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
		slog_log(slogd , SL_DEBUG , "<%s> last append recv channel ret:%d count:%d" , __FUNCTION__ , ret , psub_rsp->count);
	}
	return 0;
}

static int do_manage_cmd_proto(carrier_env_t *penv , manager_cmd_req_t *preq)
{
	char arg[MANAGER_CMD_ARG_LEN] = {0};
	char *p = NULL;

	char bridge_pack_buff[GET_PACK_LEN(sizeof(manager_cmd_rsp_t))] = {0};
	bridge_package_t *pbridge_pkg;
	manager_cmd_rsp_t *prsp;
	cmd_proto_req_t *psub_req = NULL;
	cmd_proto_rsp_t *psub_rsp = NULL;

	manager_info_t *pmanager = NULL;
	manage_item_t *pitem = NULL;
	target_detail_t *ptarget = NULL;
	int i = 0;
	int slogd = -1;
	int ret = -1;
	int send_back = 0;

	/***Arg Check*/
	if(!penv || !preq)
		return -1;

	/***Init*/
	slogd = penv->slogd;
	psub_req = &preq->data.proto;
	pmanager = penv->pmanager;

	pbridge_pkg = (bridge_package_t *)bridge_pack_buff;
	pbridge_pkg->pack_head.data_len = sizeof(manager_cmd_rsp_t);
	pbridge_pkg->pack_head.send_ts = time(NULL);
	pbridge_pkg->pack_head.sender_id = penv->proc_id;
	pbridge_pkg->pack_head.recver_id = penv->proc_id;

	prsp = (manager_cmd_rsp_t *)&pbridge_pkg->pack_data[0];
	prsp->type = preq->type;

	psub_rsp = &prsp->data.proto;
	psub_rsp->type = psub_req->type;
	strncpy(psub_rsp->arg , psub_req->arg , sizeof(psub_rsp->arg));
	psub_rsp->result = -1;
	prsp->manage_stat = pmanager->stat;
	if(prsp->manage_stat != MANAGE_STAT_OK)
	{
		send_back = 1;
		goto _send_back;
	}

	/***Handle*/
	switch(psub_req->type)
	{
	case CMD_PROTO_T_PING:
		//get target
		for(i=0; i<pmanager->item_count; i++)
		{
			pitem = &pmanager->item_list[i];
			if(strncmp(pitem->proc.name , psub_req->arg , sizeof(PROC_ENTRY_NAME_LEN)) == 0)
				break;
		}
		if(i >= pmanager->item_count) //not found
		{
			slog_log(slogd , SL_ERR , "<%s> PING %s Item not found!" , __FUNCTION__ , psub_req->arg);
			send_back = 1;
			break;
		}

		ptarget = proc_id2_target(penv->ptarget_info , pitem->proc.proc_id);
		if(!ptarget)
		{
			slog_log(slogd , SL_ERR , "<%s> PING %s Target not found!" , __FUNCTION__ , psub_req->arg);
			send_back = 1;
			break;
		}

		//match send to server
		ret = send_inner_proto(penv , ptarget , INNER_PROTO_PING , NULL , NULL);
		if(ret != 0)
		{
			slog_log(slogd , SL_ERR , "<%s> Send Inner Proto to %s Failed!" , __FUNCTION__ , psub_req->arg);
			send_back = 1;
			break;
		}
		send_inner_proto(penv , ptarget , INNER_PROTO_CONN_TRAFFIC_REQ , "*" , NULL);
		break;
	case CMD_PROTO_T_TRAFFIC:
		//check arg
		strncpy(arg , psub_req->arg , sizeof(arg));
		p = strchr(arg , ' ');
		if(!p)
		{
			send_back = 1;
			slog_log(slogd , SL_ERR , "<%s> parse traffic failed! can not get src" , __FUNCTION__);
			break;
		}
		p[0] = 0;
		p++;
		if(strlen(arg)<=0 || strlen(p)<=0)
		{
			send_back = 1;
			slog_log(slogd , SL_ERR , "<%s> parse traffic failed! can not get src or dst! arg:%s" , __FUNCTION__ , psub_req->arg);
			break;
		}
		//get target
		for(i=0; i<pmanager->item_count; i++)
		{
			pitem = &pmanager->item_list[i];
			if(strncmp(pitem->proc.name , arg , sizeof(PROC_ENTRY_NAME_LEN)) == 0)
				break;
		}
		if(i >= pmanager->item_count) //not found
		{
			slog_log(slogd , SL_ERR , "<%s> TRAFFIC %s Item not found!" , __FUNCTION__ , arg);
			send_back = 1;
			break;
		}

		ptarget = proc_id2_target(penv->ptarget_info , pitem->proc.proc_id);
		if(!ptarget)
		{
			slog_log(slogd , SL_ERR , "<%s> TRAFFIC %s Target not found!" , __FUNCTION__ , arg);
			send_back = 1;
			break;
		}

		//match send to server
		ret = send_inner_proto(penv , ptarget , INNER_PROTO_CONN_TRAFFIC_REQ , p , NULL);
		if(ret != 0)
		{
			slog_log(slogd , SL_ERR , "<%s> Send Inner Proto to %s Failed!" , __FUNCTION__ , arg);
			send_back = 1;
			break;
		}
		break;
	default:
		slog_log(slogd , SL_ERR , "<%s> failed! illegal proto:%d" , __FUNCTION__ , psub_req->type);
		break;
	}

	/***Send Back*/
_send_back:
	if(send_back)
	{
		ret = append_recv_channel(penv->phub , (char *)pbridge_pkg);
		slog_log(slogd , SL_DEBUG , "<%s> append recv channel ret:%d result:%d" , __FUNCTION__ , ret , psub_rsp->result);
	}

	return 0;
}

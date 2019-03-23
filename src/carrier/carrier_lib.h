/*
 * carrier_lib.h
 *
 *  Created on: 2019年2月21日
 *      Author: nmsoccer
 */

#ifndef CARRIER_LIB_H_
#define CARRIER_LIB_H_

#include "proc_bridge_base.h"

/*********DATA STRUCT*/
/*
 * proc_entry
 */
#define PROC_ENTRY_NAME_LEN BRIDGE_PROC_NAME_LEN
#define PROC_ENTRY_IP_LEN	64
typedef struct _proc_entry_t
{
	int proc_id;
	char name[PROC_ENTRY_NAME_LEN];
	char ip_addr[PROC_ENTRY_IP_LEN];
	short port;
}proc_entry_t;


/*
 * channel_info
 */
typedef struct _half_bridge_info_t
{
	int total_size;
	int head;
	int tail;
	unsigned int handled;	//处理过的包数
	int handing;	//正在处理的包数

	int	min_pkg_size;	//最小包长
	int 	max_pkg_size;	//最大包长
	int ave_pkg_size;	//平均包长

	unsigned int dropped;	//丢弃包数目
	long latest_drop;	//最近一次丢包时间

	unsigned int reset_connect;	//重置链接次数
	long latest_reset;
}half_bridge_info_t;

typedef struct _bridge_info_t
{
	half_bridge_info_t send;
	half_bridge_info_t recv;
}bridge_info_t;

/*
 * 每链接的信息
 */
typedef struct _conn_traffic_t
{
	unsigned int handled;	//处理过的包
	unsigned int handing; //正在处理的包
	int min_size;
	int max_size;
	int ave_size;	//平均包长
	unsigned int dropped; 	//丢包数
	long latest_drop;	//最近一次丢包
	unsigned int reset; //链接重置数
	long latest_reset;	//最近一次重置
}conn_traffic_t;
#define MAX_CONN_TRAFFIC_PER_PKG 50

typedef struct _traffic_list_t
{
	int count;
	char owner[PROC_ENTRY_NAME_LEN];
	char names[MAX_CONN_TRAFFIC_PER_PKG][PROC_ENTRY_NAME_LEN];
	conn_traffic_t lists[MAX_CONN_TRAFFIC_PER_PKG];
}traffic_list_t;

/*
 * target info
 */
#define TARGET_CONN_NONE 0	//未连接
#define TARGET_CONN_DONE 1 //已连接
#define TARGET_CONN_PROC 2	//正在连接

typedef struct _target_detail
{
	char connected;	/*是否已经链接*/
	char target_name[PROC_ENTRY_NAME_LEN];
	char ip_addr[PROC_ENTRY_NAME_LEN];	/*目标IP*/
	short port;	/*目标端口*/
	int proc_id;	/*目标proc_id*/
	int fd;	/*使用的fd*/
	struct _target_detail *next;
	long latest_ts;	//最早进入的PACK时间戳 粗略计数
	int ready_count;		//缓冲区里待发送的包数目 只是粗略计数
	int tail;	// tail of valid data in buff
	char *buff;
	char main_buff[BRIDGE_PACK_LEN * 5];
	char back_buff[BRIDGE_PACK_LEN * 5];
	conn_traffic_t traffic;
}target_detail_t;

typedef struct _target_
{
	int target_count;
	//target_detail_t target_detail[MAX_BRIDGE_PER_PROC];
	//int epoll_manage_in_connect; //在连接阶段需要由epoll管理的socket数目[连接进行+1;连接成功-1]
	target_detail_t head;		//dummy head
}target_info_t;


/*
 * client_info
 */
typedef struct _client_info
{
	int fd; //sock fd
	char *buff;
	int tail;
	char main_buff[BRIDGE_PACK_LEN];
	char back_buff[BRIDGE_PACK_LEN];
	char client_ip[64];
	unsigned short client_port;
	long connect_time;	//建立链接的时间
	char verify;	//是否经过验证
	int proc_id;	//only after verify
	struct _client_info *next;
}client_info_t;
typedef struct _client_list
{
	int total_count;
	client_info_t *list;
}client_list_t;

/*
 * manager_info
 */
#define MANAGER_MSG_LOG "msg.log"
#define MANAGER_REPORT_LOG "report.log"

#define MANAGE_STAT_INIT 0
#define MANAGE_STAT_OK 1
#define MANAGE_STAT_BAD 2

#define MANAGE_UPPER_UNKNOWN 0
#define MANAGE_UPPER_RUNNING 1
#define MANAGE_UPPER_LOSE 2

	//为每个carrier维护一个item
#define MANAGE_ITEM_FLG_NONE 0
#define MANAGE_ITEM_FLG_VALID 1
#define MANAGE_ITEM_STAT_LEN 128
typedef struct _manage_item_t
{
	char flag;	//refer MANAGE_ITEM_FLG_xx
	char my_conn_stat;	//connected to remote status.refer TARGET_CONN_xx
	long latest_update;	//latest update time stamp
	proc_entry_t proc;
	struct
	{
#define REMOTE_CONNECT_ALL 1
#define REMOTE_CONNECTING 2
		char stat;	//1:connect-all 2:connecting
		long ts;
		union
		{
			struct
			{
				proc_entry_t proc;
			}connecting;
		}data;

	}conn_stat;
	struct
	{
		struct
		{
			long start_time;	//拉起时间
			long shut_time;	//关闭时间
		}power;
		struct
		{
			long reload_time;	//最近一次加载cfg
			int result;	//结果
		}reload_info;
		struct
		{
			long check_time;
			char running;	//0:no 1:running
		}upper_stat;
		struct
		{
			long check_time;
			bridge_info_t info;	//bridge_info
		}bridge_stat;
	}run_stat;
}manage_item_t;

typedef struct _manager_info_t
{
	struct _carrier_env_t *penv;
	int msg_slogd;	//msg log
	//int item_len;	//max item
	char stat;	//refer manage_stat_xx
	int item_count;	//item count
	manage_item_t *item_list;

}manager_info_t;

/*
 * carrier_env
 */
typedef struct _carrier_env_t
{
	char proc_name[BRIDGE_PROC_NAME_LEN];
	char name_space[PROC_BRIDGE_NAME_SPACE_LEN];
	char lock_file_name[128];
	char cfg_file_name[128];
	int proc_id;
	int slogd;	//slog descriptor
	long started_ts;	//启动时间
	manager_info_t *pmanager;	//only for manager proc
	target_info_t *ptarget_info;
	client_list_t *pclient_list;
	bridge_hub_t *phub;
	bridge_info_t bridge_info;
	struct
	{
		char sig_exit;	//信号退出
		char sig_reload;	//信号重载
	}sig_map;
}carrier_env_t;


/*
 * carrier_msg
 */
#define CR_MSG_MIN	     	1
#define CR_MSG_EVENT 	1	//事件类消息
#define CR_MSG_ERROR 	2	//错误类消息
#define CR_MSG_MAX		2

#define EVENT_PRINT_PREFFIX "[EVENT]"
#define ERROR_PRINT_PREFIX "[ERROR]"

#define MSG_EVENT_T_MIN			1
#define MSG_EVENT_T_START 	1	//进程拉起
#define MSG_EVENT_T_CONNECT_ALL			2	//连接OK
#define MSG_EVENT_T_RELOAD 	4	//重加载配置
#define MSG_EVENT_T_SHUTDOWN 5	//进程关闭
#define MSG_EVENT_T_CONNECTING 6	//连接中
#define MSG_EVENT_T_UPPER_RUNNING 7 //上层业务进程正常运行
#define MSG_EVENT_T_REPORT_STATISTICS 8	//报告数据
#define MSG_EVENT_T_MAX		8

#define MSG_ERR_T_MIN			1
#define MSG_ERR_T_START 		1	//进程拉起失败
#define MSG_ERR_T_RELOAD 	2	//重加载配置失败
#define MSG_ERR_T_CONNECT	3	//连接失败
#define MSG_ERR_T_LOST_CONN 4 //丢失连接
#define MSG_ERR_T_UPPER_LOSE 5 //上层业务进程丢失
#define MSG_ERR_T_MAX				5

//msg-event
typedef struct _msg_event_stat_t
{
	bridge_info_t bridge_info;	//bridge statistics
}msg_event_stat_t;
typedef struct _msg_event_t
{
	int type;	//refer MSG_EVENT_T_xx
	union
	{
		int value;
		long lvalue;
		proc_entry_t one_proc;
		msg_event_stat_t stat;
	}data;
}msg_event_t;

//msg-error
typedef struct _msg_error_t
{
	int type;	//refer MSG_ERR_T_xx
	union
	{
		proc_entry_t one_proc;	//one proc
	}data;
}msg_error_t;

typedef struct _carrier_msg_t
{
	int msg;	//refer CR_MSG_xx
	long ts;
	union
	{
		msg_event_t event;
		msg_error_t error;
	}data;
}carrier_msg_t;

/***INNER_PROTO*/
#define INNER_PROTO_MIN 1
#define INNER_PROTO_PING 1	//ping
#define INNER_PROTO_PONG 2 //pong
#define INNER_PROTO_VERIFY_REQ 3 //链接验证
#define INNER_PROTO_VERIFY_RSP  4
#define INNER_PROTO_CONN_TRAFFIC_REQ 5
#define INNER_PROTO_CONN_TRAFFIC_RSP 6
#define INNER_PROTO_MAX 6

typedef struct _inner_proto_t
{
	int type;	//refer INNER_PROTO_**
	char arg[MANAGER_CMD_ARG_LEN];
	union
	{
		char result;
		char proc_name[PROC_ENTRY_NAME_LEN];
		char verify_key[BRIDGE_PROC_CONN_VERIFY_KEY_LEN];
		traffic_list_t traffic_list;
	}data;
}inner_proto_t;


/***********API***********/
extern char *format_time_stamp(long ts);
extern target_detail_t *proc_id2_target(target_info_t *ptarget_info , int proc_id);
extern int parse_proc_info(char *proc_info , proc_entry_t *pentry , int slogd);
extern int send_carrier_msg(carrier_env_t *penv , int msg , int type , void *arg1 , void *arg2);
extern int send_inner_proto(carrier_env_t *penv , target_detail_t *ptarget , int proto ,  void *arg1 , void*arg2);
extern int recv_inner_proto(carrier_env_t *penv , client_info_t *pclient , char *package);
extern int manager_handle(manager_info_t *pmanager , char *package , int slogd);
/*
 * 清空某个channel的target发送缓冲区
 * -1:错误
 *  0:成功
 */
extern int flush_target(target_detail_t *ptarget , int slogd);
/*
 * 初始化manager的管理列表
 */
extern int init_manager_item_list(carrier_env_t *penv);
/*
 * 重建manager的管理列表
 * 用于在动态加载配置文件之后
 */
extern int rebuild_manager_item_list(carrier_env_t *penv);
/*
 * 打印当前报表
 */
extern int print_manage_info(carrier_env_t *penv);
extern manage_item_t *get_manage_item_by_id(carrier_env_t *penv , int proc_id);
extern int print_manage_item_list(int starts , manage_item_t *item_list , int count , FILE *fp);

/*
 * 处理来自manager tool 的包
 */
extern int handle_manager_cmd(carrier_env_t *penv , void *preq);
extern int append_recv_channel(bridge_hub_t *phub , char *pstpack);

//生成校验key
extern int gen_verify_key(carrier_env_t *penv , char *key , int key_len);
//校验key
extern int do_verify_key(carrier_env_t *penv , char *key , int  key_len);

#endif /* CARRIER_LIB_H_*/

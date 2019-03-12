# proc_bridge
A distributed inter-proc communication system   
一个分布式的进程间通信组件.  

### 基本特点
* 接口简单: 只需使用简单几个接口便可进行进程间的信息通信  
* 部署方便: 通过单个脚本控制包括针对整个系统或者单个进程的部署，启停，重载及配置生成  
* 统一管理: 系统中所有的节点进程信息都通过统一的管理端接口进行监控和查看
* 动态扩展: 在系统运行中动态更改节点路由，新增或者删除路径，不影响系统的正常工作
* 自动重连: 在节点丢失链接之后会周期动态重连，并上报告警信息至管理端，一般无需手动干预
* 想到了再说...

### 环境依赖
在通用正常配置的unix-linux开发环境中，无需特别依赖，只需要再安装两个基本库即可:
* slog:一个简单的日志记录库
  * https://github.com/nmsoccer/slog
  * 安装:下载安装包文件xx.zip; unzip xx.zip; ./install.sh  
         
* stlv:一个简单的stlv格式打解包库
  * https://github.com/nmsoccer/stlv
  * 安装:下载安装包文件xx.zip; unzip xx.zip; ./install.sh

### 开发接口  
先行说明系统提供的API接口，其中的一些名词在随后介绍.  
* `int open_bridge(char *name_space , int proc_id , int slogd);`
  * name_space: proc_bridge系统定义的命名空间,每套proc_bridge系统需定义一个命名空间
  * proc_id:进程在proc_bridge系统中的唯一整型标识
  * slogd:打开的slog日志描述符(请看上面)
  * return: -1:failed >=0 success,返回打开的操纵bridge描述符(句柄) 
  
* `int close_bridge(int bd);`  
  * bd:通过open_bridge返回的bridge_descriptor   
  * return: -1:failed  0:success
 
* `int send_to_bridge(int bd , int target_id , char *sending_data , int len);`  
  * bd:通过open_bridge返回的bridge_descriptor
  * target_id:目标服务进程的proc_id
  * sending_data:发送的数据
  * len:发送的数据长度
  * return: -1:错误 -2:发送区满 -3:发送区剩余空间不足 0:success

* `int recv_from_bridge(int bd , char *recv_buff , int recv_len);`
  * bd:通过open_bridge返回的bridge_descriptor
  * recv_buff:接收缓冲区
  * recv_len:接收缓冲区长度
  * retrun: -1:错误 -2:暂无数据 -3:接收缓冲区长度不足 >0:sucess,实际接收的数据长度
  
### 架构简介  
下面以游戏服务器开发中常见的三段论，接入-逻辑-数据三层进程间通信架构举例，后续的扩展与说明均在此基础进行扩充  

# to be continue

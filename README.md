# proc_bridge  
[![996.icu](https://img.shields.io/badge/link-996.icu-red.svg)](https://996.icu) [![LICENSE](https://img.shields.io/badge/license-Anti%20996-blue.svg)](https://github.com/996icu/996.ICU/blob/master/LICENSE)    
A distributed inter-proc communication system   
一个分布式的进程间通信组件.  

### 基本特点
* _接口简单_: 只需使用简单几个接口便可进行进程间的信息通信  
* _部署方便_: 通过单个脚本控制包括针对整个系统或者单个进程的部署，启停，重载及配置生成    
* _统一管理_: 系统中所有的节点进程信息都通过统一的管理端接口进行监控和查看
* _动态扩展_: 在系统运行中动态更改节点路由，新增或者删除路径，不影响系统的正常工作
* _自动重连_: 在节点丢失链接之后会周期动态重连，并上报告警信息至管理端，一般无需手动干预
* 想好了再说...

### 环境依赖
该系统设计目的之一就是尽量减少环境依赖，所以在通用配置的unix-linux开发环境中，除了在构建打包机器上需要安装python用于部署之外，只需要再安装两个简单的支持库即可:
* _slog_:一个简单的日志库
  * https://github.com/nmsoccer/slog
  * 安装:下载安装包文件xx.zip; unzip xx.zip; ./install.sh  
         
* _stlv_:一个简单的stlv格式打解包库
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

* `int recv_from_bridge(int bd , char *recv_buff , int recv_len , int *sender , int drop_time);`
  * bd:通过open_bridge返回的bridge_descriptor
  * recv_buff:接收缓冲区
  * recv_len:接收缓冲区长度
  * sender: if Not NULL 则填写发包进程的proc_id
  * drop_time:>=0丢弃发送时间超过drop_time(秒)的包; -1:不丢弃任何包
  * retrun: -1:错误 -2:暂无数据 -3:接收缓冲区长度不足 >0:sucess,实际接收的数据长度
  
* 安装开发库
  * 下载proc_bridge-master.zip
  * unzip proc_bridge-master.zip
  * cd proc_bridge-master/src/library
  * bash ./install_lib.sh 安装开发库到/usr/local/include/proc_bridge 和 /usr/local/lib(可能需要root权限)
  

  
### 架构简介  
下面以游戏服务器开发中常见的三段论，接入-逻辑-数据三层进程间通信架构举例，后续的扩展与说明均在此基础进行扩充    
![架构图](https://github.com/nmsoccer/proc_bridge/blob/master/pic/main.jpg)

* 左边矩形框里展示了业务视图，它代表业务开发侧的部署。connect_server代表接入服务器，logic_server代表游戏逻辑服务器,db_server代表数据读写服务器.它们之间的虚线表明其逻辑通信渠道，彼此互通.  
* 右边椭圆形框里展示的是proc_bridge系统在实际通信中处于的层次。它托管了左边或者叫上层业务进程的通信。其为每个业务进程提供一个carrier进行通信接管。所以业务进程的实际的信息流通路径为右边的实线所示  
* 最右边是proc_bridge系统为管理所有carrier进程的manager，它们也可以分布式部署提供可用性

### 名词解释
* 【 **名字空间** 】 又叫命名空间。它用于标记一个完整的proc_bridge系统。一个proc_bridge系统中所有的通信节点均共享该名字空间，不同的proc_bridge系统需要配置不同的名字空间，否则可能会产生系统间通信冲突.
* 【 **通信节点** 】 表示在proc_bridge系统中参与通信的一个实体.它由上层业务进程upper和下层通信代理进程lower组成。在一个proc_bridge系统里唯一标记一个通信节点的key为【proc_id】(唯一),辅助解释字段为【proc_name】(在lower层唯一),物理地址为【地址】(唯一)
* 【 **upper/lower进程** 】upper进程代表通信节点的上半部分，即通信的业务进程，由使用proc_bridge进行通信的各具体业务开发者开发比如网络游戏等;lower进程代表在proc_bridge系统中接管upper进程通信的托管进程，即carrier.每个在proc_bridge里需要通信的业务进程都有一个对应的carrier为其管理通信.
* 【 **proc_id** 】 在一个proc_bridge系统中的全局唯一id，它与一个通信节点一一对应，不能发生冲突，由配置文件编写者自己分配设置。被upper进程和lower进程共享。
* 【 **proc_name** 】用于对proc_id和upper进程进行辅助说明的标记，只在Lower层有效。它可以用来对upper进程进行功能性的说明，在实际部署中只作用于lower层，和proc_id一一对应，不能发生冲突
* 【 **地址** 】地址即是常见的ip：port二元组，它标记了通信节点在实际网络中的物理地址。同时由于upper/lower进程同机部署，所以ip地址也代表了业务进程的地址。port端口号只用于lower进程通信，和upper进程无关  
* 【 **通信规则** 】 proc_bridge的通信节点特性类似单工通信，节点主动发送，被动接收。节点只能主动建立发送信道，对接收信道不做要求。即：一个节点如果想和另一节点发生通信，需要主动建立与对端的发送链路。如果想要互相通信，则对端需主动向该节点建立发送链路。一个通信节点  

![如图所示](https://github.com/nmsoccer/proc_bridge/blob/master/pic/proc_bridge_node.png)  
各名词的基本含义如上图所示.
这里放大了db_server通信节点的构造，并且列出了【namespace】,【proc_id】,【proc_name】,【ip,port】等的作用域。其中【proc_name】也叫db_server，这个是对业务进程 _dbserver_ 的一个显性描述，业务进程可以叫任意名字，这里的【proc_name】只约束到lower层  


### 简单演示
下面通过一个简单的echo实例来演示一下该系统的部署和使用。
#### 依赖环境
  * 请参考上面的环境依赖一节，下载安装好slog和stlv库。
  * 请确保构建服务器上已装好python和expect
  * 下载proc_bridge-master.zip，unzip解压到服务器
  * 进入proc_bridge-master/src/library
  * 执行./install_lib.sh安装支持库和头文件到/usr/local/xx 这里可能需要root权限
  
#### 本地部署  
首先作最简单的部署，节点均部署到本机。
  1) 进入proc_bridge-master/src/demo/echo目录
  2) 直接使用bridge.cfg， (_配置文件详细内容请参考https://github.com/nmsoccer/proc_bridge/wiki/config-detail_)
```
[BRIDGE_USER]
nmsoccer
[/BRIDGE_USER]

#BRIDGE_DIR
#部署安装的目录(绝对路径)
[BRIDGE_DIR]
/home/nmsoccer/proc_bridge
[/BRIDGE_DIR]

...

[PROC]
client = 20000 : 127.0.0.1 : 11070 # default r/s size
server = 30000 : 127.0.0.1 : 11060 : 50 #send_size:50
[/PROC]

[CHANNEL]
client:server #c->s
server:client #s->c
[/CHANNEL] 
```
  3) 其他不做改动，需要修改[BRIDGE_USER]和[BRIDGE_DIR]为本机服务器相关的用户和安装目录
  4) 执行脚本./build.sh install(_安装部署脚本详细说明请参阅https://github.com/nmsoccer/proc_bridge/wiki/construct-tool_)
  5) 正常情况下应安装成功,并出现以下提示:
  ```
  ...
  deploy finish
  ---------[Complete]------------
  ========================
  install complete!
  you may use './manager -i 1 -N <name_space>' to start manager to check info.
  you may use './bridge_build.py xx' to restart/shutdown/reload carrier
  good luck
  ========================
  ```
  6) 执行./manager -i 1 -N echo打开管理器。执行STAT \* 查看carrier运行状况  
  (_manager管理端详细说明参考https://github.com/nmsoccer/proc_bridge/wiki/manager_)
  ```
==========================
【STAT *】
==========================
----------[1]----------
<FLAG>               valid
<UPDATE>             2019-03-30 21:05:49
<PROC>               client:20000
<ADDR>               127.0.0.1:11070
<MY-CONNECT>         connected
<RUN>
*started:2019-03-30 21:00:38
*ERROR:upper process may not run:2019-03-30 21:05:49
<CONNECT>
*connect all:2019-03-30 21:05:49
...
----------[2]----------
<FLAG>               valid
<UPDATE>             2019-03-30 21:05:49
<PROC>               server:30000
<ADDR>               127.0.0.1:11060
<MY-CONNECT>         connected
<RUN>
*started:2019-03-30 21:00:41
*ERROR:upper process may not run:2019-03-30 21:05:49
<CONNECT>
*connect all:2019-03-30 21:05:47
  ```
  这里说明proc_bridge系统的client和server节点的lower层carrier已经部署成功，只等上层业务进程使用
  
  7) 执行./make_test.sh编译一个用于演示的简单上层业务进程
  ```
  bash ./make_test.sh 
=====================================
make success
>>exe './tester -i 30000 -N echo -s &'
>>exe './tester -i 20000 -t 30000 -N echo -c'
then to test echo...
=====================================
  ```
  
  8) 本地执行./tester -i 30000 -N echo -s &作为server;./tester -i 20000 -t 30000 -N echo -c作为client
  ```
[nmsoccer@AY13122114553417645eZ echo]$ ./tester -i 30000 -N echo -s &
[1] 18438
act as a server...
[nmsoccer@AY13122114553417645eZ echo]$ ./tester -i 20000 -t 30000 -N echo -c
act as a client...
CS 
[server]: CS 

FUDDKKK
[server]: FUDDKKK

******F****K
[server]: ******F****K
  ```
  9) ./build.sh clear 关停echo并清除文件
  
  
#### 异机部署  
  1) mv bridge.cfg bridge.cfg_bak 将同机部署的配置文件备份
  2) mv bridge.cfg_diff_server bridge.cfg 使用新的配置文件，变动的内容如下:
  ```
[MANAGER_ADDR]
10.144.172.215:11100;
[/MANAGER_ADDR]

[PROC]
client = 20000 : 10.161.37.104 : 11070
server = 30000 : 10.144.172.215 : 11060 : 50 
[/PROC]
  ```
  
其中client部署到10.161.37.104服务器上，server部署到10.144.172.215

3) 由于该套构建脚本使用expect来同步远程配置及文件，所以需要配置所部署用户在远程服务器端的密码.  在../../carrier/tools/deploy_proc.sh脚本里填入REMOTE_PASS：  

```
...
EXPECT_PREFIX="expect "
REMOTE_PASS="*****" #remote passwd to sync files
EXE_CMD="./exe_cmd.exp"
SCP_CMD="./scp.exp"
...

```
4) 其他不做改动，需要修改[BRIDGE_USER]和[BRIDGE_DIR]为所有部署服务器共同使用的用户和安装目录  
5) 执行脚本./build.sh install(_安装部署脚本详细说明请参阅https://github.com/nmsoccer/proc_bridge/wiki/construct-tool_)
6) 正常情况下应安装成功,并出现以下提示:  
```
  deploy finish
  ========================
  install complete!
  you may use './manager -i 1 -N <name_space>' to start manager to check info.
  you may use './bridge_build.py xx' to restart/shutdown/reload carrier
  good luck
  ========================
```
7) 执行./manager -i 1 -N echo打开管理器。执行STAT \* 查看carrier运行状况  
  (_manager管理端详细说明参考https://github.com/nmsoccer/proc_bridge/wiki/manager_)
  ```
  ...
STAT *
==========================
【STAT *】
==========================
----------[1]----------
<FLAG>               valid
<UPDATE>             2019-03-30 21:38:36
<PROC>               client:20000
<ADDR>               10.161.37.104:11070
<MY-CONNECT>         connected
<RUN>
*started:2019-03-30 21:36:19
*ERROR:upper process may not run:2019-03-30 21:38:34
<CONNECT>
*connect all:2019-03-30 21:38:36
 ...
 ----------[2]----------
<FLAG>               valid
<UPDATE>             2019-03-30 21:38:34
<PROC>               server:30000
<ADDR>               10.144.172.215:11060
<MY-CONNECT>         connected
<RUN>
*started:2019-03-30 21:36:23
*ERROR:upper process may not run:2019-03-30 21:38:34
<CONNECT>
*connect all:2019-03-30 21:38:34
 
  ```
以上可以见server节点已经部署到10.144.172.215,而client节点已部署到10.161.37.104

8) 执行./make_test.sh编译一个用于演示的简单上层业务进程
  ```
bash ./make_test.sh 
=====================================
make success
>>exe './tester -i 30000 -N echo -s &'
>>exe './tester -i 20000 -t 30000 -N echo -c'
then to test echo...
=====================================
  ```
9) 在10.144.172.215服务器执行./tester -i 30000 -N echo -s   
将tester复制到10.161.37.104服务器任意目录，并执行./tester -i 20000 -t 30000 -N echo -c
```
[nmsoccerg@AY13122114553417645eZ echo]$ ./tester -i 30000 -t 20000 -N echo -s 
act as a server...
...
[nmsoccer@AY140212101414859034Z ~]$ ./tester -i 20000 -t 30000 -N echo -c
act as a client...
hello world!
[server]: hello world!

new day
[server]: new day

new night
[server]: new night

cs and suomei are dog-man and woman
[server]: cs and suomei are dog-man and woman

```
10) ./build.sh clear 关停echo并清除文件



# MORE
更多详细配置和使用说明请查看:https://github.com/nmsoccer/proc_bridge/wiki

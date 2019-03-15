#!/bin/bash
LOCAL_CFG_DIR="./cfg"
CFG_FILE="carrier.cfg"
BINARY_FILES=("creater" "deleter" "carrier")
WORK_DIR=`pwd`
BACK_UP_DIR="./backup"


#arg list 
proc_name=""
proc_id=""
ip=""
port=""
usr=""
dir=""
opt=""
send_size=""
recv_size=""
name_space=""
curr_time=`date +'%s'`

#global info
is_local=0

function show_help()
{
  echo "usage:$0 <proc_name> <proc_id> <ip> <port> <usr> <dir> <opt> <send_size> <recv_size> <name_space>"
}

function deploy()
{
  echo "try to deploy $proc_name to $usr@$ip[$port]:$dir"
  if [[ -z ${send_size} ]]
  then
    echo "send_size none!"
    exit 1
  fi

  if [[ -z ${recv_size} ]]
  then
    echo "recv_size none!"
    exit 1
  fi

  if [[ -z ${name_space} ]]
  then
    echo "name_space none!"
    exit 1
  fi

  #make backup
  mkdir -p ${BACK_UP_DIR}
  if [[ ! -e ${BACK_UP_DIR} ]]
  then
    echo "${BACK_UP_DIR} not exist!"
    exit 1
  fi


  #check cfg file
  cfg_path="${LOCAL_CFG_DIR}/${proc_name}/${CFG_FILE}"  
  if [[ ! -e ${cfg_path} ]]
  then
    echo "cfg file:${cfg_path} not found!"
    exit 1
  fi

  #check binary file
  for file in ${BINARY_FILES[@]}
  do
    if [[ ! -e ${file} ]]
    then
      echo "${file} not exist!"
      exit 1
    fi
  done 

  #create target dir
  target_dir=${dir}/${proc_name}
  if [[ ${is_local} -eq 1 ]]
  then
    mkdir -p ${target_dir}
  else
    echo "ha?"
  fi
  if [[ $? -ne 0 ]]
  then
    exit 1
  fi

  #tar file
  tar_file=carrier.${proc_id}.tar.gz
  tar -zcvf ${tar_file} ${BINARY_FILES[@]} ${cfg_path} 1>/dev/null 2>&1
  if [[ ! -e ${tar_file} ]]
  then
    echo "create ${tar_file} failed!"
    exit 1
  fi

  #md5 file
  md5=`md5sum ${tar_file} | awk '{print $1}'`
  md5file=carrier.${proc_id}.md5
  echo ${md5} > ${md5file}

  #copy tar file and md5file
  if [[ ${is_local} -eq 1 ]]
  then
    cp -f ${tar_file} ${target_dir}/
    cp -f ${md5file}  ${target_dir}/
  else
    echo "ha?"
  fi
  if [[ $? -ne 0 ]]
  then
    exit 1
  fi
  echo "copy ${tar_file} and ${md5file} to ${target_dir} success!"

  #back up
  mv ${tar_file} ${BACK_UP_DIR}/${tar_file}_${curr_time} 
  mv ${md5file} ${BACK_UP_DIR}/${md5file}_${curr_time}

 
  #change dir
  if [[ ${is_local} -eq 1 ]]
  then
    cd ${target_dir}
  else
    echo "ha?"
  fi

  #check md5
  md5_a=`md5sum ${tar_file} | awk '{print $1}'`
  md5_b=`cat ${md5file}`
  if [[ ${md5_a} != ${md5_b} ]]
  then
    echo "${tar_file} not matched!"
    exit 1
  fi

  
  #unzip tar 
  tar -zxvf ${tar_file} 1>/dev/null 2>&1 
  cp ${cfg_path} . 
  echo "unzip ${tar_file} success~"


  echo "try to delete shm..."
  #delete shm
  if [[ ${is_local} -eq 1 ]]
  then
    cd ${target_dir}
    ./deleter -i ${proc_id} -N ${name_space}
  else
    echo "ha?"
  fi

  usleep 500000 
  echo "try to create shm..."
  #create shm
  if [[ ${is_local} -eq 1 ]]
  then
    ./creater -i ${proc_id} -N ${name_space} -r ${recv_size} -s ${send_size}
  else
    echo "ha?"
  fi

  usleep 500000
  echo "try to start carrier..."
  #exe carrier
  if [[ ${is_local} -eq 1 ]]
  then
    echo "name_space:${name_space}"
    ./carrier -i ${proc_id} -N ${name_space} -p ${port} -n ${proc_name} -S
  else
    echo "ha?"
  fi

  #finish
  echo "deploy finish"
}

function update_cfg()
{
  echo "try to update ${name_space}:${proc_name} ${CFG_FILE}"
    
  #check cfg file
  cfg_path="${LOCAL_CFG_DIR}/${proc_name}/${CFG_FILE}"
  if [[ ! -e ${cfg_path} ]]
  then
    echo "${cfg_path} not found!"
    exit 1
  fi 

  target_dir=${dir}/${proc_name}
  #copy cfg file
  if [[ ${is_local} -eq 1 ]]
  then
    cp -f ${cfg_path} ${target_dir}/
  else
    echo "ha?"
  fi
  if [[ $? -ne 0 ]]
  then
    exit 1
  fi
  echo "copy ${cfg_path} to ${target_dir} success!"
}

function reload_cfg()
{
  echo "try to reload cfg of ${name_space}:${proc_name}"
  #reload cfg
  if [[ ${is_local} -eq 1 ]]
  then
    #need grep -v bash which has wrong with '/xx/x/xxxx' arg
    pid=`ps -Ao "pid","command" |grep carrier | grep "\<${name_space}\>" | grep "\<${proc_name}\>" | grep "\<${proc_id}\>" |grep -v 'grep' | grep -v 'bash' |awk '{print $1}'`
    if [[ -z ${pid} ]]
    then
      echo "carrier with ${proc_name} is not running"
    else
      echo "reload to ${pid}"
      kill -s SIGUSR1 ${pid} 
    fi
  else
    echo "ha?"
  fi

  echo "reload finish"
}


function deploy_old()
{
  if [[ $# -lt 6 ]]
  then
    show_help
    exit 1
  fi 

  echo "try to deploy $proc_name to $usr@$ip[$port]:$dir"

  #check cfg file
  cfg_path="${LOCAL_CFG_DIR}/${proc_name}/${CFG_FILE}"  
  if [[ ! -e ${cfg_path} ]]
  then
    echo "cfg file:${cfg_path} not found!"
    exit 1
  fi

  #check binary file
  for file in ${BINARY_FILES[@]}
  do
    if [[ ! -e ${file} ]]
    then
      echo "${file} not exist!"
      exit 1
    fi
  done 

  #create target dir
  target_dir=${dir}/${proc_name}
  if [[ ${is_local} -eq 1 ]]
  then
    mkdir -p ${target_dir}
  else
    echo "ha?"
  fi
  if [[ $? -ne 0 ]]
  then
    exit 1
  fi
  
  #copy cfg file
  if [[ ${is_local} -eq 1 ]]
  then
    cp -f ${cfg_path} ${target_dir}/
  else
    echo "ha?"
  fi
  if [[ $? -ne 0 ]]
  then
    exit 1
  fi
  echo "copy ${cfg_path} to ${target_dir} success!"

  #copy binary file
  for file in ${BINARY_FILES[@]}
  do
    if [[ ${is_local} -eq 1 ]]
    then
      cp ${file} ${target_dir}
      if [[ $? -ne 0 ]]
      then
        exit 1
      fi
    else
     echo "ha?"
    fi
  done
  echo "copy binary files to ${target_dir} success!"

  echo "try to delete shm..."
  #delete shm
  if [[ ${is_local} -eq 1 ]]
  then
    cd ${target_dir}
    ./deleter -i ${proc_id}
  else
    echo "ha?"
  fi

  sleep 1
  echo "try to create shm..."
  #create shm
  if [[ ${is_local} -eq 1 ]]
  then
    ./creater -i ${proc_id}
  else
    echo "ha?"
  fi

  #exe carrier
  if [[ ${is_local} -eq 1 ]]
  then
    ./carrier -i ${proc_id} -n ${port} -S
  else
    echo "ha?"
  fi

  #finish
  echo "deploy finish"
}

function shut_carrier()
{
  pid=`cat "/tmp/.proc_bridge.${name_space}/carrier.${proc_id}.lock"`
  ret=`ps aux | grep carrier | grep ${name_space} | grep "\<${proc_name}\>" | grep ${pid} | grep -v grep | grep -v 'bash'`
  if [[ -z ${ret} ]]
  then
    echo "${proc_name} not runnable!"
    return
  fi

  #kill proc
  if [[ ${is_local} -eq 1 ]]
  then
    kill -s SIGINT ${pid}
  else
    echo "ha?"
  fi

  sleep 1
  #rm shm
  target_dir=${dir}/${proc_name}
  if [[ ${is_local} -eq 1 ]]
  then
    cd ${target_dir}
    ./deleter -i ${proc_id} -N ${name_space}
  else
    echo "ha?"
  fi


  #check
  ret=`ps aux |grep carrier | grep ${name_space} | grep '\<${pid}\>' | grep -v grep`
  if [[ -z ${ret} ]]
  then
    echo "shut ${proc_name} success!"
  else
    echo "shut ${proc_name} failed!"
  fi 
}

function main()
{
  if [[ $# -lt 7 ]]
  then
    show_help
    exit 1
  fi

  proc_name=$1
  proc_id=$2
  ip=$3
  port=$4
  usr=$5
  dir=$6
  opt=$7
  send_size=$8
  recv_size=$9
  name_space=${10} 
 
  #local & remote
  is_local=0
  if [[ ${ip} == "localhost" || ${ip} == "127.0.0.1" ]]
  then
    #echo "{local handle}"
    is_local=1
  else
    echo "{remote handle}"
  fi


  #deploy
  case ${opt} in
  0)
    deploy 
  ;;
  1)
    echo "try to shutdown carrier ${proc_name}[${name_space}:${proc_id}]<${ip}:${port}>"
    shut_carrier
  ;;
  2)
    update_cfg
  ;;
  3)
    reload_cfg
  ;;
  ?)
    echo "other"
  ;;
  esac

}

main $@

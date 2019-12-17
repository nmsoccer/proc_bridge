#!/usr/bin/bash
WORK_DIR=`pwd`
LIB_DIR=../../library
RECVER="recver"
SENDER="sender"

SEND_FILE="./data.send"
RECV_FILE="./data.recv"
TMP_FILE="./tmp"

#compile
gcc -g -Wall press_sender.c -lproc_bridge -lslog -lstlv -lm -lrt -o ${SENDER}
gcc -g -Wall press_recver.c -lproc_bridge -lslog -lstlv -lm -lrt -o ${RECVER}
if [[ $? -ne 0 ]]
then
  echo "compilng failed! please enter ${LIB_DIR} and exec 'install_lib.sh' to install library first!"
  exit 1
fi

function generate_files() 
{
  #create test files
  : > $TMP_FILE
  : > $RECV_FILE
  : > $SEND_FILE
  count=1
  while [[ $count -le 4096 ]]
  do
    date +"%s%N" | md5sum | awk '{print $1}' >> $TMP_FILE
    let count=count+1 
  done
  cat $TMP_FILE | xargs -n 3 > $SEND_FILE
  rm $TMP_FILE
  echo "create $SEND_FILE finish"
}

if [[ ! -e $SEND_FILE ]] 
then
  echo "create $SEND_FILE"
  generate_files  
fi


 
#done
echo "====================================="
echo "make success"
#echo ">>exe './${TARGET} -i 30000 -t 20000 -N echo -s &'"
echo ">>exe './${RECVER}  -f $RECV_FILE -P XX(128) -S YY &'"
echo ">>exe './${SENDER} -P XX(128) -f $SEND_FILE'"
echo "then to test press..."
echo "====================================="

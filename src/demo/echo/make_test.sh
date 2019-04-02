#!/usr/bin/bash
WORK_DIR=`pwd`
LIB_DIR=../../library
TARGET="tester"


#compile
gcc -g -Wall test_proc.c -lproc_bridge -lslog -lstlv -lm -lrt -o ${TARGET}
if [[ $? -ne 0 ]]
then
  echo "compilng failed! please enter ${LIB_DIR} and exec 'install_lib.sh' to install library first!"
  exit 1
fi
 
#done
echo "====================================="
echo "make success"
echo ">>exe './${TARGET} -i 30000 -N echo -s &'"
echo ">>exe './${TARGET} -i 20000 -t 30000 -N echo -c'"
echo "then to test echo..."
echo "====================================="

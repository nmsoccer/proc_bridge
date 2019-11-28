package main 

import (
	"fmt"
	bridge "test_c/clib/proc_bridge"
	"flag"
	"time"
	"os"	
)

var proc_id = flag.Int("i", 0 , "proc_id");
var serv_id = flag.Int("t", 0, "server proc_id");
var ns = flag.String("N", "", "name space");

func recv_msg(bd int) {
	recv_buff := make([]byte , 256);
	var recved int = 0;
	var sender int = 0;
	
	for {
		recved = 0;
		sender = 0;
		recv_buff = recv_buff[:cap(recv_buff)];
		
		recved = bridge.RecvBridge(bd, recv_buff, cap(recv_buff), &sender, -1);
		if recved > 0 {
			fmt.Printf("[%d] recved:%d sender:%d content:%s\n", time.Now().UnixNano()/1000000 , recved , sender , recv_buff[:recved]);
		} else {
			//fmt.Printf("recved:%d\n", recved);
			time.Sleep(1e6);
		}
				
	}
}

func main() {
	flag.Parse();
	//arg
	if *proc_id<=0 || *serv_id<=0 || *ns=="" {
		flag.PrintDefaults();
		return;
	}
	fmt.Printf("proc_id:%d serv_id:%d ns:%s\n", *proc_id , *serv_id , *ns);
	
	//open bridge
	bd := bridge.OpenBridge(*ns , *proc_id , -1);
	if bd < 0 {
		fmt.Printf("open bridge failed!\n");
		return;
	}
	fmt.Printf("open bd:%d\n", bd);
	
	//recv msg
	go recv_msg(bd);
	
	//get input
	count := 0;
	buff := make([]byte , 128);
	result := 0;
	for {
		fmt.Printf("please input>>");
		count , _ = os.Stdin.Read(buff);
		buff = buff[:count-1]; //trip \n
		//fmt.Printf("read:%s count:%d\n", string(buff) , count);
		if string(buff) == "exit" {
			fmt.Printf("byte\n");
			break;
		}
		time.Sleep(1e9);
		
		//send
		count -= 1;
		if count > 0 {
			result = bridge.SendBridge(bd, *serv_id, buff, count);
			fmt.Printf("[%d] send result:%d! %d bytes!\n", time.Now().UnixNano()/1000000 , result , count);
			if result != 0 {
				fmt.Printf("send failed!\n");
			}
			time.Sleep(10e8);
		}
		
		buff = buff[:cap(buff)];				
	}
	return;
}


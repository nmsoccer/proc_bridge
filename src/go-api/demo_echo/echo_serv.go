package main 

import (
	"fmt"
	bridge "test_c/clib/proc_bridge"
	//slog "test_c/clib/slog"
	"flag"
	"time"
)

var proc_id = flag.Int("i", 0 , "proc_id");
var ns = flag.String("N", "", "name space");
var exit_ch = make(chan int);

func handle_pkg(bd int) {
	recv_buff := make([]byte , 128);
	recved := 0;
	var sender int = 0;
	
	snd_buff := make([]byte , 128);
	var snd_res int = 0;
	for {
		recved = bridge.RecvBridge(bd, recv_buff, cap(recv_buff), &sender, -1);
		switch {
			case recved > 0:
				fmt.Printf("recved:%d sender:%d content:%s\n", recved , sender , string(recv_buff[:recved]));
				
				//echo
				snd_buff = append(snd_buff[:0] , "[server:]"...);
				snd_buff = append(snd_buff , recv_buff[:recved]...);
				snd_res = bridge.SendBridge(bd, sender, snd_buff, len(snd_buff));
				fmt.Printf("[%d] send %s. result:%d len:%d\n", time.Now().UnixNano()/1000000 , string(snd_buff) , snd_res , len(snd_buff));
			default:
				//fmt.Printf("recve:%d\n" , recved);
				time.Sleep(1e6);
		} 
		
		recv_buff = recv_buff[:cap(recv_buff)];
	}
	
	exit_ch <- 1;
}

func main() {
	fmt.Printf("starts...\n");
	flag.Parse();

	//parse arg
	fmt.Printf("proc:%d name_space:%s\n", *proc_id , *ns);
	if *proc_id<=0 || *ns=="" {
		flag.PrintDefaults();
		return;
	}
	
	//open slog[optional]
	var sld int = -1;
	/*
	sld := slog.SLogLocalOpen(slog.SL_DEBUG , "proc_serv.log" , slog.SLF_PREFIX , slog.SLD_SEC , 10*1024*1024 , 5);
	if sld < 0 {
		fmt.Printf("open slog failed!\n");
	}*/
	
	//open bridge
	bd := bridge.OpenBridge(*ns, *proc_id, sld);
	if bd < 0 {
		fmt.Printf("open bridge failed!\n");
		return;
	}
	fmt.Printf("bridge:%d\n",  bd);
	
	//handle
	go handle_pkg(bd); 
	
	//wait for exit
	<- exit_ch;
}


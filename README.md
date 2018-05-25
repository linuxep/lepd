# lepd
server daemon of LEP

## LepD是什么


LEP的结构采用的是Client/Server的模型， Client端是Django+Web服务器，负责显示从Server端取得的数据。 Server端是我们最终需要profile的target。 LepD运行在Server(Target)端, 它的工作如下：


是一个JsonRpcServer, Client通过JsonRpc连接
LepD会解析接收到的RPC命令，取得系统的各类不同的数据信息，比如内存，CPU，IO状态
将取得的结果，转换成Json格式返回给Client

## LepD应用


LepvClient通过JsonRpc连接

简单测试时，通过Linux nc命令即可

发送"ListAllMethod?"这个rpc方法，可以获得LepD所支持的所有方法

root@bob-VirtualBox:~# echo '{\"method\":\"ListAllMethod\"}' | nc <lepd IP地址> 12307

{

	"result":	"SayHello ListAllMethod GetProcMeminfo GetProcLoadavg GetProcVmstat GetProcZoneinfo GetProcBuddyinfo GetProcCpuinfo GetProcSlabinfo GetProcSwaps GetProcInterrupts GetProcSoftirqs GetProcDiskstats GetProcVersion GetProcStat GetProcModules GetCmdFree GetCmdProcrank GetCmdIostat GetCmdVmstat GetCmdTop GetCmdTopH GetCmdIotop GetCmdSmem GetCmdDmesg lepdendstring"
}

root@bob-VirtualBox:~#

具体某个方法，比如想获得内存的信息

root@bob-VirtualBox:~# echo '{\"method\":\"GetProcMeminfo\"}' | nc <lepd IP地址> 12307

{

	"result":	"MemTotal:        1017788 kB\nMemFree:          426560 kB\nBuffers:           77080 kB\nCached:           231968 kB\nSwapCached:            0 kB\nActive:           342212 kB\nInactive:         172488 kB\nActive(anon):     207000 kB\nInactive(anon):    11256 kB\nActive(file):     135212 kB\nInactive(file):   161232 kB\nUnevictable:           0 kB\nMlocked:               0 kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\nDirty:                92 kB\nWriteback:             0 kB\nAnonPages:        205652 kB\nMapped:            40480 kB\nShmem:             12608 kB\nSlab:              50868 kB\nSReclaimable:      39328 kB\nSUnreclaim:        11540 kB\nKernelStack:        1336 kB\nPageTables:         6768 kB\nNFS_Unstable:          0 kB\nBounce:                0 kB\nWritebackTmp:          0 kB\nCommitLimit:      508892 kB\nCommitted_AS:     947444 kB\nVmallocTotal:   34359738367 kB\nVmallocUsed:        8796 kB\nVmallocChunk:   34359721724 kB\nHardwareCorrupted:     0 kB\nAnonHugePages:     67584 kB\nHugePages_Total:       0\nHugePages_Free:        0\nHugePages_Rsvd:        0\nHugePages_Surp:        0\nHugepagesize:       2048 kB\nDirectMap4k:       63360 kB\nDirectMap2M:      985088 kB\nDirectMap1G:           0 kB\nlepdendstring"
}

root@bob-VirtualBox:~# 

当发现LepD连不上，或者获得不到数据时，直接Kill掉lepd进程，然后再通过如下命令把LepD重启
git@iZ22ngfe4n3Z:~$ /opt/deploy_lepv/lepd &

## 如何编译

root@bob-VirtualBox:~#  git clone https://github.com/linuxep/lepd

For X86（电脑需要安装libev-dev库）:

电脑需要提前安装：

{

	apt-get install libev-dev 
	apt-get install linux-tools-common linux-tools-generic linux-tools-`uname -r`
	apt-get install libncurses5-dev
}

root@bob-VirtualBox:~/lepd-src# make

For ARM（电脑需要安装arm-linux-gnueabi-gcc）:

root@bob-VirtualBox:~/lepd-src# make ARCH=arm 

编译的过程中你可能会遇到如下问题：

lepd uses VFP register arguments, lepd/libs/arm-libev/libev.a(ev.o) does not

这可能是因为你的工具链加了 -mfloat-abi=hard

如果基线不允许修改这个参数，那么你可以自行编译libev（我是这么做的）

libev可以通过如下方式下载：

git clone https://github.com/enki/libev.git

关于编译libev我这里手顺是：

./configure --prefix=/home/xxx/elp/ --host=arm （这之前我source了yocto的sdk脚本environment-setup-cortexa9hf-neon-poky-linux-gnueabi）

make

然后cp libev.a 到 lepd/libs/arm-libev/libev.a

继续完成lepd编译


## 如何运行

运行lepd需要root权限，因为系统有些proc文件无root权限无法读取。

lepd运行的板子、服务器的内核需要使能TASKSTATS，这样IOTOP的功能才可以起来：
General setup --->

CPU/Task time and stats accounting --->

[*] Export task/process statistics through netlink

[*] Enable per-task delay accounting

[*] Enable extended accounting over taskstats

[*] Enable per-task storage I/O accounting

目前,LEPD还没有集成perf的功能，因此，要求目标平台上，还是有安装perf，内核也使能perf相关的支持。

## 实现

用了cJSON和jsonrpc-c

在server.c里面解析rpc方法，结果转成Json格式的字符串返回给Client


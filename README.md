# lepd

server daemon of LEP


## LepD 是什么


LEP 的结构采用的是 Client/Server 的模型， Client 端是 Django+Web 服务器，负责显示从 Server 端取得的数据。 Server 端是我们最终需要 profile 的 target。 LepD 运行在 Server（Target）端，它的工作如下：

- 是一个 JsonRpcServer，Client 通过 JsonRpc 连接
- LepD 会解析接收到的 RPC 命令，取得系统的各类不同的数据信息，比如内存、CPU、IO 状态
- 将取得的结果，转换成 Json 格式返回给 Client

## LepD 应用


LepvClient 通过 JsonRpc 连接

简单测试时，通过 Linux 的 `nc` 命令即可

发送"ListAllMethod?"这个 rpc 方法，可以获得 LepD 所支持的所有方法。如：

```console
root@bob-VirtualBox:~# echo "{\"method\":\"ListAllMethod\"}" | nc <lepd IP地址> 12307
```

返回：

```json
{
"result":	"SayHello ListAllMethod GetProcMeminfo GetProcLoadavg GetProcVmstat GetProcZoneinfo GetProcBuddyinfo GetProcCpuinfo GetProcSlabinfo GetProcSwaps GetProcInterrupts GetProcSoftirqs GetProcDiskstats GetProcVersion GetProcStat GetProcModules GetCmdFree GetCmdProcrank GetCmdIostat GetCmdVmstat GetCmdTop GetCmdTopH GetCmdIotop GetCmdSmem GetCmdDmesg lepdendstring"
}
```

具体某个方法，比如想获得内存的信息：

```console
root@bob-VirtualBox:~# echo "{\"method\":\"GetProcMeminfo\"}" | nc <lepd IP地址> 12307
```

返回：

```json
{
"result":	"MemTotal:        1017788 kB\nMemFree:          426560 kB\nBuffers:           77080 kB\nCached:           231968 kB\nSwapCached:            0 kB\nActive:           342212 kB\nInactive:         172488 kB\nActive(anon):     207000 kB\nInactive(anon):    11256 kB\nActive(file):     135212 kB\nInactive(file):   161232 kB\nUnevictable:           0 kB\nMlocked:               0 kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\nDirty:                92 kB\nWriteback:             0 kB\nAnonPages:        205652 kB\nMapped:            40480 kB\nShmem:             12608 kB\nSlab:              50868 kB\nSReclaimable:      39328 kB\nSUnreclaim:        11540 kB\nKernelStack:        1336 kB\nPageTables:         6768 kB\nNFS_Unstable:          0 kB\nBounce:                0 kB\nWritebackTmp:          0 kB\nCommitLimit:      508892 kB\nCommitted_AS:     947444 kB\nVmallocTotal:   34359738367 kB\nVmallocUsed:        8796 kB\nVmallocChunk:   34359721724 kB\nHardwareCorrupted:     0 kB\nAnonHugePages:     67584 kB\nHugePages_Total:       0\nHugePages_Free:        0\nHugePages_Rsvd:        0\nHugePages_Surp:        0\nHugepagesize:       2048 kB\nDirectMap4k:       63360 kB\nDirectMap2M:      985088 kB\nDirectMap1G:           0 kB\nlepdendstring"
}
```

当发现 LepD 连不上，或者获得不到数据时，直接 Kill 掉 lepd 进程，然后再通过如下命令把 LepD 重启

```console
git@iZ22ngfe4n3Z:~$ /opt/deploy_lepv/lepd &
```


## 如何编译

```console
root@bob-VirtualBox:~#  git clone https://github.com/linuxep/lepd
```

For X86（电脑需要安装 libev-dev 库）:

电脑需要提前安装：

```shell
apt-get install libev-dev 
apt-get install linux-tools-common linux-tools-generic linux-tools-`uname -r`
apt-get install libncurses5-dev
```

然后编译：

```console
root@bob-VirtualBox:~/lepd-src# make
```

For ARM（电脑需要安装 arm-linux-gnueabi-gcc）:

```console
root@bob-VirtualBox:~/lepd-src# make ARCH=arm 
```


## 如何运行

运行 lepd 需要 root 权限，因为系统有些 proc 文件无 root 权限无法读取。

lepd 运行的板子、服务器的内核需要使能 TASKSTATS，这样 IOTOP 的功能才可以起来：

```
General setup --->
    CPU/Task time and stats accounting --->
    [*] Export task/process statistics through netlink
    [*] Enable per-task delay accounting
    [*] Enable extended accounting over taskstats
    [*] Enable per-task storage I/O accounting
```

目前，LEPD 还没有集成 perf 的功能，因此，要求目标平台上，还是有安装 perf，内核也使能 perf 相关的支持。

**如果要在浏览器中支持火焰图，也需要 lepd 运行的目标平台上支持了 perf!**

## 实现

用了 cJSON 和 jsonrpc-c

在 server.c 里面解析 rpc 方法，结果转成 Json 格式的字符串返回给 Client。


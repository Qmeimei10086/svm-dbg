# svm-dbg
基于npt-hook的amd平台调试体系重建工具
# 效果:
效果：  
![img2](https://github.com/Qmeimei10086/qmeimei10086.github.io/blob/main/img/2026-9-1-blog-show1.png?raw=true "img2")  
这是普通调试器  
![img3](https://github.com/Qmeimei10086/qmeimei10086.github.io/blob/main/img/2026-9-1-blog-show2.png?raw=true "img3")  
经过测试可以调试全版本启动了反调试的vmp   
至于调试某游戏，我没试过  
# 环境
目前只测试过20h1版本虚拟机，请勿在实体机使用  
虚拟机推荐4-8核，核太少带不动npt hook，太多核dpc调度容易出问题，内存4G以上，太少会有部分关键函数被换入分页文件，在hook时引发蓝屏  
# 注意
尽可能再刚开机时加载程序，减少内存被换入分页文件的概率  
启动有概率刚好被pg发现，引发蓝屏  
启动创建调试会hook几个高频函数，可能增加蓝屏几率和系统卡顿  
运行时将编译好的svm-dbg.exe Amd-V-ReloadDbg.sys 和 dbghelp.dll symsrv.dll 放一起，然后选择管理员身份启动，最好先开一个调试器再启动svm-dbg  
启动时直接选择按f8允许加载未签名驱动就行，不要连接windbg，不然异常会先被windbg接管
# 未来目标
优化npt hook代码，增加运行速率并减少蓝屏可能性  
添加int3无痕断点功能  
添加调试器白名单  
# 参考
https://github.com/Liu-Zhiying/StartAMDVHookDriverFromNone  
https://github.com/xyddnljydd/vt-ReloadDbg  


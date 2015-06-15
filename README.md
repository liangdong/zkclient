# zkclient
c++ wrapper for libzookeeper async api

保留了一些assert, 用于在生产环境中发现一些没有预期到的state/event，不过从zookeeper c client源码来看，其他未assert的状态不应出现。


搞清楚zookeeper client的一些细节，需要自己看zookeeper client源码，重点关注几个问题：

1，async接口回调失败错误码，watch是否可能生效？
答：否，async接口的watch虽然提交给了server，但最终决定是否生效的决定点在于response里的错误码，只有response里标记ZOK，此时才会active这个watch。
（看源码，关注activateWatcher这个函数）
2，watch通知session event怎么处理？
答：整个zhandle上的watch在会话（和zk的连接）出现问题的情况下，都会被通知session event的，我们只需要关注zookeeper_init的那个watch就OK了，其他都是重复的。
另外，只要是当前生效的watch，都会在重连zk后发起一次setWatch的request，都会重新恢复起来的。这里的恢复也不是简单的重新注册上，是肯定会根据客户端和服务端的版本变化马上对比，如果有差异马上会通知，这一点对用户完全透明。
3，async接口是否有超时控制？
答：没有，其实zookeeper client只是和一个zk server保持一条连接，通过poll调用完成收发，并且对每个request的response何时回来并不在意，所以如果zk server卡住了，那么client的异步请求将不会回调。另外，zk client和zk server只有ping包的，但是并没有什么真的卵用，只是在服务端太久没动静的情况下打印一个exceed deadline之类的日志。最重要的，zk client只会当与zk server连接异常的情况下才断开连接重连，当然排队的async操作都会被回调CONNECTION LOSS/OPERATION TIMEOUT之类的错误，既然一旦出现问题就断开连接并回调用户失败了，那么自然也不用担心这些请求的watch会莫名其妙的过几秒回调了，这一点可以看一下问题1）中的解释，联系起来看。

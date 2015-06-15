# zkclient
c++ wrapper for libzookeeper async api

保留了一些assert, 用于在生产环境中发现一些没有预期到的state/event，不过从zookeeper c client源码来看，其他未assert的状态不应出现。

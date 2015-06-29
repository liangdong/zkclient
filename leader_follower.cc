/*
 * leader_follower.cc
 *
 *  Created on: 2015年6月16日
 *      Author: Administrator
 */


/**
 *
 * 	实现主从模式，集群只有1个节点为master，其他成为slave.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <vector>
#include <algorithm>
#include "zkclient.h"

namespace {
	bool leader = false;
	char node_id[1024];
}

void NodeGetNodeHandler(ZKErrorCode errcode, const std::string& path, const char* value, int value_len, void* context) {
	if (errcode == kZKSucceed) {
		if (!leader && strncmp(node_id, value, value_len) == 0) { // leader节点
			leader = true;
			printf("I am Leader - path=%s node_id=%.*s\n", path.c_str(), value_len, value);
		} else if (!leader) { // follower节点
			printf("I am Follower - leader=%s node_id=%.*s\n", path.c_str(), value_len, value);
		}
	} else if (errcode == kZKNotExist) {
		// leader节点下线了，没关系，GetChildren一定会触发watch通知我们这个变化
	} else {
		ZKClient::GetInstance().GetNode(path, NodeGetNodeHandler, NULL); // no watch
	}
}

void NodeGetChildrenHandler(ZKErrorCode errcode, const std::string& path, int count, char** data, void* context) {
	if (errcode == kZKSucceed) {
		if (!leader) { // 在follower状态才需要关注leader变化，在leader状态无需关注
			assert(count); // 无人为干预, 至少应该有自己在线

			std::vector<std::string> nodes(data, data + count);
			std::sort(nodes.begin(), nodes.end());

			std::string leader_node("/leader_follower/");
			leader_node.append(nodes[0]);

			// 获取leader节点的value，与node_id比较确认是否自己成为leader。
			ZKClient::GetInstance().GetNode(leader_node, NodeGetNodeHandler, NULL); // no watch
		} else {
			printf("I am already Leader, just new node found - count=%d\n", count);
		}
	} else if (errcode == kZKDeleted) {
		assert(false);
	} else if (errcode == kZKError) {
		// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
		ZKClient::GetInstance().GetChildren("/leader_follower", NodeGetChildrenHandler, NULL, true);
	} else if (errcode == kZKNotExist) {
		assert(false);
	}
}

void NodeCreateHandler(ZKErrorCode errcode, const std::string& path, const std::string& value, void* context) {
	if (errcode == kZKSucceed) {
		printf("I'm online - path=%s id=%s\n", value.c_str(), node_id);
		// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
		ZKClient::GetInstance().GetChildren("/leader_follower", NodeGetChildrenHandler, NULL, true);
	} else if (errcode == kZKError) {
		// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
		ZKClient::GetInstance().Create("/leader_follower/node-", node_id, ZOO_EPHEMERAL | ZOO_SEQUENCE, NodeCreateHandler, NULL);
	} else if (errcode == kZKNotExist) {
		fprintf(stderr, "You need to create /leader-follower\n");
		exit(-1);
	} else if (errcode == kZKExisted) {
		assert(false);
	}
}

int main(int argc, char** argv) {
	ZKClient& zkclient = ZKClient::GetInstance();
	if (!zkclient.Init("127.0.0.1:3000,127.0.0.1:3001,127.0.0.1:3002", 10000, NULL, NULL, true, "./leader_follower.log")) {
		fprintf(stderr, "ZKClient failed to init...\n");
		return -1;
	}
	// 生成唯一进程标示ID，作为临时节点的value，用途：
	// 可能Create临时节点成功了而客户端认为失败，这样客户端重试会又建出一个临时节点，
	// 如果仅仅依靠Create返回的临时节点名字作为判断是自身的依据，则可能导致该进程占据了leader的位置而自己又认为自己不是leader的极端情况，
	// 所以不能依赖唯一临时路径标示自己，而是要使用路径的value唯一性来确认自身身份。
	// 
	// 注: 在分布式系统里，通过使用gethostname()来唯一标示一个物理节点.
	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand((unsigned)time(NULL));
	snprintf(node_id, sizeof(node_id), "%ld-%ld-%d-%d", tv.tv_sec, tv.tv_usec, getpid(), rand());

	// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
	zkclient.Create("/leader_follower/node-", node_id, ZOO_EPHEMERAL | ZOO_SEQUENCE, NodeCreateHandler, NULL);

	while (true) {
		sleep(1);
	}
	return 0;
}

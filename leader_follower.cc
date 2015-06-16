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
#include <assert.h>
#include <vector>
#include <algorithm>
#include "zkclient.h"

namespace {
	bool leader = false;
	std::string node;
}

void NodeGetChildrenHandler(ZKErrorCode errcode, const std::string& path, int count, char** data, void* context) {
	if (errcode == kZKSucceed) {
		assert(count); // 无人为干预, 至少应该有自己在线

		std::vector<std::string> nodes(data, data + count);
		std::sort(nodes.begin(), nodes.end());

		std::string leader_node("/leader_follower/");
		leader_node.append(nodes[0]);

		if (leader_node == node) {
			leader = true;
			printf("I'm leader - %s\n", node.c_str());
		} else {
			printf("I'm follower - %s, leader is %s\n", node.c_str(), leader_node.c_str());
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
		node = value;
		printf("I'm online - %s\n", node.c_str());
		// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
		ZKClient::GetInstance().GetChildren("/leader_follower", NodeGetChildrenHandler, NULL, true);
	} else if (errcode == kZKError) {
		// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
		ZKClient::GetInstance().Create("/leader_follower/node-", "HELLO WORLD", ZOO_EPHEMERAL | ZOO_SEQUENCE, NodeCreateHandler, NULL);
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

	// 不需要检查返回值，除非SESSION_EXPIRED才会失败，那也会被回调退出程序的
	zkclient.Create("/leader_follower/node-", "HELLO WORLD", ZOO_EPHEMERAL | ZOO_SEQUENCE, NodeCreateHandler, NULL);

	while (true) {
		sleep(1);
	}
	return 0;
}

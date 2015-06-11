/*
 * test.cc
 *
 *  Created on: 2015年6月10日
 *      Author: Administrator
 */

#include <stdio.h>
#include <unistd.h>
#include "zkclient.h"

void TestGetNodeHandler(ZKErrorCode errcode, const std::string& path, const char* value, int value_len) {
	if (errcode == kZKSucceed) {
		printf("[kZKSucceed] path=%s value=%.*s\n", path.c_str(), value_len, value);
	} else if (errcode == kZKDeleted) {
		printf("[kZKDeleted] path=%s\n", path.c_str());
	} else if (errcode == kZKError) {
		printf("[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("[kZKNotExist] path=%s\n", path.c_str());
	}
}

int main(int argc, char** argv) {
	ZKClient& zkclient = ZKClient::GetInstance();
	if (!zkclient.Init("127.0.0.1:3000,127.0.0.1:3001,127.0.0.1:3002", 10000)) {
		fprintf(stderr, "ZKClient failed to init...\n");
		return false;
	}

	bool succeed = zkclient.GetNode("/test", TestGetNodeHandler, NULL, true);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to GetNode...\n");
		return false;
	}
	while (true) {
		sleep(1);
	}
	return 0;
}

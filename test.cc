/*
 * test.cc
 *
 *  Created on: 2015年6月10日
 *      Author: Administrator
 */

#include <stdio.h>
#include <unistd.h>
#include "zkclient.h"

void TestGetNodeHandler(ZKErrorCode errcode, const std::string& path, const char* value, int value_len, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestGetNodeHandler-[kZKSucceed] path=%s value=%.*s\n", path.c_str(), value_len, value);
	} else if (errcode == kZKDeleted) {
		printf("TestGetNodeHandler-[kZKDeleted] path=%s\n", path.c_str());
	} else if (errcode == kZKError) {
		printf("TestGetNodeHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestGetNodeHandler-[kZKNotExist] path=%s\n", path.c_str());
	}
}

void TestGetChildrenHandler(ZKErrorCode errcode, const std::string& path, int count, char** data, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestGetChildrenHandler-[kZKSucceed] path=%s count=%d\n", path.c_str(), count);
	} else if (errcode == kZKDeleted) {
		printf("TestGetChildrenHandler-[kZKDeleted] path=%s\n", path.c_str());
	} else if (errcode == kZKError) {
		printf("TestGetChildrenHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestGetChildrenHandler-[kZKNotExist] path=%s\n", path.c_str());
	}
}

void TestExistHandler(ZKErrorCode errcode, const std::string& path, const struct Stat* stat, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestExistHandler-[kZKSucceed] path=%s version=%d\n", path.c_str(), stat->version);
	} else if (errcode == kZKDeleted) {
		printf("TestExistHandler-[kZKDeleted] path=%s\n", path.c_str());
	} else if (errcode == kZKError) {
		printf("TestExistHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestExistHandler-[kZKNotExist] path=%s\n", path.c_str());
	}
}

void TestCreateHandler(ZKErrorCode errcode, const std::string& path, const std::string& value, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestCreateHandler-[kZKSucceed] path=%s value=%s\n", path.c_str(), value.c_str());
	} else if (errcode == kZKError) {
		printf("TestCreateHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestCreateHandler-[kZKNotExist] path=%s\n", path.c_str());
	} else if (errcode == kZKExisted) {
		printf("TestCreateHandler-[kZKExisted] path=%s\n", path.c_str());
	}
}

void TestSetHandler(ZKErrorCode errcode, const std::string& path, const struct Stat* stat, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestSetHandler-[kZKSucceed] path=%s version=%d\n", path.c_str(), stat->version);
	} else if (errcode == kZKError) {
		printf("TestSetHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestSetHandler-[kZKNotExist] path=%s\n", path.c_str());
	}
}

void TestDeleteHandler(ZKErrorCode errcode, const std::string& path, void* context) {
	if (errcode == kZKSucceed) {
		printf("TestDeleteHandler-[kZKSucceed] path=%s\n", path.c_str());
	} else if (errcode == kZKError) {
		printf("TestDeleteHandler-[kZKError] path=%s\n", path.c_str());
	} else if (errcode == kZKNotExist) {
		printf("TestDeleteHandler-[kZKNotExist] path=%s\n", path.c_str());
	} else if (errcode == kZKNotEmpty) {
		printf("TestDeleteHandler-[kZKNotEmpty] path=%s\n", path.c_str());
	}
}

int main(int argc, char** argv) {
	ZKClient& zkclient = ZKClient::GetInstance();
	if (!zkclient.Init("127.0.0.1:3000,127.0.0.1:3001,127.0.0.1:3002", 10000)) {
		fprintf(stderr, "ZKClient failed to init...\n");
		return -1;
	}

	bool succeed = zkclient.GetNode("/test", TestGetNodeHandler, NULL, true);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to GetNode...\n");
		return -1;
	}

	succeed = zkclient.GetChildren("/test", TestGetChildrenHandler, NULL, true);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to GetChildren...\n");
		return -1;
	}

	succeed = zkclient.Exist("/test", TestExistHandler, NULL, true);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to Exist...\n");
		return -1;
	}

	succeed = zkclient.Create("/test/node-", "HELLO WORLD", ZOO_EPHEMERAL | ZOO_SEQUENCE, TestCreateHandler, NULL);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to Create...\n");
		return -1;
	}

	succeed = zkclient.Set("/test", "set by zkclient~~", TestSetHandler, NULL);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to Set...\n");
		return -1;
	}

	succeed = zkclient.Delete("/test", TestDeleteHandler, NULL);
	if (!succeed) {
		fprintf(stderr, "ZKClient failed to Delete...\n");
		return -1;
	}

	while (true) {
		sleep(1);
	}
	return 0;
}

/*
 * zkclient.h
 *
 *  Created on: 2015年6月10日
 *      Author: Administrator
 */

#ifndef ZK_ZKCLIENT_H_
#define ZK_ZKCLIENT_H_

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <map>
#include "zookeeper.h"

class ZKClient;

enum ZKErrorCode {
	kZKSucceed= 0, // 操作成功
	kZKNotExist, // 节点不存在
	kZKError, // 请求失败, 注册的watch会被取消
	kZKDeleted, // 节点删除
	kZKChanged, // 节点数据变动
};

typedef void (*SessionExpiredHandler)(void* context);
typedef void (*GetNodeHandler)(ZKErrorCode errcode, const std::string& path, const char* value, int value_len, void* context);
typedef void (*GetChildrenHandler)(ZKErrorCode errcode, const std::string& path, int count, char** data, void* context);
typedef void (*ExistHandler)(ZKErrorCode errcode, const std::string& path, const struct Stat* stat, void* context);

struct ZKWatchContext {
	ZKWatchContext(const std::string& path, void* context, ZKClient* zkclient, bool watch);

	bool watch;
	void* context;
	std::string path;
	ZKClient* zkclient;
	union {
		GetNodeHandler getnode_handler;
		GetChildrenHandler getchildren_handler;
		ExistHandler exist_handler;
	};
};

class ZKClient {
public:
	static ZKClient& GetInstance();

	~ZKClient();

	bool Init(const std::string& host, int timeout, SessionExpiredHandler expired_handler = NULL, void* context = NULL,
			 bool debug = false, const std::string& zklog = "");

	bool GetNode(const std::string& path, GetNodeHandler handler, void* context, bool watch = false);

	bool GetChildren(const std::string& path, GetChildrenHandler handler, void* context, bool watch = false);

	bool Exist(const std::string& path, ExistHandler handler, void* context, bool watch = false);

private:
	static void NewInstance();
	static ZKClient& GetClient();

	static void DefaultSessionExpiredHandler(void* context);
	static void SessionWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcher_ctx);
	static void* SessionCheckThreadMain(void* arg);

	// GetNode的zk回调处理
	static void GetNodeDataCompletion(int rc, const char* value, int value_len,
	        const struct Stat* stat, const void* data);
	static void GetNodeWatcher(zhandle_t* zh, int type, int state, const char* path,void* watcher_ctx);

	// GetChildren的zk回调处理
	static void GetChildrenStringCompletion(int rc, const struct String_vector* strings, const void* data);
	static void GetChildrenWatcher(zhandle_t* zh, int type, int state, const char* path,void* watcher_ctx);

	// Exist的zk回调处理
	static void ExistCompletion(int rc, const struct Stat* stat, const void* data);
	static void ExistWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcher_ctx);

	void UpdateSessionState(int state);
	void CheckSessionState();

	int64_t GetCurrentMs();

	ZKClient();
	ZKClient(const ZKClient&);
	ZKClient& operator=(const ZKClient&);

	static pthread_once_t new_instance_once_;

	zhandle_t* zhandle_;
	FILE* log_fp_;
	SessionExpiredHandler expired_handler_;
	void* user_context_;

	// ZK会话状态
	int session_state_;
	int session_timeout_;
	int64_t session_disconnect_ms_;
	pthread_mutex_t state_mutex_;
	pthread_cond_t state_cond_;

	// ZK会话状态检测线程（由于zk精确到毫秒，所以毫秒级间隔check）
	bool session_check_running_;
	pthread_t session_check_tid_;
};


#endif /* ZK_ZKCLIENT_H_ */

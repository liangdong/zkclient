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
#include <vector>
#include "zookeeper.h"

/**
 *		对于注册了Watch的操作，严格根据下列返回码来区分watch是否失效。
 *
 *		一旦Watch失效，用户需自行决定是否，以及何时发起新的操作，ZKClient内部不会帮用户做决策，
 *		尤其体现在一个操作因为Connection loss或者Connection timeout错误码而失败的情况下，ZKClient
 *		也并没有做什么特殊处理，而是直接反馈给用户自己决策是否以及何时发起下一次操作。
 *
 */

class ZKClient;

enum ZKErrorCode {
	kZKSucceed= 0, // 操作成功，watch继续生效
	kZKNotExist, // 节点不存在，对于exist操作watch继续生效，其他操作均失效
	kZKError, // 请求失败, watch失效
	kZKDeleted, // 节点删除，watch失效
	kZKExisted, // 节点已存在，Create失败
	kZKNotEmpty // 节点有子节点，Delete失败
};

// 节点类型引用zookeeper原生定义
//enum ZKNodeType {
//	ZOO_EPHEMERAL,
//	ZOO_SEQUENCE,
//};

typedef void (*SessionExpiredHandler)(void* context);
typedef void (*GetNodeHandler)(ZKErrorCode errcode, const std::string& path, const char* value, int value_len, void* context);
typedef void (*GetChildrenHandler)(ZKErrorCode errcode, const std::string& path, int count, char** data, void* context);
typedef void (*ExistHandler)(ZKErrorCode errcode, const std::string& path, const struct Stat* stat, void* context);
typedef void (*CreateHandler)(ZKErrorCode errcode, const std::string& path, const std::string& value, void* context);
typedef void (*SetHandler)(ZKErrorCode errcode, const std::string& path, const struct Stat* stat, void* context);
typedef void (*DeleteHandler)(ZKErrorCode errcode, const std::string& path, void* context);

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
		CreateHandler create_handler;
		SetHandler set_handler;
		DeleteHandler delete_handler;
	};
};

class ZKClient {
public:
	static ZKClient& GetInstance();

	~ZKClient();

	bool Init(const std::string& host, int timeout, SessionExpiredHandler expired_handler = NULL, void* context = NULL,
			 bool debug = false, const std::string& zklog = "");

	/* async api */
	bool GetNode(const std::string& path, GetNodeHandler handler, void* context, bool watch = false);

	bool GetChildren(const std::string& path, GetChildrenHandler handler, void* context, bool watch = false);

	bool Exist(const std::string& path, ExistHandler handler, void* context, bool watch = false);

	bool Create(const std::string& path, const std::string& value, int flags, CreateHandler handler, void* context);

	bool Set(const std::string& path, const std::string& value, SetHandler handler, void* context);

	bool Delete(const std::string& path, DeleteHandler handler, void* context);

	/* sync api */
	ZKErrorCode GetNode(const std::string& path, char* buffer, int* buffer_len, GetNodeHandler handler = NULL,
			void* context = NULL, bool watch = false);

	ZKErrorCode GetChildren(const std::string& path, std::vector<std::string>* value, GetChildrenHandler handler = NULL,
			void* context = NULL, bool watch = false);

	ZKErrorCode Exist(const std::string& path, struct Stat* stat = NULL, ExistHandler handler = NULL,
			void* context = NULL, bool watch = false);

	ZKErrorCode Create(const std::string& path, const std::string& value, int flags, char* path_buffer = NULL, int path_buffer_len = 0);

	ZKErrorCode Set(const std::string& path, const std::string& value);

	ZKErrorCode Delete(const std::string& path);

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

	// Create的zk回调处理
	static void CreateCompletion(int rc, const char* value, const void* data);

	// Set的zk回调处理
	static void SetCompletion(int rc, const struct Stat* stat, const void* data);

	// Delete的zk回调处理
	static void DeleteCompletion(int rc, const void* data);

	void UpdateSessionState(zhandle_t* zhandle, int state);
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

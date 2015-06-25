/*
 * zkclient.cc
 *
 *  Created on: 2015年6月10日
 *      Author: Administrator
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include "zkclient.h"

pthread_once_t ZKClient::new_instance_once_ = PTHREAD_ONCE_INIT;

ZKWatchContext::ZKWatchContext(const std::string& path, void* context, ZKClient* zkclient, bool watch) {
	this->watch = watch;
	this->path = path;
	this->context = context;
	this->zkclient = zkclient;
}

ZKClient& ZKClient::GetInstance() {
	pthread_once(&new_instance_once_, NewInstance);
	return GetClient();
}

void ZKClient::NewInstance() {
	GetClient();
}

ZKClient& ZKClient::GetClient() {
	static ZKClient singleton;
	return singleton;
}

ZKClient::ZKClient()
	: zhandle_(NULL), log_fp_(NULL), expired_handler_(DefaultSessionExpiredHandler),  user_context_(NULL),
	  session_state_(ZOO_CONNECTING_STATE), session_check_running_(false) {
	pthread_mutex_init(&state_mutex_, NULL);
	pthread_cond_init(&state_cond_, NULL);
}

ZKClient::~ZKClient() {
	if (session_check_running_) { // 终止会话检测线程
		session_check_running_ = false;
		pthread_join(session_check_tid_, NULL);
	}
	if (zhandle_) {
		zookeeper_close(zhandle_);
	}
	if (log_fp_) {
		fclose(log_fp_);
	}
	pthread_cond_destroy(&state_cond_);
	pthread_mutex_destroy(&state_mutex_);
}

bool ZKClient::Init(const std::string& host, int timeout, SessionExpiredHandler expired_handler, void* context,
		 bool debug, const std::string& zklog) {
	// 用户配置
	session_timeout_ = timeout;
	if (expired_handler) {
		expired_handler_ = expired_handler;
	}
	user_context_ = context;
	// log级别
	ZooLogLevel log_level = debug ? ZOO_LOG_LEVEL_DEBUG : ZOO_LOG_LEVEL_INFO;
	zoo_set_debug_level(log_level);
	// log目录
	if (!zklog.empty()) {
		log_fp_ = fopen(zklog.c_str(), "w");
		if (!log_fp_) {
			return false;
		}
		zoo_set_log_stream(log_fp_);
	}
	// zk初始化，除非参数有问题，否则总是可以立即返回
	//
	// 存在一个非常罕见的BUG场景：就是zookeeper_init返回赋值到zhandle_之前就完成了到
	// zookeeper的连接并回调了SessionWatcher，所以在SessionWatcher里一定要注意不要依赖
	// zhandle_，而是使用SessionWatcher被传入的zhandle参数。
	zhandle_ = zookeeper_init(host.c_str(), SessionWatcher, timeout, NULL, this, 0);
	if (!zhandle_) {
		return false;
	}
	/*
	 * 等待session初始化完成，两种可能返回值：
	 * 1，连接成功，会话建立。
	 * 2，会话过期，在初始化期间很难发生，场景：连接成功后io线程cpu卡住，导致zkserver一段时间没有收到心跳，会话超时了，
	 * 然后cpu恢复运转，connected的watch事件开始处理更新了session_state然后cond_signal，然而init线程cpu卡住了，
	 * 然后zkserver挂了，然后io线程重连了另外一个zkserver，然而session过期了，然后返回我们session_expire的，然后
	 * session_expire的watch事件更新了session_state，然后init线程cpu正常了，然后看见了expire_session这个状态。
	 * （天哪，真的有这种牛逼的巧合吗。。。只是比较严谨而已~）
	 */
	pthread_mutex_lock(&state_mutex_);
	while (session_state_ != ZOO_CONNECTED_STATE &&
			session_state_ != ZOO_EXPIRED_SESSION_STATE) {
		pthread_cond_wait(&state_cond_, &state_mutex_);
	}
	int session_state = session_state_;
	pthread_mutex_unlock(&state_mutex_);
	if (session_state == ZOO_EXPIRED_SESSION_STATE) { // 会话过期，fatal级错误
		return false;
	}
	/*
	 * 会话建立，可以启动一个zk状态检测线程，主要是发现2种问题：
	 *	1，处于session_expire状态，那么回调SessionExpiredHandler，由用户终结程序（zkserver告知我们会话超时）。
	 *	2，处于非connected状态，那么判断该状态持续时间是否超过了session timeout时间，
	 *	超过则回调SessionExpiredHandler，由用户终结程序（client自己意识到会话超时）。
	 */
	session_check_running_ = true;
	pthread_create(&session_check_tid_, NULL, SessionCheckThreadMain, this);
	return true;
}

void ZKClient::GetNodeDataCompletion(int rc, const char* value, int value_len,
        const struct Stat* stat, const void* data) {
	assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;

	if (rc == ZOK) {
		watch_ctx->getnode_handler(kZKSucceed, watch_ctx->path, value, value_len, watch_ctx->context);
		if (!watch_ctx->watch) { // 没有注册watch
			delete watch_ctx;
		}
		return;
	}
	if (rc == ZNONODE) {
		watch_ctx->getnode_handler(kZKNotExist, watch_ctx->path, value, value_len, watch_ctx->context);
	} else {
		watch_ctx->getnode_handler(kZKError, watch_ctx->path, value, value_len, watch_ctx->context);
	}
	// 只要不是ZOK，那么zk都不会触发Watch事件了
	delete watch_ctx;
}

void ZKClient::GetNodeWatcher(zhandle_t* zh, int type, int state, const char* path,void* watcher_ctx) {
	assert(type == ZOO_DELETED_EVENT || type == ZOO_CHANGED_EVENT
			|| type == ZOO_NOTWATCHING_EVENT || type == ZOO_SESSION_EVENT);

	ZKWatchContext* context = (ZKWatchContext*)watcher_ctx;

	if (type == ZOO_SESSION_EVENT) { // 跳过会话事件,由zk handler的watcher进行处理
		return;
	}

	if (type == ZOO_DELETED_EVENT) {
		context->getnode_handler(kZKDeleted, context->path, NULL, 0, context->context);
		delete context;
	} else {
		if (type == ZOO_CHANGED_EVENT) {
			int rc = zoo_awget(zh, context->path.c_str(), GetNodeWatcher, context, GetNodeDataCompletion, context);
			if (rc == ZOK) {
				return;
			}
		} else if (type == ZOO_NOTWATCHING_EVENT) {
			// nothing to do
		}
		context->getnode_handler(kZKError, context->path, NULL, 0, context->context);
		delete context;
	}
}

bool ZKClient::GetNode(const std::string& path, GetNodeHandler handler, void* context, bool watch) {
	watcher_fn watcher = watch ? GetNodeWatcher : NULL;

	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, watch);
	watch_ctx->getnode_handler = handler;

	int rc = zoo_awget(zhandle_, path.c_str(), watcher, watch_ctx, GetNodeDataCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

bool ZKClient::GetChildren(const std::string& path, GetChildrenHandler handler, void* context, bool watch) {
	watcher_fn watcher = watch ? GetChildrenWatcher : NULL;

	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, watch);
	watch_ctx->getchildren_handler = handler;

	int rc = zoo_awget_children(zhandle_, path.c_str(), watcher, watch_ctx, GetChildrenStringCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

void ZKClient::GetChildrenStringCompletion(int rc, const struct String_vector* strings, const void* data) {
	assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;

	if (rc == ZOK) {
		watch_ctx->getchildren_handler(kZKSucceed, watch_ctx->path, strings->count, strings->data, watch_ctx->context);
		if (!watch_ctx->watch) { // 没有注册watch
			delete watch_ctx;
		}
		return;
	}
	if (rc == ZNONODE) {
		watch_ctx->getchildren_handler(kZKNotExist, watch_ctx->path, 0, NULL, watch_ctx->context);
	} else {
		watch_ctx->getchildren_handler(kZKError, watch_ctx->path, 0, NULL, watch_ctx->context);
	}
	// 只要不是ZOK，那么zk都不会触发Watch事件了
	delete watch_ctx;
}

void ZKClient::GetChildrenWatcher(zhandle_t* zh, int type, int state, const char* path,void* watcher_ctx) {
	assert(type == ZOO_DELETED_EVENT || type == ZOO_CHILD_EVENT
			|| type == ZOO_NOTWATCHING_EVENT || type == ZOO_SESSION_EVENT);

	ZKWatchContext* context = (ZKWatchContext*)watcher_ctx;

	if (type == ZOO_SESSION_EVENT) { // 跳过会话事件,由zk handler的watcher进行处理
		return;
	}

	if (type == ZOO_DELETED_EVENT) {
		context->getchildren_handler(kZKDeleted, context->path, 0, NULL, context->context);
		delete context;
	} else {
		if (type == ZOO_CHILD_EVENT) {
			int rc = zoo_awget_children(zh, context->path.c_str(), GetChildrenWatcher, context, GetChildrenStringCompletion, context);
			if (rc == ZOK) {
				return;
			}
		} else if (type == ZOO_NOTWATCHING_EVENT) {
			// nothing to do
		}
		context->getchildren_handler(kZKError, context->path, 0, NULL, context->context);
		delete context;
	}
}

bool ZKClient::Exist(const std::string& path, ExistHandler handler, void* context, bool watch) {
	watcher_fn watcher = watch ? ExistWatcher : NULL;

	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, watch);
	watch_ctx->exist_handler = handler;

	int rc = zoo_awexists(zhandle_, path.c_str(), watcher, watch_ctx, ExistCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

void ZKClient::ExistCompletion(int rc, const struct Stat* stat, const void* data) {
	assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;

	if (rc == ZOK || rc == ZNONODE) {
		watch_ctx->exist_handler(rc == ZOK ? kZKSucceed : kZKNotExist, watch_ctx->path, stat, watch_ctx->context);
		if (!watch_ctx->watch) { // 没有注册watch
			delete watch_ctx;
		}
		return;
	}
	watch_ctx->exist_handler(kZKError, watch_ctx->path, NULL, watch_ctx->context);
	// 只要不是ZOK，那么zk都不会触发Watch事件了
	delete watch_ctx;
}

void ZKClient::ExistWatcher(zhandle_t* zh, int type, int state, const char* path, void* watcher_ctx) {
	assert(type == ZOO_DELETED_EVENT || type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT
			|| type == ZOO_NOTWATCHING_EVENT || type == ZOO_SESSION_EVENT);

	ZKWatchContext* context = (ZKWatchContext*)watcher_ctx;

	if (type == ZOO_SESSION_EVENT) { // 跳过会话事件,由zk handler的watcher进行处理
		return;
	}

	if (type == ZOO_NOTWATCHING_EVENT) {
		context->exist_handler(kZKError, context->path, NULL, context->context);
	} else if (type == ZOO_DELETED_EVENT) {
		context->exist_handler(kZKDeleted, context->path, NULL, context->context);
	} else if (type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT) { // 节点创建或者元信息变动,重新获取通知用户
		int rc = zoo_awexists(zh, context->path.c_str(), ExistWatcher, context, ExistCompletion, context);
		if (rc == ZOK) {
			return;
		}
		context->exist_handler(kZKError, context->path, NULL, context->context);
	}
	delete context;
}

bool ZKClient::Create(const std::string& path, const std::string& value, int flags, CreateHandler handler, void* context) {
	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, false);
	watch_ctx->create_handler = handler;

	int rc = zoo_acreate(zhandle_, path.c_str(), value.c_str(), value.size(), &ZOO_OPEN_ACL_UNSAFE, flags, CreateCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

void ZKClient::CreateCompletion(int rc, const char* value, const void* data) {
	assert(rc == ZOK || rc == ZNODEEXISTS || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZNOCHILDRENFOREPHEMERALS || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;
	if (rc == ZOK) {
		watch_ctx->create_handler(kZKSucceed, watch_ctx->path, value, watch_ctx->context);
	} else if (rc == ZNONODE) {
		watch_ctx->create_handler(kZKNotExist, watch_ctx->path, "", watch_ctx->context);
	} else if (rc == ZNODEEXISTS) {
		watch_ctx->create_handler(kZKExisted, watch_ctx->path, "", watch_ctx->context);
	} else {
		watch_ctx->create_handler(kZKError, watch_ctx->path, "", watch_ctx->context);
	}
	delete watch_ctx;
}

bool ZKClient::Set(const std::string& path, const std::string& value, SetHandler handler, void* context) {
	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, false);
	watch_ctx->set_handler = handler;

	int rc = zoo_aset(zhandle_, path.c_str(), value.c_str(), value.size(), -1, SetCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

void ZKClient::SetCompletion(int rc, const struct Stat* stat, const void* data) {
	assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT || rc == ZBADVERSION ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;
	if (rc == ZOK) {
		watch_ctx->set_handler(kZKSucceed, watch_ctx->path, stat, watch_ctx->context);
	} else if (rc == ZNONODE) {
		watch_ctx->set_handler(kZKNotExist, watch_ctx->path, NULL, watch_ctx->context);
	} else {
		watch_ctx->set_handler(kZKError, watch_ctx->path, NULL, watch_ctx->context);
	}
	delete watch_ctx;
}

bool ZKClient::Delete(const std::string& path, DeleteHandler handler, void* context) {
	ZKWatchContext* watch_ctx = new ZKWatchContext(path, context, this, false);
	watch_ctx->delete_handler = handler;

	int rc = zoo_adelete(zhandle_, path.c_str(), -1, DeleteCompletion, watch_ctx);
	return rc == ZOK ? true : false;
}

void ZKClient::DeleteCompletion(int rc, const void* data) {
	assert(rc == ZOK || rc == ZCONNECTIONLOSS || rc == ZOPERATIONTIMEOUT || rc == ZBADVERSION ||
			rc == ZNOAUTH || rc == ZNONODE || rc == ZNOTEMPTY || rc == ZCLOSING);

	const ZKWatchContext* watch_ctx = (const ZKWatchContext*)data;
	if (rc == ZOK) {
		watch_ctx->delete_handler(kZKSucceed, watch_ctx->path, watch_ctx->context);
	} else if (rc == ZNONODE) {
		watch_ctx->delete_handler(kZKNotExist, watch_ctx->path, watch_ctx->context);
	} else if (rc == ZNOTEMPTY) {
		watch_ctx->delete_handler(kZKNotEmpty, watch_ctx->path, watch_ctx->context);
	} else {
		watch_ctx->delete_handler(kZKError, watch_ctx->path, watch_ctx->context);
	}
	delete watch_ctx;
}

ZKErrorCode ZKClient::GetNode(const std::string& path, char* buffer, int* buffer_len, GetNodeHandler handler,
		void* context, bool watch) {
	watcher_fn watcher = watch ? GetNodeWatcher : NULL;

	ZKWatchContext* watch_ctx = NULL;
	if (watch) {
		watch_ctx = new ZKWatchContext(path, context, this, watch);
		watch_ctx->getnode_handler = handler;
	}
	int rc = zoo_wget(zhandle_, path.c_str(), watcher, watch_ctx, buffer, buffer_len, NULL);
	if (rc == ZOK) {
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	}
	return kZKError;
}

ZKErrorCode ZKClient::GetChildren(const std::string& path, std::vector<std::string>* value, GetChildrenHandler handler,
		void* context, bool watch) {
	watcher_fn watcher = watch ? GetChildrenWatcher : NULL;

	ZKWatchContext* watch_ctx = NULL;
	if (watch) {
		watch_ctx = new ZKWatchContext(path, context, this, watch);
		watch_ctx->getchildren_handler = handler;
	}
	struct String_vector strings = { 0, NULL };
	int rc = zoo_wget_children(zhandle_, path.c_str(), watcher, watch_ctx, &strings);
	if (rc == ZOK) {
		for (int i = 0; i < strings.count; ++i) {
			value->push_back(strings.data[i]);
		}
		deallocate_String_vector(&strings);
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	}
	return kZKError;
}

ZKErrorCode ZKClient::Exist(const std::string& path, struct Stat* stat, ExistHandler handler, void* context, bool watch) {
	watcher_fn watcher = watch ? ExistWatcher : NULL;

	ZKWatchContext* watch_ctx = NULL;
	if (watch) {
		watch_ctx = new ZKWatchContext(path, context, this, watch);
		watch_ctx->exist_handler = handler;
	}
	int rc = zoo_wexists(zhandle_, path.c_str(), watcher, watch_ctx, stat);
	if (rc == ZOK) {
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	}
	return kZKError;
}

ZKErrorCode ZKClient::Create(const std::string& path, const std::string& value, int flags, char* path_buffer, int path_buffer_len) {
	int rc = zoo_create(zhandle_, path.c_str(), value.c_str(), value.size(), &ZOO_OPEN_ACL_UNSAFE, flags, path_buffer, path_buffer_len);
	if (rc == ZOK) {
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	} else if (rc == ZNODEEXISTS) {
		return kZKExisted;
	}
	return kZKError;
}

ZKErrorCode ZKClient::Set(const std::string& path, const std::string& value) {
	int rc = zoo_set(zhandle_, path.c_str(), value.c_str(), value.size(), -1);
	if (rc == ZOK) {
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	}
	return kZKError;
}

ZKErrorCode ZKClient::Delete(const std::string& path) {
	int rc = zoo_delete(zhandle_, path.c_str(), -1);
	if (rc == ZOK) {
		return kZKSucceed;
	} else if (rc == ZNONODE) {
		return kZKNotExist;
	} else if (rc == ZNOTEMPTY) {
		return kZKNotEmpty;
	}
	return kZKError;
}

void ZKClient::DefaultSessionExpiredHandler(void* context) {
	exit(0);
}

void ZKClient::SessionWatcher(zhandle_t *zh, int type, int state, const char *path, void *watcher_ctx) {
	assert(type == ZOO_SESSION_EVENT);
/*
	printf("connecting=%d assoting=%d connected=%d auth_failed=%d expired_sesssion=%d\n",
			ZOO_CONNECTING_STATE, ZOO_ASSOCIATING_STATE,
			ZOO_CONNECTED_STATE, ZOO_AUTH_FAILED_STATE, ZOO_EXPIRED_SESSION_STATE);

	printf("type=%d state=%d\n", type, state);
*/
	ZKClient* zkclient = (ZKClient*)watcher_ctx;
	zkclient->UpdateSessionState(zh, state);
}

void ZKClient::UpdateSessionState(zhandle_t* zhandle, int state) {
	pthread_mutex_lock(&state_mutex_);
	session_state_ = state;
	// 连接建立，记录协商后的会话过期时间，唤醒init函数（只有第一次有实际作用）
	if (state == ZOO_CONNECTED_STATE) {
		session_timeout_ = zoo_recv_timeout(zhandle);
		// printf("session_timeout=%ld\n", session_timeout_);
		pthread_cond_signal(&state_cond_);
	} else if (state == ZOO_EXPIRED_SESSION_STATE) {
		// 会话过期，唤醒init函数
		pthread_cond_signal(&state_cond_);
	} else {// 连接异常，记录下异常开始时间，用于计算会话是否过期
		session_disconnect_ms_ = GetCurrentMs();
		// printf("state=%d disconnect_ms=%ld\n", state, session_disconnect_ms_);
	}
	pthread_mutex_unlock(&state_mutex_);
}

void ZKClient::CheckSessionState() {
	while (session_check_running_) {
		bool session_expired = false;
		pthread_mutex_lock(&state_mutex_);
		if (session_state_ == ZOO_EXPIRED_SESSION_STATE) {
			session_expired = true;
		} else if (session_state_ != ZOO_CONNECTED_STATE) {
			if (GetCurrentMs() - session_disconnect_ms_ > session_timeout_) {
				session_expired = true;
			}
		}
		pthread_mutex_unlock(&state_mutex_);
		if (session_expired) { // 会话过期，回调用户终结程序
			return expired_handler_(user_context_); // 停止检测
		}
		usleep(1000); // 睡眠1毫秒
	}
}

void* ZKClient::SessionCheckThreadMain(void* arg) {
	ZKClient* zkclient = (ZKClient*)arg;
	zkclient->CheckSessionState();
	return NULL;
}

int64_t ZKClient::GetCurrentMs() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

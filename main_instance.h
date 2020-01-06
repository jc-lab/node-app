/**
 * @file	main_instance.h
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/21
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef __NODE_APP_MAIN_INSTANCE_H__
#define __NODE_APP_MAIN_INSTANCE_H__

#include <node.h>
#include <uv.h>
#include "vfs_handler.h"
#include "console_handler.h"

#include <vector>

namespace node_app {

	class MainInstance {
	public:
		MainInstance();
		void initializeOncePerProcess(int argc, char** argv);
		int prepare(const char* entry_file = NULL);
		int run();
		void teardownProcess();

		void setVfsHandler(VfsHandler *handler);
		void setConsoleOutputHandler(ConsoleOutputHandler* handler);

		static MainInstance* getInstance();
		v8::Local<v8::Context> getRootContext();

	private:
		struct RunEnvironment {
			std::unique_ptr<node::ArrayBufferAllocator> array_buffer_allocator_;

			v8::Isolate* isolate_;
			node::IsolateData* isolate_data_;

			v8::Locker locker;
			v8::Isolate::Scope isolate_scope;
			v8::HandleScope handle_scope;

			v8::Local<v8::Context> context_;

			node::Environment* env_;

			std::string entrypoint_src;

			RunEnvironment(std::unique_ptr< node::ArrayBufferAllocator> array_buffer_allocator, v8::Isolate* isolate, node::IsolateData *isolate_data);
		};

		uv_loop_t* loop_;
		node::tracing::Agent* tracing_agent_;

		node::MultiIsolatePlatform* platform_;

		VfsHandler *vfs_handler_;
		ConsoleOutputHandler *console_out_handler_;

		int argc_;
		char** argv_;

		int exec_argc_;
		const char** exec_argv_;

		std::vector<char*> run_arguments_;
		std::unique_ptr<RunEnvironment> run_env_;

		void applyVfs(v8::Local<v8::Context>& context);
		void applyConsole(v8::Local<v8::Context>& context);

		static MainInstance* instance_;

		static int argToRelPath(std::string& resolved_relpath, const v8::FunctionCallbackInfo<v8::Value>& info, std::string *arg_path = NULL);
		static void jsapp_callback_vfs_internalModuleStat(const v8::FunctionCallbackInfo<v8::Value>& info);
		static void jsapp_callback_vfs_realpathSync(const v8::FunctionCallbackInfo<v8::Value>& info);
		static void jsapp_callback_vfs_readFileSync(const v8::FunctionCallbackInfo<v8::Value>& info);
		static void jsapp_callback_console_out(const v8::FunctionCallbackInfo<v8::Value>& info);
	};

	template<class T>
	class MainInstanceWithContext : public MainInstance {
	private:
		T context_;

	public:
		void setInstanceContext(T context) {
			context_ = std::forward(context);
		}
	};

	template<class T>
	class MainInstanceWithContext<std::unique_ptr<T>> : public MainInstance {
	private:
		std::unique_ptr<T> context_;

	public:
		void setInstanceContext(std::unique_ptr<T> context) {
			context_ = std::move(context);
		}

		T* getInstanceContext() {
			return context_.get();
		}
	};

	template<class T>
	class MainInstanceWithContext<std::shared_ptr<T>> : public MainInstance {
	private:
		std::shared_ptr<T> context_;

	public:
		void setInstanceContext(std::shared_ptr<T> context) {
			context_ = context;
		}

		T* getInstanceContext() {
			return context_.get();
		}
	};
}

#endif //__NODE_MAIN_INSTANCE_HPP__

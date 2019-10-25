/**
 * @file	main_instance.cc
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/21
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#include "main_instance.h"

namespace node {
	namespace tracing {
		class NODE_EXTERN TraceEventHelper {
		public:
			static TracingController* GetTracingController();
			static Agent* GetAgent();
			static void SetAgent(Agent* agent);
		};
	}
}

namespace node_app {

	MainInstance* MainInstance::instance_ = NULL;

	class StringOnceWriterImpl : public StringOnceWriter {
	public:
		v8::Isolate* isolate_;
		v8::Local<v8::String> buffer_;

		StringOnceWriterImpl(v8::Isolate* isolate) : isolate_(isolate) {}

		void write(const char* data, int64_t size) override {
			if (size < 0)
				size = strlen(data);
			buffer_ = v8::String::NewFromUtf8(isolate_, data, v8::NewStringType::kNormal, (size_t)size).ToLocalChecked();
		}
	};

	class ArrayBufferWriterImpl : public ArrayBufferWriter {
	public:
		v8::Isolate* isolate_;
		v8::Local<v8::ArrayBuffer> buffer_;

		ArrayBufferWriterImpl(v8::Isolate* isolate) : isolate_(isolate) {}

		void* allocate(size_t size) override {
			buffer_ = v8::ArrayBuffer::New(isolate_, size);
			return buffer_->GetContents().Data();
		}

		void* allocate(void* data, size_t size) override {
			buffer_ = v8::ArrayBuffer::New(isolate_, data, size);
			return buffer_->GetContents().Data();
		}
	};

	MainInstance::MainInstance()
		: vfs_handler_(NULL)
	{
		instance_ = this;
	}

	MainInstance* MainInstance::getInstance()
	{
		return instance_;
	}

	v8::Local<v8::Context> MainInstance::getRootContext() {
		v8::Local<v8::Context> context;
		if (!run_env_)
			return context;
		return run_env_->context_;
	}

	MainInstance::RunEnvironment::RunEnvironment(std::unique_ptr< node::ArrayBufferAllocator> array_buffer_allocator, v8::Isolate* isolate, node::IsolateData* isolate_data)
		: array_buffer_allocator_(std::move(array_buffer_allocator)), isolate_(isolate), isolate_data_(isolate_data),
		locker(isolate), isolate_scope(isolate), handle_scope(isolate)
	{
	}

	void MainInstance::initializeOncePerProcess(int argc, char** argv) {
		const int thread_pool_size = 2;
		int i;

		argc_ = argc;
		argv_ = uv_setup_args(argc, argv);

		loop_ = uv_default_loop();

		tracing_agent_ = node::CreateAgent();
		node::tracing::TraceEventHelper::SetAgent(tracing_agent_);
		node::tracing::TracingController* controller = node::tracing::TraceEventHelper::GetTracingController();
		platform_ = node::CreatePlatform(thread_pool_size, controller);
		v8::V8::InitializePlatform(platform_);
		v8::V8::Initialize();

		/* Run Environment */
		{
			std::unique_ptr< node::ArrayBufferAllocator> array_buffer_allocator = node::ArrayBufferAllocator::Create();
			v8::Isolate* isolate = node::NewIsolate(array_buffer_allocator.get(), loop_, platform_);
			node::IsolateData* isolate_data = node::CreateIsolateData(isolate, loop_, platform_);

			run_env_.reset(new RunEnvironment(std::move(array_buffer_allocator), isolate, isolate_data));

			run_env_->context_ = node::NewContext(isolate);

			run_env_->arguments.reserve(3 + argc_);
			run_env_->arguments.push_back(argv_[0]);
			run_env_->arguments.push_back("-e");
			run_env_->arguments.push_back(NULL);

			for (i = 1; i < argc_; i++)
			{
				run_env_->arguments.push_back(argv_[i]);
			}

			applyVfs(run_env_->context_);
		}
	}

	int MainInstance::run(const char* entry_file)
	{
		int exec_argc;
		const char** exec_argv;
		int argc = run_env_->arguments.size();

		run_env_->entrypoint_src.append(R"((function(){
	const cwd = process.cwd();
	const internalBinding = process.internalBinding
	const fs = require('fs');
	delete process.internalBinding;
	const internalFs = internalBinding('fs');
	const orig = {
		internalModuleStat: internalFs.internalModuleStat,
		internalModuleReadJSON: internalFs.internalModuleReadJSON,
		realpathSync: fs.realpathSync,
		readFileSync: fs.readFileSync
	};
	internalFs.internalModuleStat = function(path) {
		const r = _app_8a3f.vfs_internalModuleStat(cwd, path);
		if(r >= 0) return r;
		return orig.internalModuleStat(path);
	}
	internalFs.internalModuleReadJSON = function(path, options) {
		const resolved = _app_8a3f.vfs_readFileSync(cwd, path);
		return resolved || orig.internalModuleReadJSON(path, options);
	}
	fs.realpathSync = function(path, options) {
		const resolved = _app_8a3f.vfs_realpathSync(cwd, path, options);
		return resolved || orig.realpathSync(path, options);
	}
	fs.readFileSync = function(path, options) {
		const resolved = _app_8a3f.vfs_readFileSync(cwd, path, options);
		return resolved || orig.readFileSync(path, options);
	}
})();
require("./)");
		run_env_->entrypoint_src.append(entry_file ? entry_file : "index");
		run_env_->entrypoint_src.append(R"(");)");
		run_env_->arguments[2] = (char*)run_env_->entrypoint_src.data();

		int exit_code = 0;

		node::Init(&argc, (const char**)run_env_->arguments.data(), &exec_argc, &exec_argv);
		run_env_->env_ = node::CreateEnvironment(run_env_->isolate_data_, run_env_->context_, argc_, argv_, exec_argc, exec_argv);

		node::LoadEnvironment(run_env_->env_);

		bool more;
		do {
			more = uv_run(loop_, UV_RUN_ONCE);
			platform_->DrainTasks(run_env_->isolate_);
			if (more == false) {
				node::EmitBeforeExit(run_env_->env_);
				more = uv_loop_alive(loop_);
				if (uv_run(loop_, UV_RUN_NOWAIT) != 0)
					more = true;
			}
		} while (more == true);

		exit_code = node::EmitExit(run_env_->env_);
		node::RunAtExit(run_env_->env_);

		node::FreeEnvironment(run_env_->env_);
		run_env_->env_ = NULL;

		node::FreeIsolateData(run_env_->isolate_data_);
		run_env_->isolate_data_ = NULL;

		platform_->DrainTasks(run_env_->isolate_);
		platform_->CancelPendingDelayedTasks(run_env_->isolate_);
		platform_->UnregisterIsolate(run_env_->isolate_);

		run_env_.reset();

		return exit_code;
	}

	void MainInstance::teardownProcess()
	{
		v8::V8::Dispose();
		node::FreePlatform(platform_);
	}

	void MainInstance::setVfsHandler(VfsHandler* handler)
	{
		vfs_handler_ = handler;
	}

	int MainInstance::argToRelPath(std::string& resolved_relpath, const v8::FunctionCallbackInfo<v8::Value>& info, std::string* arg_path)
	{
		v8::Isolate* isolate = info.GetIsolate();
		if ((info.Length() < 2))
		{
			info.GetReturnValue().Set(v8::Integer::New(isolate, -1));
			return -1;
		}
		if (!info[0]->IsString() || !info[1]->IsString())
		{
			info.GetReturnValue().Set(v8::Integer::New(isolate, -1));
			return -1;
		}

		v8::String::Utf8Value info_cwd(isolate, info[0]);
		v8::String::Utf8Value info_path(isolate, info[1]);

		if (arg_path)
		{
			*arg_path = *info_path;
		}

		const char* path_begin = strstr(*info_path, *info_cwd);
		if (!path_begin)
		{
			info.GetReturnValue().Set(v8::Integer::New(isolate, -1));
			return -1;
		}

		int info_cwd_len = strlen(*info_cwd);

		resolved_relpath = path_begin + info_cwd_len;

		return 0;
	}

	void MainInstance::jsapp_callback_vfs_internalModuleStat(const v8::FunctionCallbackInfo<v8::Value>& info)
	{
		std::string relpath;
		int rc = argToRelPath(relpath, info);
		if (rc < 0)
			return;

		v8::Isolate* isolate = info.GetIsolate();

		if (instance_->vfs_handler_)
		{
			rc = instance_->vfs_handler_->vfsStat(relpath.c_str());
		} else {
			rc = -1;
		}

		info.GetReturnValue().Set(v8::Integer::New(isolate, rc));
	}

	void MainInstance::jsapp_callback_vfs_realpathSync(const v8::FunctionCallbackInfo<v8::Value>& info)
	{
		std::string relpath;
		std::string arg_path;
		int rc = argToRelPath(relpath, info, &arg_path);
		if (rc < 0)
			return;

		v8::Isolate* isolate = info.GetIsolate();

		if (instance_->vfs_handler_)
		{
			std::string retval;
			rc = instance_->vfs_handler_->vfsRealpathSync(retval, arg_path, relpath);
			if (rc >= 0)
			{
				info.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, retval.c_str(), v8::NewStringType::kNormal, retval.length()).ToLocalChecked());
			}
		} else {
			rc = -1;
		}
	}

	void MainInstance::jsapp_callback_vfs_readFileSync(const v8::FunctionCallbackInfo<v8::Value>& info)
	{
		std::string relpath;
		int rc = argToRelPath(relpath, info);
		if (rc < 0)
			return;

		v8::Isolate* isolate = info.GetIsolate();

		if (instance_->vfs_handler_)
		{
			StringOnceWriterImpl data_writer(isolate);
			rc = instance_->vfs_handler_->vfsReadFileSync(data_writer, relpath.c_str());
			info.GetReturnValue().Set(data_writer.buffer_);
		} else {
			rc = -1;
		}
	}

	void MainInstance::applyVfs(v8::Local<v8::Context>& context) {
		v8::Context::Scope context_scope(context);

		v8::Isolate* isolate = context->GetIsolate();

		v8::Local<v8::Value> globalAppKey = v8::String::NewFromUtf8(isolate, "_app_8a3f");
		v8::Local<v8::Object> globalAppObj = v8::Object::New(isolate);
		{
			v8::Local<v8::Value> key = v8::String::NewFromUtf8(isolate, "vfs_internalModuleStat");
			v8::Local<v8::Function> func = v8::Function::New(context, jsapp_callback_vfs_internalModuleStat).ToLocalChecked();
			globalAppObj->Set(key, func);
		}
		{
			v8::Local<v8::Value> key = v8::String::NewFromUtf8(isolate, "vfs_realpathSync");
			v8::Local<v8::Function> func = v8::Function::New(context, jsapp_callback_vfs_realpathSync).ToLocalChecked();
			globalAppObj->Set(key, func);
		}
		{
			v8::Local<v8::Value> key = v8::String::NewFromUtf8(isolate, "vfs_readFileSync");
			v8::Local<v8::Function> func = v8::Function::New(context, jsapp_callback_vfs_readFileSync).ToLocalChecked();
			globalAppObj->Set(key, func);
		}
		context->Global()->Set(globalAppKey, globalAppObj);
	}

}

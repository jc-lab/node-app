/**
 * @file	app_bus.cc
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/23
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#include "app_bus.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace node_app {

	class AppBus::EventLock {
	private:
		std::unique_lock<std::mutex> lock_;
		EventHolder* holder_;

	public:
		bool alive() const {
			return holder_ != NULL;
		}

		EventHolder* holder() const {
			return holder_;
		}

		EventLock(AppBus* cls, const char* event_key, bool create_if_not_exists)
			: holder_(nullptr)
		{
			std::unique_lock<std::mutex> lock_map(cls->mutex_);
			auto map_iter = cls->event_map_.find(event_key);
			if (map_iter == cls->event_map_.end()) {
				if (!create_if_not_exists) {
					return;
				}
				std::unique_ptr<EventHolder> new_holder(new EventHolder());
				holder_ = new_holder.get();
				cls->event_map_.emplace(event_key, std::move(new_holder));
			} else {
				holder_ = map_iter->second.get();
			}
			lock_ = std::unique_lock<std::mutex>(holder_->mutex);
		}
	};

	struct AppBus::HostEventHandlerHolder : EventHandlerHolder {
		EventHandler_t func_;

		HostEventHandlerHolder(uv_loop_t* loop, EventHandler_t func)
			: EventHandlerHolder(loop), func_(func)
		{ }

		void handle(std::shared_ptr<EventMessage> message) override;
	};

	struct AppBus::V8EventHandlerHolder : EventHandlerHolder {
		v8::Persistent<v8::Function> vpfunc_;

		V8EventHandlerHolder(uv_loop_t* loop, v8::Isolate* isolate, v8::Local<v8::Function> func)
			: EventHandlerHolder(loop), vpfunc_(isolate, func)
		{ }

		void handle(std::shared_ptr<EventMessage> message) override;
	};

	struct AppBus::EventEmitTask {
		uv_async_t async_;
		std::shared_ptr<EventMessage> message_;
		std::shared_ptr<EventHandlerHolder> handler_;

		static void callback(uv_async_t* handle) {
			EventEmitTask* pthis = (EventEmitTask*)(handle->data);
			pthis->handler_->handle(pthis->message_);
			uv_close((uv_handle_t*)handle, [](uv_handle_t* handle) {
				EventEmitTask* pthis = (EventEmitTask*)(handle->data);
				delete pthis;
			});
		}

		EventEmitTask(std::shared_ptr<EventMessage> message, std::shared_ptr<EventHandlerHolder> handler)
			: message_(message), handler_(handler)
		{
			memset(&async_, 0, sizeof(async_));
			uv_async_init(handler->loop() , &async_, callback);
			async_.data = this;
		}

		void send() {
			uv_async_send(&async_);
		}
	};

	template<class JsonAllocator>
	static void v8ValueToJsonObject(rapidjson::Value& target, JsonAllocator& allocator, v8::Isolate *isolate, v8::Local<v8::Value> vvalue) {
		v8::Local<v8::Context> vcontext = isolate->GetCurrentContext();
		if (vvalue.IsEmpty()) {
			target.SetNull();
			return;
		}
		if (vvalue->IsString()) {
			v8::Local<v8::String> vimpl = vvalue.As<v8::String>();
			v8::String::Utf8Value str(isolate, vimpl);
			target.SetString(*str, str.length(), allocator);
		}
		else if (vvalue->IsStringObject()) {
			v8::Local<v8::StringObject> vimpl = vvalue.As<v8::StringObject>();
			v8::String::Utf8Value str(isolate, vimpl->ValueOf());
			target.SetString(*str, str.length(), allocator);
		}
		else if (vvalue->IsBoolean()) {
			v8::Local<v8::StringObject> vimpl = vvalue.As<v8::StringObject>();
			target.SetBool(vimpl->Int32Value(vcontext).ToChecked());
		}
		else if (vvalue->IsInt32()) {
			v8::Local<v8::Integer> vimpl = vvalue.As<v8::Integer>();
			target.SetInt(vimpl->Int32Value(vcontext).ToChecked());
		}
		else if (vvalue->IsUint32()) {
			v8::Local<v8::Integer> vimpl = vvalue.As<v8::Integer>();
			target.SetUint(vimpl->Uint32Value(vcontext).ToChecked());
		}
		else if (vvalue->IsNumber()) {
			v8::Local<v8::Number> vimpl = vvalue.As<v8::Number>();
			target.SetDouble(vimpl->NumberValue(vcontext).ToChecked());
		}
		else if (vvalue->IsNumberObject()) {
			v8::Local<v8::NumberObject> vimpl = vvalue.As<v8::NumberObject>();
			target.SetDouble(vimpl->ValueOf());
		}
		else if (vvalue->IsObject()) {
			v8::Local<v8::Object> vmap = vvalue.As<v8::Object>();
			v8::Local<v8::Array> keys = vmap->GetPropertyNames(vcontext).ToLocalChecked();
			target.SetObject();
			for (size_t i = 0; i < keys->Length(); i++) {
				// For-In Next:
				v8::Local<v8::Value> vkey = keys->Get(i);
				v8::String::Utf8Value utf8_key(isolate, vkey);
				rapidjson::Value jkey;
				rapidjson::Value jvalue;
				jkey.SetString(*utf8_key, utf8_key.length(), allocator);
				v8ValueToJsonObject(jvalue, allocator, isolate, vmap->Get(vcontext, vkey).ToLocalChecked());
				target.AddMember(jkey, jvalue, allocator);
			}
		}
		else if (vvalue->IsMap()) {
			v8::Local<v8::Map> vmap = vvalue.As<v8::Map>();
			v8::Local<v8::Array> keys = vmap->GetPropertyNames(vcontext).ToLocalChecked();
			target.SetObject();
			for (size_t i = 0; i < keys->Length(); i++) {
				// For-In Next:
				v8::Local<v8::Value> vkey = keys->Get(i);
				v8::String::Utf8Value utf8_key(isolate, vkey);
				rapidjson::Value jkey;
				rapidjson::Value jvalue;
				jkey.SetString(*utf8_key, utf8_key.length(), allocator);
				v8ValueToJsonObject(jvalue, allocator, isolate, vmap->Get(vcontext, vkey).ToLocalChecked());
				target.AddMember(jkey, jvalue, allocator);
			}
		}
		else if (vvalue->IsArray()) {
			v8::Local<v8::Array> varr = vvalue.As<v8::Array>();
			target.SetArray();
			for (size_t i = 0; i < varr->Length(); i++) {
				// For-In Next:
				v8::Local<v8::Value> vkey = varr->Get(i);
				rapidjson::Value jvalue;
				v8ValueToJsonObject(jvalue, allocator, isolate, vkey);
				target.PushBack(jvalue, allocator);
			}
		}
	}

	static v8::Local<v8::Value> jsonObjectToV8Value(v8::Isolate *isolate, const rapidjson::Value& src) {
		v8::Local<v8::Value> vtarget;
		if (src.IsNull()) {
			vtarget = v8::Null(isolate);
		}
		else if (src.IsString()) {
			vtarget = v8::String::NewFromUtf8(isolate, src.GetString(), v8::NewStringType::kNormal, src.GetStringLength()).ToLocalChecked();
		}
		else if (src.IsBool()) {
			vtarget = v8::Boolean::New(isolate, src.GetBool());
		}
		else if (src.IsInt()) {
			vtarget = v8::Int32::New(isolate, src.GetInt());
		}
		else if (src.IsUint()) {
			vtarget = v8::Uint32::New(isolate, src.GetUint());
		}
		else if (src.IsInt64()) {
			vtarget = v8::BigInt::New(isolate, src.GetInt64());
		}
		else if (src.IsUint64()) {
			vtarget = v8::BigInt::NewFromUnsigned(isolate, src.GetUint64());
		}
		else if (src.IsDouble()) {
			vtarget = v8::Number::New(isolate, src.GetDouble());
		}
		else if (src.IsFloat()) {
			vtarget = v8::Number::New(isolate, src.GetFloat());
		}
		else if (src.IsObject()) {
			v8::Local<v8::Object> vmap = v8::Object::New(isolate);
			for (auto iter = src.MemberBegin(); iter != src.MemberEnd(); iter++) {
				v8::Local<v8::Value> vkey = jsonObjectToV8Value(isolate, iter->name);
				v8::Local<v8::Value> vvalue = jsonObjectToV8Value(isolate, iter->value);
				vmap->Set(vkey, vvalue);
			}
			vtarget = vmap;
		}
		else if (src.IsArray()) {
			v8::Local<v8::Array> varr = v8::Array::New(isolate);
			uint32_t index = 0;
			for (auto iter = src.Begin(); iter != src.End(); iter++, index++) {
				v8::Local<v8::Value> vvalue = jsonObjectToV8Value(isolate, *iter);
				varr->Set(index, vvalue);
			}
			vtarget = varr;
		}
		else
		{
			vtarget = v8::Undefined(isolate);
		}
		return vtarget;
	}

	void AppBus::v8ThrowError(const char* msg) {
		v8::HandleScope scope(v8::Isolate::GetCurrent());
		scope.GetIsolate()->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(scope.GetIsolate(), msg)));
	}

	void AppBus::on(const char* event_key, EventHandler_t handler, uv_loop_t* loop) {
		std::unique_ptr<HostEventHandlerHolder> handler_holder(new HostEventHandlerHolder(loop ? loop : loop_, handler));

		{
			EventLock lock(this, event_key, true);
			lock.holder()->list.emplace_back(std::move(handler_holder));
		}
	}

	void AppBus::v8CallbackOn(const v8::FunctionCallbackInfo<v8::Value>& info) {
		v8::Isolate* isolate = info.GetIsolate();
		v8::HandleScope scope(isolate);
		v8::Local<v8::Context> context = isolate->GetCurrentContext();
		v8::Local<v8::Object> v_event_bus = info.This();
		AppBus* self = static_cast<AppBus*>(v_event_bus->GetAlignedPointerFromInternalField(0));
		uv_loop_t* loop = static_cast<uv_loop_t*>(v_event_bus->GetAlignedPointerFromInternalField(1));

		if (info.Length() < 2) {
			v8ThrowError("It must be two arguments");
			return;
		}

		if (!info[0]->IsString()) {
			v8ThrowError("The key must be a string");
			return;
		}

		if (!info[1]->IsFunction()) {
			v8ThrowError("The handler must be a function");
			return;
		}

		v8::String::Utf8Value event_key(isolate, info[0]);
		std::unique_ptr<V8EventHandlerHolder> handler_holder(new V8EventHandlerHolder(loop, isolate, info[1].As<v8::Function>()));

		{
			EventLock lock(self, *event_key, true);
			lock.holder()->list.emplace_back(std::move(handler_holder));
		}
	}

	void AppBus::emitImpl(const char* event_key, std::shared_ptr<EventMessage> message) {
		{
			EventLock lock(this, event_key, false);
			if (lock.alive()) {
				auto& list = lock.holder()->list;
				for (auto iter = list.begin(); iter != list.end(); iter++) {
					EventEmitTask* task = new EventEmitTask(message, *iter);
					task->send();
				}
			}
		}
	}

	void AppBus::emit(const char* event_key, rapidjson::Value& args, bool single_argument) {
		std::shared_ptr<EventMessage> message(new EventMessage());
		if (single_argument)
		{
			rapidjson::Value jsonValue;
			jsonValue.CopyFrom(args, message->args.GetAllocator());
			message->args.PushBack(jsonValue, message->args.GetAllocator());
		} else {
			for (auto iter = args.Begin(); iter != args.End(); iter++) {
				rapidjson::Value jsonValue;
				jsonValue.CopyFrom(*iter, message->args.GetAllocator());
				message->args.PushBack(jsonValue, message->args.GetAllocator());
			}
		}
		this->emitImpl(event_key, message);
	}

    void AppBus::emit(const char* event_key) {
        std::shared_ptr<EventMessage> message(new EventMessage());
        this->emitImpl(event_key, message);
    }

	void AppBus::v8CallbackEmit(const v8::FunctionCallbackInfo<v8::Value>& info) {
		v8::HandleScope scope(v8::Isolate::GetCurrent());
		v8::Isolate* isolate = info.GetIsolate();
		v8::Local<v8::Object> v_event_bus = info.This();
		AppBus* self = static_cast<AppBus*>(v_event_bus->GetAlignedPointerFromInternalField(0));
		uv_loop_t* loop = static_cast<uv_loop_t*>(v_event_bus->GetAlignedPointerFromInternalField(1));

		if (info.Length() < 1) {
			v8ThrowError("At least one argument is required");
			return;
		}

		if (!info[0]->IsString()) {
			v8ThrowError("The key must be a string");
			return;
		}

		v8::String::Utf8Value event_key(isolate, info[0]);
		std::shared_ptr<EventMessage> message(new EventMessage());

		for (int i = 1, n = info.Length(); i < n; i++) {
			rapidjson::Value jsonValue;
			v8ValueToJsonObject(jsonValue, message->args.GetAllocator(), isolate, info[i]);
			message->args.PushBack(jsonValue, message->args.GetAllocator());
		}

		self->emitImpl(*event_key, message);
	}

	void AppBus::V8EventHandlerHolder::handle(std::shared_ptr<EventMessage> message) {
		v8::Isolate* isolate = v8::Isolate::GetCurrent();
		v8::HandleScope handle_scope(isolate);
		v8::Local<v8::Context> context = v8::Context::New(isolate);
		v8::Context::Scope context_scope(context);

		v8::Local<v8::Context> temp = isolate->GetCurrentContext();

		v8::Local<v8::Function> vcallback = v8::Local<v8::Function>::New(isolate, vpfunc_);

		const rapidjson::Document& args = message->args;
		std::vector<v8::Local<v8::Value>> vargs(args.Size());

		int index = 0;
		for (auto iter = args.Begin(); iter != args.End(); iter++, index++) {
			vargs[index] = jsonObjectToV8Value(isolate, *iter);
		}

		vcallback->Call(temp, v8::Null(isolate), vargs.size(), vargs.data());
		//node::async_context async_context = node::EmitAsyncInit(isolate, v8::Object::New(isolate), "AppBus emit");
		//node::MakeCallback(isolate, v8::Null(isolate).As<v8::Object>(), vcallback, vargs.size(), vargs.data(), async_context);
		//node::EmitAsyncDestroy(isolate, async_context);
	}

	void AppBus::HostEventHandlerHolder::handle(std::shared_ptr<EventMessage> message)
	{
		func_(message->args);
	}

	AppBus::AppBus()
		: loop_(nullptr)
	{
	}

	void AppBus::init(uv_loop_t* loop) {
		loop_ = loop;
	}

	void AppBus::registerToContext(uv_loop_t* loop, v8::Local<v8::Context> context, const char* globalKey) {
		v8::Context::Scope context_scope(context);

		v8::Isolate* isolate = context->GetIsolate();

		v8::Local<v8::ObjectTemplate> v_templ = v8::ObjectTemplate::New(isolate);

		v8::Local<v8::String> v_global_key(v8::String::NewFromUtf8(isolate, globalKey));
		v8::Local<v8::Object> v_event_bus;
		v8::Local<v8::Function> v_func_on;

		if (!loop) {
#if NODE_MAJOR_VERSION >= 10 || \
  NODE_MAJOR_VERSION == 9 && NODE_MINOR_VERSION >= 3 || \
  NODE_MAJOR_VERSION == 8 && NODE_MINOR_VERSION >= 10
			loop = node::GetCurrentEventLoop(v8::Isolate::GetCurrent());
#else
			loop = uv_default_loop();
#endif
		}

		v_templ->SetInternalFieldCount(2);

		v_event_bus = v_templ->NewInstance(context).ToLocalChecked();

		v_event_bus->SetAlignedPointerInInternalField(0, this);
		v_event_bus->SetAlignedPointerInInternalField(1, loop);


		{
			v8::Local<v8::Value> key = v8::String::NewFromUtf8(isolate, "on");
			v8::Local<v8::Function> func = v8::Function::New(context, v8CallbackOn).ToLocalChecked();
			v_event_bus->Set(key, func);
		}
		{
			v8::Local<v8::Value> key = v8::String::NewFromUtf8(isolate, "emit");
			v8::Local<v8::Function> func = v8::Function::New(context, v8CallbackEmit).ToLocalChecked();
			v_event_bus->Set(key, func);
		}

		context->Global()->Set(context, v_global_key, v_event_bus);
	}

}
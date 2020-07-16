/**
 * @file	app_bus.hpp
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/23
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef __NODE_APP_MAIN_APP_BUS_HPP__
#define __NODE_APP_MAIN_APP_BUS_HPP__

#include <uv.h>
#include <node.h>

#include <memory>
#include <mutex>
#include <map>
#include <list>
#include <functional>

#include "rapidjson/document.h"

namespace node_app {

	class AppBus {
	public:
		typedef std::function<void(const rapidjson::Document&)> EventHandler_t;
		typedef std::function<void(const rapidjson::Document& retval, bool is_throw)> ResponseHandler_t;
		typedef std::function<void(const rapidjson::Document& args, ResponseHandler_t& response)> RequestHandler_t;

		AppBus();
		void init(uv_loop_t* loop);
		void registerToContext(uv_loop_t *loop, v8::Local<v8::Context> context, const char* globalKey);

		void on(const char* event_key, EventHandler_t handler, uv_loop_t *loop = NULL);
		void emit(const char* event_key, rapidjson::Value& args, bool single_argument = false);
		void emit(const char* event_key);

		void onRequest(const char* key, RequestHandler_t handler, uv_loop_t* loop = NULL);

	private:
		uv_loop_t* loop_;


		struct EventMessage {
			rapidjson::Document args;

			EventMessage()
				: args(rapidjson::kArrayType)
			{
			}
		};

		struct RequestMessage {
			std::string reqid;
			rapidjson::Document args;

			RequestMessage(const std::string& _reqid)
				: reqid(_reqid), args(rapidjson::kArrayType)
			{
			}
		};

		struct EventHandlerHolder {
			uv_loop_t* loop_;
			EventHandlerHolder(uv_loop_t* loop) : loop_(loop) {}
			virtual ~EventHandlerHolder() {}
			uv_loop_t* loop() const { return loop_; }
			virtual void handle(std::shared_ptr<EventMessage> message) = 0;
		};

		struct RequestHandlerHolder {
			uv_loop_t* loop_;
			RequestHandlerHolder(uv_loop_t* loop) : loop_(loop) {}
			virtual ~RequestHandlerHolder() {}
			uv_loop_t* loop() const { return loop_; }
			virtual void handle(std::shared_ptr<RequestMessage> message) = 0;
		};

		struct EventHolder {
			std::recursive_timed_mutex mutex;
			std::list<std::shared_ptr<EventHandlerHolder>> list;
			std::map<std::string, std::shared_ptr<RequestHandlerHolder>> reqs;
		};

		class EventLock;

		struct EventEmitTask;

		struct HostEventHandlerHolder;
		struct V8EventHandlerHolder;
		struct HostRequestHandlerHolder;
		struct V8RequestHandlerHolder;

		std::recursive_timed_mutex mutex_;
		std::map<std::string, std::unique_ptr<EventHolder>> event_map_;

		static void v8ThrowError(const char *msg);
		static void v8CallbackOn(const v8::FunctionCallbackInfo<v8::Value>& info);
		static void v8CallbackEmit(const v8::FunctionCallbackInfo<v8::Value>& info);

		void requestFromNode(const v8::FunctionCallbackInfo<v8::Value>& info);

		void emitImpl(const char* event_key, std::shared_ptr<EventMessage> message);
	};
}

#endif //__NODE_APP_MAIN_APP_BUS_HPP__

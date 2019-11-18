/**
 * @file	console_handler.hpp
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/11/18
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef __NODE_APP_CONSOLE_HANDLER_HPP__
#define __NODE_APP_CONSOLE_HANDLER_HPP__

#include <stdint.h>
#include <string>

namespace node_app {

	enum ConsoleOutputType {
		CONSOLE_STDOUT = 1,
		CONSOLE_STDERR = 2,
	};

	class ConsoleOutputHandler {
	public:
		virtual bool consoleOutput(ConsoleOutputType type, const std::string& text) = 0;
	};

}

#endif //__NODE_APP_CONSOLE_HANDLER_HPP__

/**
 * @file	vfs_handler.hpp
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/21
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef __NODE_APP_VFS_HANDLER_HPP__
#define __NODE_APP_VFS_HANDLER_HPP__

#include <stdint.h>
#include <string>

namespace node_app {

	class StringOnceWriter {
	public:
		virtual void write(const char* data, int64_t size = -1) = 0;
	};

	class ArrayBufferWriter {
	public:
		virtual void* allocate(size_t size) = 0;
		virtual void* allocate(void* data, size_t size) = 0;
	};

	class VfsHandler {
	public:
		virtual int vfsStat(const std::string& rel_path) = 0;
		virtual int vfsRealpathSync(std::string &retval, const std::string& arg_path, const std::string& rel_path) = 0;
		virtual int vfsReadFileSync(StringOnceWriter &writer, const std::string& rel_path) = 0;
	};

}

#endif //__NODE_APP_VFS_HANDLER_HPP__

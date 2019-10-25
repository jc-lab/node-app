/**
 * @file	main_instance.d.h
 * @author	Jichan (development@jc-lab.net / http://ablog.jc-lab.net/ )
 * @date	2019/10/21
 * @copyright Copyright (C) 2019 jichan.\n
 *            This software may be modified and distributed under the terms
 *            of the Apache License 2.0.  See the LICENSE file for details.
 */

#ifndef __NODE_APP_MAIN_INSTANCE_D_H__
#define __NODE_APP_MAIN_INSTANCE_D_H__

#include <memory>

namespace node_app {

	class MainInstance;

	template<class T>
	class MainInstanceWithContext;

	template<class T>
	class MainInstanceWithContext<std::unique_ptr<T>>;

	template<class T>
	class MainInstanceWithContext<std::shared_ptr<T>>;
}

#endif //__NODE_MAIN_INSTANCE_D_H__

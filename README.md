# Node-App

node-app은 C++환경에서 node.js를 VFS(Virtual File System)환경에서 동작시킬 수 있게 도와주는 라이브러리 입니다. 이를 이용해 node.js 프로젝트를 패키징하여 단일 실행 파일로 만들 수 있습니다. libarchive등을 이용하면 좋습니다.

node-app을 사용하려면 node 소스를 변경하여 새로 빌드해야 합니다.

node.h에서
```c++
MultiIsolatePlatform* InitializeV8Platform(int thread_pool_size);
```
이 부분을
```c++
NODE_EXTERN MultiIsolatePlatform* InitializeV8Platform(int thread_pool_size);
```
이와같이 변경해야 합니다.

# Example

```c++
#include <iostream>
#include <memory>

#include "node-app/main_instance.h"
#include "node-app/vfs_handler.h"

class MyVfsHandler : public node_app::VfsHandler
{
public:
	int vfsStat(const std::string& rel_path) override {
		if (rel_path.find("index.js") != std::string::npos) {
			return 0;
		}
		return -1;
	}
	int vfsRealpathSync(std::string& retval, const std::string& arg_path, const std::string& rel_path) override {
		retval = arg_path;
		return 0;
	}
	int vfsReadFileSync(node_app::StringOnceWriter& writer, const std::string& rel_path) override {
		if (rel_path.find("index.js") != std::string::npos)
		{
			writer.write("console.log('Hello Node World!')", -1);
		}
		return 0;
	}
};

int main(int argc, char *argv[])
{
	int exit_code;
	node_app::MainInstanceWithContext<std::unique_ptr<MyVfsHandler>> node_instance;
	std::unique_ptr<MyVfsHandler> vfs_handler(new MyVfsHandler());

	node_instance.setVfsHandler(vfs_handler.get());
	node_instance.setInstanceContext(std::move(vfs_handler));

    node_instance.initializeOncePerProcess(argc, argv);
    node_instance.prepare("index");
	exit_code = node_instance.run();
	node_instance.teardownProcess();

	return exit_code;
}
```


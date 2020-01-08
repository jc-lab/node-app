# Node-App

node-app은 C++환경에서 node.js를 VFS(Virtual File System)환경에서 동작시킬 수 있게 도와주는 라이브러리 입니다. 이를 이용해 node.js 프로젝트를 패키징하여 단일 실행 파일로 만들 수 있습니다. libarchive등을 이용하면 좋습니다.

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

# Issue

## Worker 문제
* per_process::v8_platform(export되지 않아 외부에서 사용할 수 없는 객체)을 이용하여 초기화하지 않은경우 node-app을 사용하려면 [6db45bf7def22eedfd95dac95713e850c366b169](https://github.com/nodejs/node/commit/6db45bf7def22eedfd95dac95713e850c366b169) 커밋 이후 버전으로 빌드된 libnode.dll이 필요합니다. ([#31217](https://github.com/nodejs/node/pull/31217) 코멘트 참고)
* 아직 Worker는 동작하지 않습니다. ([#31258](https://github.com/nodejs/node/issues/31258) 참고)

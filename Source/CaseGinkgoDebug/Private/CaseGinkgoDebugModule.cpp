#include "Camera/GinkgoCameraDebug.h"
#include "Modules/ModuleManager.h"

/**
 * 调试模块：只负责把各个调试工具挂上、摘下。要挂的只有相机的（Docs/Design/CameraPipeline.md「参数化与调试工具」）；
 * 输入的测量与设置界面的测试命令（Control/）只是控制台命令，自己注册（Docs/Design/Control.md「实测」）
 */
class FCaseGinkgoDebugModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Ginkgo::Debug::Camera::Startup();
	}

	virtual void ShutdownModule() override
	{
		Ginkgo::Debug::Camera::Shutdown();
	}
};

IMPLEMENT_MODULE(FCaseGinkgoDebugModule, CaseGinkgoDebug)

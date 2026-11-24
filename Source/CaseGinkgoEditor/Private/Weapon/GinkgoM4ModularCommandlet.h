#pragma once

#include "Commandlets/Commandlet.h"
#include "GinkgoM4ModularCommandlet.generated.h"

/**
 * 从 M4 配件适配表构建 Mutable 图并同步编译、保存。
 * UE 5.8 的图节点属于编辑器私有 API，节点通过反射创建；运行时模块只依赖公开的生成接口。
 * 使用 -run=GinkgoM4Modular；先运行 Private/anim/M4/tools/m4_gameplay.py 生成配件与武器定义。
 */
UCLASS()
class UGinkgoM4ModularCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoM4ModularCommandlet();

	virtual int32 Main(const FString& Params) override;
};

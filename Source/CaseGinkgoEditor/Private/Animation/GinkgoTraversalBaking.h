#pragma once

class UAnimSequence;

namespace Ginkgo::Traversal::Editor
{
	/** 从 run_Lfoot 派生左手支撑主体，并混入真实落地段；只创建功能目录资产，不修改原序列。 */
	UAnimSequence* BakeVault(bool bMovingExit);
}

#pragma once

#include "CoreMinimal.h"
#include "Camera/GinkgoSpring.h"     // TGinkgoCriticalSpring：走路的偏追身体的速度
#include "UObject/SoftObjectPtr.h"  // 合成材质的软引用
#include "GinkgoScope.generated.h"

class UMaterialInterface;

/**
 * @struct FGinkgoScopeOptics : 瞄具的光学（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
 * 类默认值从 ZF39 这一类 4 倍德国瞄具来。ZF39 原厂的表没找到，数取同类的蔡司耶拿 Ziel 4/S 的技术表（ssg-82.de 的 zielfern.pdf）：
 * 4 倍、物镜 32 mm、出瞳 8 mm、目镜 36 mm、眼距约 80 mm、100 m 上视场 11.25 m（真实视场 6.44°，目镜里 25.4°）。
 * 视场与眼距不照它（2026-11-12 你手测以后选的：镜外不动、镜内做大）：真的那样镜内的像只占屏幕高的 30%，
 * 现在真实视场 9°（目镜里 35°，占 42%），眼睛近到 35° 的像正好填满模型的目镜。倍率、出瞳、分划照真的
 *
 * 镜内照正切条件放大（没有畸变的目镜）：真实方向离光轴 θ，目镜里看到的方向离光轴 θ'，tan θ' = 倍率 × tan θ。
 * 所以镜内画面就是一个视场小倍率那么多的针孔相机拍的，只在镜子的圆里看得到；镜外照常，不放大（仅镜内变焦，你说的）。
 * 靠镜框的一圈再加畸变（你说的：镜头边缘做镜头畸变）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScopeOptics
{
	GENERATED_BODY()

	/** 倍率：镜内的角度放大多少。大了远处看得清，镜内的视场小、晃得也多 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "倍率", ClampMin = 1))
	float Magnification = 4.f;

	/**
	 * 真实视场（全角）：镜内看得到多大一片世界，目镜里看是 2·atan(倍率 × tan(它 / 2))，镜内的像在屏幕上就这么大。
	 * 真的 Ziel 4/S 是 100 m 上 11.25 m：6.44°，目镜里 25.4°，FOV 90 时占屏幕高的 30%。你选的镜内做大：9°，目镜里 35°，占 42%
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "真实视场", ClampMin = 0.5, ClampMax = 40, Units = "Degrees"))
	float TrueFieldOfView = 9.f;

	/** 出瞳直径：物镜 32 mm ÷ 4 倍 = 8 mm。眼睛要在这一圈里才看得到整个画面：偏出去是黑的月牙，远了只剩中间一小块 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "出瞳直径", ClampMin = 0.05, Units = "cm"))
	float ExitPupil = 0.8f;

	/**
	 * 眼距：出瞳在目镜最后一片镜后面多远。真的约 8 cm。第一人称的眼睛在它后面一点（FGinkgoScopeHandling::EyeBehind）。
	 * 眼睛放在目镜的像正好填满模型的目镜的地方：目镜通光半径 ÷ 目镜里半视场的正切。98k 模型的目镜通光半径 1.42 cm，
	 * 目镜里 35° 时是 1.42 ÷ tan 17.5° = 4.5 cm（真的 25.4° 时是 6.3 cm）：出瞳 3.9 cm，眼睛再往后 0.6 cm。远了镜框挡住视场的边，近了看到镜框里一圈黑
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "眼距", ClampMin = 0.5, Units = "cm"))
	float EyeRelief = 3.9f;

	/** 透光：镜内比镜外暗多少。那个年代刚有镀膜，约 85% */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "透光", ClampMin = 0, ClampMax = 1))
	float Transmission = 0.85f;

	/**
	 * 目镜污点对镜内亮度的最大衰减比例。合成在分划与场景之后，按视线穿过目镜的位置取样；
	 * 镜片上的灰印随玻璃移动。设为 0 时只关掉这一层
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "镜片污点强度", ClampMin = 0, ClampMax = 1))
	float DustOpacity = 0.08f;

	/**
	 * 物镜散射的混合权重。着色器从镜内 HDR 捕获的邻域取亮度，在画分划之前混入场景色。
	 * 权重为 0 时跳过邻域取样，目镜污点仍单独按 DustOpacity 求值
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics", meta = (DisplayName = "物镜积灰散射", ClampMin = 0, ClampMax = 1))
	float ObjectiveScatter = 0.045f;

	/** 畸变从镜内的哪里开始（半径的比例，视场的边是 1）：里面的不变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Distortion", meta = (DisplayName = "畸变开始", ClampMin = 0, ClampMax = 0.99))
	float DistortionStart = 0.75f;

	/** 畸变的强度（桶形）：越靠边压得越紧，直线在边上弯；最外沿多看到 强度 × (1 − 开始) 那么多的视场。0 是没有畸变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Distortion", meta = (DisplayName = "畸变强度", ClampMin = 0, ClampMax = 2))
	float DistortionStrength = 0.35f;

	/** 畸变的色差：红、蓝的畸变强度比绿的各多、少这么多（比例）。只在畸变那一圈（外 25%）里，最外沿红、蓝差约 1.5 px（1080p），往里很快就没了 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Color", meta = (DisplayName = "畸变的色差", ClampMin = 0, ClampMax = 0.5))
	float ChromaticAberration = 0.08f;

	/**
	 * 倍率色差：目镜把蓝的像放得比红的大这么多（半径的比例）。离中心越远彩边越宽，中心没有（绿是基准，分划的尖照绿的，不动）：
	 * 0.01 时视场的边上红、蓝差约 2.3 px（1080p、FOV 90）。那个年代的目镜校得不好，边上看得出（2026-11-12 你说的：加瞄具的色散，提高质感）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Color", meta = (DisplayName = "倍率色差", ClampMin = 0, ClampMax = 0.1))
	float LateralColor = 0.01f;

	/**
	 * 眼位色差：光线穿过出瞳的那一点离出瞳的中心每 1 cm，红、蓝的方向差这么多（正切，蓝的往那一点那边偏）。
	 * 站稳了只有视场的边上一点（眼睛在出瞳后面，边上的光从出瞳靠边的地方过）；开枪、走路时眼睛偏了，整个镜内都有彩边，越靠近月牙越宽。
	 * 0.006 时偏 0.4 cm 红、蓝差约 1.7 px（1080p）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Color", meta = (DisplayName = "眼位色差（每 cm）", ClampMin = 0, ClampMax = 0.05))
	float EyeColor = 0.006f;

	/** 出瞳的色差：红的出瞳比蓝的离目镜远这么多（cm）。眼睛偏了以后月牙的边带颜色（眼睛在出瞳后面时蓝的先暗，边上偏橙） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Color", meta = (DisplayName = "出瞳的色差", ClampMin = 0, ClampMax = 2, Units = "cm"))
	float PupilColor = 0.2f;

	/** 边缘变暗：镜内最外 15% 的半径暗到少多少（0 不暗） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Distortion", meta = (DisplayName = "边缘变暗", ClampMin = 0, ClampMax = 1))
	float EdgeDarkening = 0.35f;

	/** 视场光阑的边多软（1080p 高的像素）：0 是一刀切 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Distortion", meta = (DisplayName = "视场边缘的软", ClampMin = 0))
	float FieldStopSoftness = 1.5f;

	/** 出瞳的边多软：眼睛自己的瞳孔也有大小（约 3 mm），偏出去不是一下子就黑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Optics|Distortion", meta = (DisplayName = "出瞳边缘的软", ClampMin = 0, Units = "cm"))
	float EyeBoxSoftness = 0.15f;
};

/**
 * @struct FGinkgoScopeMount : 瞄具装在枪上的哪里（枪的网格的坐标，cm：X 沿枪管朝枪口，Z 朝上，原点是握把）。光轴平行于枪管
 * 类默认值是 98k 模型上的 ZF39：Private/anim/Mauser98k/tools/probe_aim.py 量的模型单位乘 12.6 换成 cm，再按导入的变换
 * （SM_Mauser98k.import.json：转 180°，平移 30.87、0.79、2.23）换到网格的坐标。光轴比握把高 11.90 cm、比枪管高 5.6 cm
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScopeMount
{
	GENERATED_BODY()

	/** 目镜最后一片镜的中心：光轴从这里往枪口走 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount", meta = (DisplayName = "目镜中心", Units = "cm"))
	FVector EyepieceCenter = FVector(7.38, 0.0, 11.90);

	/** 目镜的通光半径：镜内画面只画在这一圈里（模型上那片镜 1.47 cm，留一点给镜框） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount", meta = (DisplayName = "目镜通光半径", ClampMin = 0.1, Units = "cm"))
	float EyepieceRadius = 1.42f;

	/** 物镜在目镜前面多远（沿光轴）：镜内画面从物镜那里往前拍，子弹从眼睛沿光轴打，两条线是同一条 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mount", meta = (DisplayName = "物镜到目镜", ClampMin = 0, Units = "cm"))
	float ObjectiveDistance = 33.23f;
};

/**
 * @struct FGinkgoScopeReticle : 一号分划（Absehen 1，德式一号）的尺寸，真实视场里的角度（mrad：100 m 上的 cm 数 ÷ 10）
 * 左右两根横柱、下面一根竖柱，竖柱的尖就是瞄准点，与横柱的中线同高。Ziel 4/S 的表：两根横柱之间空 70 cm/100 m，
 * 横柱与竖柱遮住 20 cm/100 m。分划在物镜焦面上（欧洲瞄具的常规）：同画面一起放大、一起畸变
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScopeReticle
{
	GENERATED_BODY()

	/** 两根横柱之间空多少：100 m 上 70 cm，一头横着的鹿正好填满 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle", meta = (DisplayName = "横柱间距（mrad）", ClampMin = 0))
	float Gap = 7.f;

	/** 横柱、竖柱的宽：100 m 上遮住 20 cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle", meta = (DisplayName = "柱宽（mrad）", ClampMin = 0.05))
	float PostWidth = 2.f;

	/** 竖柱的尖收多长：从全宽收到一个点。资料里没有，我定的（看截图再调） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle", meta = (DisplayName = "尖长（mrad）", ClampMin = 0))
	float TipLength = 3.f;

	/** 颜色（线性）：没有照明的分划是黑的 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reticle", meta = (DisplayName = "颜色"))
	FLinearColor Color = FLinearColor(0.003f, 0.003f, 0.003f);
};

/**
 * @struct FGinkgoScopeSurround : 镜外（你选的：轻微模糊加暗角）。眼睛盯着镜里，镜外与近处的枪身虚一点、四周暗一点；
 * 都按第一人称的进度淡入，越肩时没有
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScopeSurround
{
	GENERATED_BODY()

	/** 模糊的半径（1080p 高的像素，按屏幕高缩放）：镜外远处的世界虚一点。0 不模糊。原来 3，2026-11-12 你手测以后说再大一点，改成 5 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surround", meta = (DisplayName = "模糊半径", ClampMin = 0))
	float BlurRadius = 5.f;

	/** 镜外充分模糊时，主视图宽、高各取当前画质比例的多少。1 使用原分辨率；退出和拉栓时随模糊恢复。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surround", meta = (DisplayName = "镜外渲染比例", ClampMin = 0.5, ClampMax = 1))
	float RenderScale = 0.70f;

	/** 按镜外实际可见的模糊混合渲染比例。FirstPerson 与 Focus 均为 0–1，Focus 为眼睛对准目镜的程度。 */
	float GetRenderScale(float FirstPerson, float Focus) const;

	/**
	 * 近处更虚：眼睛透过瞄具对焦在远处，离眼睛几厘米的镜框、枪身、拉栓的手本来就是虚的。比这个距离近的按离得多近加大模糊，贴着眼睛时到 NearBlurRadius
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surround", meta = (DisplayName = "近处开始虚的距离", ClampMin = 0, Units = "cm"))
	float NearDistance = 40.f;

	/** 贴着眼睛的东西模糊到多大（1080p 高的像素）。0 是近处不另外虚 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surround", meta = (DisplayName = "近处的模糊半径", ClampMin = 0))
	float NearBlurRadius = 9.f;

	/** 暗角：从镜子的边往外渐暗，屏幕的角上暗到少多少（0 不暗） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surround", meta = (DisplayName = "暗角", ClampMin = 0, ClampMax = 1))
	float Vignette = 0.45f;
};

/**
 * @struct FGinkgoScopeHandling : 第一人称开镜时枪相对眼睛怎么动（2026-11-12 你手测以后说的：开火时要允许枪离开准星、离开中心，
 * 视线离开光轴，镜子边上就有发黑的光学效果；开火时枪的抖动、移动、快速转镜头同理。第二次：枪踢不代替画面的踢动，
 * 画面的 view kick 与 camera shake 照越肩的保留，枪踢像《使命召唤》那样只是略微离开中心、轻微抖一下；
 * 镜内的黑照真的光学：眼睛离开出瞳不是整个变黑，而是能看到的视野变小，偏一边时那一边几乎看不见）。
 * 相机朝向跟视角（再叠画面的后坐、震动）；枪在枪口对准以后再加这几样偏移，都绕枪托顶肩的地方转（同枪的踢动，FGinkgoGunKick::Pivot）：
 * - 开枪的踢动（枪的 GunKick，第一人称时乘 KickScale）；
 * - 转镜头时枪落后视线（腰射的手感弹簧，开镜时原来收掉了）；
 * - 走路时枪往反方向偏、每一步晃一下。
 * 眼睛（相机的位置）只跟着它们走一部分（脸贴着枪托：前后跟得多，上下左右跟得少），在出瞳后面一点。
 * 分划跟着枪离开屏幕中心；出瞳离开眼睛，镜内照出瞳算的那一部分就暗：前后离开时能看到的视野变小，偏一边时那一边暗（月牙）。
 * 子弹沿枪真正的光轴走，分划指着哪里打哪里
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScopeHandling
{
	GENERATED_BODY()

	/**
	 * 眼睛跟着枪的偏移走多少（上下左右，瞄具的坐标）：0 头不动、只有枪在动；1 头完全贴着枪，出瞳不离开眼睛、镜子不暗（分划照样离开屏幕中心：相机的朝向只跟视角）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "眼睛跟枪（上下左右）", ClampMin = 0, ClampMax = 1))
	float EyeFollow = 0.7f;

	/** 眼睛跟着枪的偏移走多少（前后，沿光轴）：枪托顶着肩、脸贴着枪托往后走，眼睛离目镜差不多不变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "眼睛跟枪（前后）", ClampMin = 0, ClampMax = 1))
	float EyeFollowAxial = 0.9f;

	/**
	 * 第一人称时枪自己踢多少：枪的 GunKick（越肩、腰射照它踢）乘它。画面的踢动、震动由相机那一层管（同越肩）。
	 * 0.3 时 98k 上扬 1.5°、往后顶 0.75 cm：分划略微离开中心再回来，镜子一边暗一下
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "第一人称的枪踢", ClampMin = 0, ClampMax = 1))
	float KickScale = 0.3f;

	/** 开火瞬间的额外眼位偏移（目镜坐标，cm）：X 为靠近镜片的距离，Y、Z 控制镜框侧摆。置零可对照原手感。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "开火时的眼位偏移", Units = "cm"))
	FVector ShotEyeShift = FVector(1.1, 0.16, -0.14);

	/**
	 * 眼睛在出瞳后面多远（沿光轴）。正好在出瞳的平面上时，偏了是整个镜内一起暗；前后差一点，偏的那一边先暗、另一边还亮，是镜子边上的月牙（你说的边缘黑色）。
	 * 0.6 cm 时站稳了看得到整个视场（视场的边进出瞳的地方离中心 0.19 cm，出瞳半径 0.4、边软 0.15），出瞳偏 0.06 cm 起边上开始暗，偏 0.74 cm 全黑
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "眼睛在出瞳后面", ClampMin = 0, Units = "cm"))
	float EyeBehind = 0.6f;

	/** 转镜头时枪落后视线多少：腰射手感弹簧（枪的 Handling）的落后乘它，按第一人称的进度从 0 混过来。98k 以 45°/s 转时落后约 1.2°。0 不落后 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling", meta = (DisplayName = "转镜头的落后", ClampMin = 0, ClampMax = 2))
	float AimLag = 0.7f;

	/**
	 * 走路时枪偏多少（以参考速度走时，度）：往右走时枪口往左落（偏航负）、往右侧倾，往前走时枪口往下一点。
	 * 跟着下面的弹簧追身体的速度：起步时枪落在后面，停下时慢一点回来
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "走路的偏"))
	FRotator MoveLean = FRotator(-0.4, -0.4, 2.0);

	/** 走路时每一步晃多少（以参考速度走时，度）：X 偏航，两步一个来回；Y 俯仰，每一步一上一下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "走路的晃（偏航、俯仰）"))
	FVector2D MoveBob = FVector2D(0.6, 0.5);

	/**
	 * 走路时枪挪多少（以参考速度走时，cm，枪的坐标：X 前、Y 右、Z 上）：X、Z 按前进的速度（往前走枪往下沉），Y 按横着的速度（往右走枪往左落）。
	 * 同"走路的偏"一起追身体的速度。转动在分划上看得出来，挪动主要让出瞳离开眼睛（眼睛只跟一半），镜子边上暗
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "走路的挪（cm）"))
	FVector MoveLeanShift = FVector(0.0, -0.25, -0.15);

	/**
	 * 走路时每一步枪颠多少（以参考速度走时，cm）：X 左右（两步一个来回），Y 上下（每一步一上一下）。出瞳一颠一颠地离开眼睛，镜子边上一阵一阵地暗。
	 * 同转动加起来，出瞳离开眼睛最多约 0.45 cm（眼睛跟一半以后）：暗一部分，不全黑
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "走路的颠（左右、上下，cm）"))
	FVector2D MoveBobShift = FVector2D(0.2, 0.35);

	/** 参考速度：开镜走路的速度（98k 开镜 156 cm/s）。偏与晃按 速度 ÷ 它 缩放，最多 1.5 倍 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "参考速度", ClampMin = 1, Units = "CentimetersPerSecond"))
	float MoveReferenceSpeed = 156.f;

	/** 一步多长：晃的快慢跟着走过的距离（参考速度时约两步一秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "步长", ClampMin = 1, Units = "cm"))
	float StepLength = 80.f;

	/** 枪跟上身体的弹簧（角频率，临界阻尼）：小了起步、停下时枪落后得多、回得慢 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Handling|Move", meta = (DisplayName = "跟上身体的快慢", ClampMin = 0.1, ForceUnits = "rad/s"))
	float MoveOmega = 9.f;
};

/**
 * @struct FGinkgoScope : 一具瞄具（光学、装在枪上的位置、分划、镜外）与它的画面怎么做
 * 在项目设置里（UGinkgoDeveloperSettings::Scopes），按枪的开镜准星的名字找：98k 的 DA_Mauser98k 写的是 ZF39，资产不用改。
 * 有瞄具的枪开镜时进第一人称：眼睛在出瞳上（按枪算，不按头的骨骼；枪的程序化偏移只跟一部分，见 FGinkgoScopeHandling），
 * 镜内画面由一个场景捕获从物镜往前拍，在后处理里合成（UGinkgoScopeView）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoScope
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scope", meta = (DisplayName = "光学"))
	FGinkgoScopeOptics Optics;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scope", meta = (DisplayName = "安装"))
	FGinkgoScopeMount Mount;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scope", meta = (DisplayName = "分划"))
	FGinkgoScopeReticle Reticle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scope", meta = (DisplayName = "镜外"))
	FGinkgoScopeSurround Surround;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scope", meta = (DisplayName = "第一人称的手感"))
	FGinkgoScopeHandling Handling;

	/** 拍的视场比真实视场大多少：畸变把边上的往里压，最外沿要多看到 强度 × (1 − 开始)（默认 8.75%），色差再多一点 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture", meta = (DisplayName = "拍的视场余量", ClampMin = 1, ClampMax = 2))
	float CaptureMargin = 1.15f;

	/** 拍的分辨率是镜子在屏幕上的直径的几倍：超采样，镜内的细节不闪（1080p 下镜子约 450 px，拍 736；4K 下约 900 px，到上限 1024） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture", meta = (DisplayName = "超采样", ClampMin = 0.5, ClampMax = 3))
	float CaptureSupersample = 1.4f;

	/** 拍的分辨率的上下限（正方形的边长，像素） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture", meta = (DisplayName = "最小分辨率", ClampMin = 64))
	int32 CaptureMinSize = 256;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture", meta = (DisplayName = "最大分辨率", ClampMin = 64))
	int32 CaptureMaxSize = 1024;

	/** 合成镜内画面、镜外的模糊与暗角的后处理材质（脚本 Scripts/Assets/scope_material.py 生成） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capture", meta = (DisplayName = "合成材质"))
	TSoftObjectPtr<UMaterialInterface> CompositeMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Weapons/Scope/M_ScopeComposite.M_ScopeComposite")));
};

/**
 * 瞄具的光学计算：纯函数，后处理材质（Scripts/Assets/scope_composite.hlsl）照同样的式子算，单独测试（CaseGinkgo.Weapon.Scope.*）。
 * 坐标：瞄具的坐标系原点在目镜最后一片镜的中心，X 沿光轴朝物镜，Y、Z 是枪的（向右、向上）。镜内的点用"视场半径的比例" ρ 表示，视场的边是 1
 */
namespace Ginkgo::Scope
{
	/**
	 * @struct FSightFrame : 这一帧瞄具在世界里的位置与朝向（由枪的网格的变换算出来）
	 */
	struct FSightFrame
	{
		/** 目镜最后一片镜的中心 */
		FVector Eyepiece = FVector::ZeroVector;
		/** 物镜的中心：镜内画面从这里往前拍 */
		FVector Objective = FVector::ZeroVector;
		/** 出瞳的中心：第一人称的眼睛、开镜的子弹都从这里出发 */
		FVector Eye = FVector::ZeroVector;
		/** 朝向：X 是光轴，Y、Z 同枪 */
		FQuat Rotation = FQuat::Identity;

		FVector GetAxis() const { return Rotation.GetAxisX(); }
	};

	/** @brief 由枪的网格这一刻的变换算出瞄具在世界里在哪（缩放不算） */
	CASEGINKGO_API FSightFrame MakeSightFrame(const FTransform& Gun, const FGinkgoScope& Scope);

	/** @brief 目镜里看到的半视场的正切：倍率 × tan(真实视场 / 2)。现在的 ZF39 是 0.315（17.5°；真的那样是 0.225） */
	CASEGINKGO_API double GetApparentTanHalfField(const FGinkgoScopeOptics& Optics);

	/**
	 * @brief 畸变（桶形）：显示在 ρ 的点取的是画面里 ρ_src 那里的。开始之内 ρ_src = ρ；之外 ρ_src = ρ + k·(ρ − ρ0)³ / (1 − ρ0)²，
	 *        到开始处一阶、二阶导数都接得上，越往外压得越紧，在 ρ = 1 处多看到 k·(1 − ρ0)
	 * @param Channel - −1 红、0 绿、+1 蓝：红、蓝的 k 各多、少色差那么多
	 */
	CASEGINKGO_API double DistortRadius(double Rho, const FGinkgoScopeOptics& Optics, int32 Channel = 0);

	/** @brief 反过来：画面里 ρ_src 的点显示在 ρ 哪里（牛顿法；DistortRadius 单调增，只有一个解） */
	CASEGINKGO_API double UndistortRadius(double SourceRho, const FGinkgoScopeOptics& Optics, int32 Channel = 0);

	/**
	 * @brief 色差：这一色从目镜出来的方向（正切，瞄具的 Y、Z）。倍率色差（蓝的像大，取得靠里）加眼位色差（顺着光线穿过出瞳的那一点偏）；
	 *        绿的不变（基准：分划的尖、所见即所打都照绿的）。之后照这一色的方向加畸变（DistortRadius 带同一个 Channel）、取画面、画分划
	 * @param Apparent - 视线从目镜出来的方向（正切）
	 * @param Pupil - 视线与出瞳所在的平面交在哪（离出瞳的中心，cm）
	 * @param Channel - −1 红、0 绿、+1 蓝
	 */
	CASEGINKGO_API FVector2D ChannelApparent(const FVector2D& Apparent, const FVector2D& Pupil, const FGinkgoScopeOptics& Optics, int32 Channel);

	/**
	 * @brief 一号分划在一点盖住多少（0–1，边按一个像素抗锯齿）
	 * @param Mrad - 真实视场里的位置（mrad），X 向右、Y 向上，原点是竖柱的尖（瞄准点）
	 * @param PixelSize - 一个像素在这里有多少 mrad
	 */
	CASEGINKGO_API float ReticleCoverage(const FVector2D& Mrad, const FGinkgoScopeReticle& Reticle, double PixelSize);

	/**
	 * @brief 眼睛在 Camera、看的方向 Direction（世界）这条线进不进得了出瞳（0–1）：线与出瞳所在的平面的交点离光轴不超过出瞳的半径。
	 *        眼睛在出瞳的中心时什么方向都进得去（只受视场的边限制）；远了只剩中间一小块，偏了是月牙
	 * @param Channel - −1 红、0 绿、+1 蓝：出瞳的色差，红的出瞳离目镜远 PupilColor 的一半、蓝的近一半，月牙的边带颜色
	 */
	CASEGINKGO_API float ExitPupilVisibility(const FSightFrame& Sight, const FGinkgoScopeOptics& Optics, const FVector& Camera, const FVector& Direction, int32 Channel = 0);

	/**
	 * @brief 世界里的一点透过瞄具在眼睛看来是哪个方向：物镜看它的真实方向，乘倍率、加畸变（绿色的）就是目镜出来的方向
	 * @param OutApparentDirection - 目镜出来的方向（世界，单位向量）
	 * @param OutRho - 它在镜内的半径（视场的边是 1）
	 * @return 在物镜前面、在视场里（ρ ≤ 1）时为真
	 */
	CASEGINKGO_API bool ProjectThroughScope(const FSightFrame& Sight, const FGinkgoScopeOptics& Optics, const FVector& WorldPoint, FVector& OutApparentDirection, double& OutRho);

	/**
	 * @brief 第一人称的进度：开镜进度（两头缓过的）就是相机从越肩滑到出瞳走了多少。开镜到位时眼睛正好到出瞳，枪也抬到了开镜的位置
	 * @param AdsAlpha - 开镜进度（FGinkgoHoldState::GetAdsAlpha）
	 * @param bHasScope - 手上的枪有瞄具；没有时总是 0（越肩开镜）
	 */
	CASEGINKGO_API float FirstPersonAlpha(float AdsAlpha, bool bHasScope);

	/**
	 * @brief 开镜的灵敏度倍率（倍镜）：不开镜时 1，开镜到位时 倍镜灵敏度 ÷ 倍率，镜内的像在屏幕上走得同不开镜时一样快（你选的：倍镜单独一项）
	 * @param AdsAlpha - 开镜进度（两头缓过的）
	 * @param ScopeScale - 玩家的倍镜灵敏度（FGinkgoInputSettings，0.1–2）
	 * @param Magnification - 倍镜的倍率
	 */
	CASEGINKGO_API float ScopeLookScale(float AdsAlpha, float ScopeScale, float Magnification);

	/** @brief 绕一点转的变换（枪的坐标）：枪的程序化偏移都绕枪托顶肩的地方转（同枪的踢动）。枪的最终变换 = 它 × 枪口对准以后的变换 */
	CASEGINKGO_API FTransform MakePivotRotation(const FRotator& Rotation, const FVector& Pivot);

	/**
	 * @brief 第一人称的眼睛：枪口对准以后、程序化偏移之前的出瞳往后 EyeBehind，再跟着偏移走（沿光轴的那份跟 EyeFollowAxial，横着的跟 EyeFollow；FGinkgoScopeHandling）
	 * @param Gun - 枪的网格这一帧最终的变换（带着偏移）
	 * @param Offset - 程序化的偏移（枪的坐标，UGinkgoWeaponComponent::GetGunOffsetTransform）
	 * @param OutRest - 偏移之前的瞄具（眼睛的家在它的出瞳后面 EyeBehind）
	 * @return 眼睛（拉栓让开之前）
	 */
	CASEGINKGO_API FVector ComputeFirstPersonEye(const FTransform& Gun, const FTransform& Offset, const FGinkgoScope& Scope, FSightFrame* OutRest = nullptr);

	/**
	 * @brief 开镜开火时最终画面的眼位偏移，供玩家相机在叠加层之后取样
	 * @param Elapsed - 距最近一次开火事件的世界时间（秒），起点和包络结束后都返回零
	 * @return 目镜局部坐标中的位移（cm）。调用方转到世界空间，再按第一人称进度和拉栓让位程度混入最终视点
	 */
	CASEGINKGO_API FVector SampleShotEyeShift(const FGinkgoScopeHandling& Handling, double Elapsed);

	/**
	 * @struct FMoveSwayState : 走路的偏与晃（FGinkgoScopeHandling）的状态：追身体速度（视角坐标：X 前、Y 右，cm/s）与地速的临界阻尼弹簧、走过的步的相位。
	 * 武器组件在移动之前推进（控制器的 PlayerTick），读到的是上一帧末的速度：存的状态只推到上一帧末（速度在上一帧里当作匀速变，
	 * TGinkgoCriticalSpring::StepMoving，相位按首尾地速的平均走），这一帧照上一帧末的速度推出来、不存。
	 * 所以走得匀速时同帧率无关；起步、停下时差的是速度在一帧里的变化，Δt² 量级（直接用上一帧的速度推这一帧时，相位落后一帧，30 fps 比 144 fps 落后 0.16 rad）
	 */
	struct FMoveSwayState
	{
		TGinkgoCriticalSpring<FVector2D> Velocity;
		TGinkgoCriticalSpring<double> GroundSpeed;
		/** 上一帧末的身体速度、地速（上一次读到的） */
		FVector2D LastVelocity = FVector2D::ZeroVector;
		double LastGroundSpeed = 0.0;
		/** 上一帧多长：下一次读到这一帧末的速度时，把状态推过这一帧 */
		double LastDeltaSeconds = 0.0;
		/** 走过的步的相位（rad，一步是 π） */
		double Phase = 0.0;
		bool bStarted = false;
	};

	/**
	 * @brief 推进走路的偏与晃，返回这一帧末枪绕枪托转多少（度）。第一次从这一刻的速度开始追（进第一人称时不另起一下）
	 * @param LocalVelocity - 身体的水平速度，视角坐标（X 前、Y 右，cm/s）：上一帧末的（这一帧还没移动）
	 * @param bOnGround - 在地上：只有在地上走才一步一晃
	 * @param DeltaSeconds - 这一帧多长
	 * @param OutShift - 这一帧枪挪多少（cm，枪的坐标，"走路的挪"加"走路的颠"）：先绕枪托转，再挪
	 */
	CASEGINKGO_API FRotator StepMoveSway(FMoveSwayState& State, const FGinkgoScopeHandling& Handling, const FVector2D& LocalVelocity, bool bOnGround, double DeltaSeconds,
		FVector* OutShift = nullptr);
}

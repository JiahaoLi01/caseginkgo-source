#include "GinkgoInputRecording.h"

#if !UE_BUILD_SHIPPING

#include "Algo/BinarySearch.h"

namespace
{
	const TCHAR* const FormatLine = TEXT("# CaseGinkgo input recording v9");
	const TCHAR* const HeaderLine = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys,fire_held,reload_held,wheel_x,wheel_y,stick_x,stick_y,switch_shoulder");
	constexpr int32 ColumnCount = 21;
	/** v8 没有手动换肩的按下沿；旧录制读取时保持默认肩位 */
	const TCHAR* const HeaderLineV8 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys,fire_held,reload_held,wheel_x,wheel_y,stick_x,stick_y");
	constexpr int32 ColumnCountV8 = 20;
	/** v7 没有装填键按着、弹种轮盘的鼠标与摇杆那五列 */
	const TCHAR* const HeaderLineV7 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys,fire_held");
	constexpr int32 ColumnCountV7 = 15;
	const TCHAR* const HeaderLineV6 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys");
	constexpr int32 ColumnCountV6 = 14;

	/**
	 * v5 没有换枪的键那一列，v4 没有开镜键那一列，v3 还没有交互键那一列，v2 还没有方向键朝向那两列，v1 连方向键按下那一列也没有；
	 * 测量脚本生成的路线大多还是 v1
	 */
	const TCHAR* const HeaderLineV5 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim");
	constexpr int32 ColumnCountV5 = 13;
	const TCHAR* const HeaderLineV4 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact");
	constexpr int32 ColumnCountV4 = 12;
	const TCHAR* const HeaderLineV3 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y");
	constexpr int32 ColumnCountV3 = 11;
	const TCHAR* const HeaderLineV2 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press");
	constexpr int32 ColumnCountV2 = 9;
	const TCHAR* const HeaderLineV1 = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint");
	constexpr int32 ColumnCountV1 = 8;

	/** 一个方向键朝向的分量：−1、0、1 */
	int32 ParseKeyAxis(const FString& Field)
	{
		return FMath::Clamp(FCString::Atoi(*Field), -1, 1);
	}

	/** a 到 b 按最短角度插值 */
	double LerpAngle(const double A, const double B, const double Alpha)
	{
		return A + FRotator::NormalizeAxis(B - A) * Alpha;
	}

	/** 解析 "x,y,z" */
	bool ParseTriple(const FString& Text, FVector& Out)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), false);
		if (Parts.Num() != 3)
		{
			return false;
		}
		Out = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		return true;
	}

	FString FormatTriple(const FVector& Value)
	{
		return FString::Printf(TEXT("%.4f,%.4f,%.4f"), Value.X, Value.Y, Value.Z);
	}

	/** 解析开始状态那一行："# start relative|absolute location=x,y,z velocity=x,y,z rotation=p,y,r mode=MOVE_Walking" */
	bool ParseStartLine(const FString& Line, FGinkgoInputRecordingStart& Out, FString& OutError)
	{
		TArray<FString> Tokens;
		Line.ParseIntoArrayWS(Tokens);
		// Tokens[0] 是 "#"，Tokens[1] 是 "start"
		if (Tokens.Num() < 3 || (Tokens[2] != TEXT("relative") && Tokens[2] != TEXT("absolute")))
		{
			OutError = TEXT("开始状态要写明 relative 或 absolute");
			return false;
		}
		Out.bRelative = Tokens[2] == TEXT("relative");
		for (int32 Index = 3; Index < Tokens.Num(); ++Index)
		{
			FString Key, Value;
			if (!Tokens[Index].Split(TEXT("="), &Key, &Value))
			{
				OutError = FString::Printf(TEXT("开始状态里的 %s 不是 key=value"), *Tokens[Index]);
				return false;
			}
			FVector Triple;
			if (Key == TEXT("location") && ParseTriple(Value, Triple))
			{
				Out.Location = Triple;
			}
			else if (Key == TEXT("velocity") && ParseTriple(Value, Triple))
			{
				Out.Velocity = Triple;
			}
			else if (Key == TEXT("rotation") && ParseTriple(Value, Triple))
			{
				Out.ControlRotation = FRotator(Triple.X, Triple.Y, Triple.Z);
			}
			else if (Key == TEXT("mode"))
			{
				const int64 Mode = StaticEnum<EMovementMode>()->GetValueByNameString(Value);
				if (Mode == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("未知的移动模式 %s"), *Value);
					return false;
				}
				Out.MovementMode = static_cast<EMovementMode>(Mode);
			}
			else
			{
				OutError = FString::Printf(TEXT("开始状态里的 %s 无法解析"), *Tokens[Index]);
				return false;
			}
		}
		return true;
	}
}

FGinkgoInputSample FGinkgoInputRecording::Evaluate(const double Time) const
{
	if (Samples.IsEmpty())
	{
		return FGinkgoInputSample();
	}
	// 第一个时刻晚于 Time 的采样
	const int32 Next = Algo::UpperBoundBy(Samples, Time, &FGinkgoInputSample::Time);
	if (Next == 0)
	{
		FGinkgoInputSample Result = Samples[0];
		Result.bJumpPressed = false;
		Result.bInteractPressed = false;
		Result.bSwitchShoulderPressed = false;
		Result.DirectionPresses = 0;
		Result.WeaponKeys = EGinkgoWeaponKey::None;
		Result.WheelMouse = FVector2D::ZeroVector;
		return Result;
	}
	const FGinkgoInputSample& Before = Samples[Next - 1];
	FGinkgoInputSample Result = Before;
	Result.Time = Time;
	Result.bJumpPressed = false;
	Result.bInteractPressed = false;
	Result.bSwitchShoulderPressed = false;
	Result.DirectionPresses = 0;
	Result.WeaponKeys = EGinkgoWeaponKey::None;
	Result.WheelMouse = FVector2D::ZeroVector;
	if (Next < Samples.Num())
	{
		const FGinkgoInputSample& After = Samples[Next];
		const double Span = After.Time - Before.Time;
		const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER ? (Time - Before.Time) / Span : 0.0;
		Result.ControlRotation.Pitch = LerpAngle(Before.ControlRotation.Pitch, After.ControlRotation.Pitch, Alpha);
		Result.ControlRotation.Yaw = LerpAngle(Before.ControlRotation.Yaw, After.ControlRotation.Yaw, Alpha);
		Result.ControlRotation.Roll = LerpAngle(Before.ControlRotation.Roll, After.ControlRotation.Roll, Alpha);
	}
	return Result;
}

bool FGinkgoInputRecording::HasJumpPressBetween(const double From, const double To) const
{
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		if (Samples[Index].bJumpPressed)
		{
			return true;
		}
	}
	return false;
}

bool FGinkgoInputRecording::HasInteractPressBetween(const double From, const double To) const
{
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		if (Samples[Index].bInteractPressed)
		{
			return true;
		}
	}
	return false;
}

bool FGinkgoInputRecording::ShouldSwitchShoulderBetween(const double From, const double To) const
{
	bool bSwitch = false;
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		bSwitch ^= Samples[Index].bSwitchShoulderPressed;
	}
	return bSwitch;
}

uint8 FGinkgoInputRecording::CountDirectionPressesBetween(const double From, const double To) const
{
	int32 Presses = 0;
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		Presses += Samples[Index].DirectionPresses;
	}
	return static_cast<uint8>(FMath::Min(Presses, int32(MAX_uint8)));
}

EGinkgoWeaponKey FGinkgoInputRecording::GetWeaponKeysBetween(const double From, const double To) const
{
	EGinkgoWeaponKey Keys = EGinkgoWeaponKey::None;
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		Keys |= Samples[Index].WeaponKeys;
	}
	return Keys;
}

FVector2D FGinkgoInputRecording::GetWheelMouseBetween(const double From, const double To) const
{
	FVector2D Sum = FVector2D::ZeroVector;
	for (int32 Index = Algo::UpperBoundBy(Samples, From, &FGinkgoInputSample::Time); Index < Samples.Num() && Samples[Index].Time <= To; ++Index)
	{
		Sum += Samples[Index].WheelMouse;
	}
	return Sum;
}

FString FGinkgoInputRecording::ToCsv() const
{
	TStringBuilder<4096> Builder;
	Builder.Append(FormatLine).Append(TEXT("\n"));
	Builder.Appendf(TEXT("# start %s location=%s velocity=%s rotation=%s mode=%s\n"),
		Start.bRelative ? TEXT("relative") : TEXT("absolute"),
		*FormatTriple(Start.Location), *FormatTriple(Start.Velocity),
		*FormatTriple(FVector(Start.ControlRotation.Pitch, Start.ControlRotation.Yaw, Start.ControlRotation.Roll)),
		*StaticEnum<EMovementMode>()->GetNameStringByValue(Start.MovementMode));
	Builder.Append(HeaderLine).Append(TEXT("\n"));
	for (const FGinkgoInputSample& Sample : Samples)
	{
		Builder.Appendf(TEXT("%.6f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%d\n"), Sample.Time, Sample.ControlRotation.Pitch, Sample.ControlRotation.Yaw,
			Sample.Move.X, Sample.Move.Y, Sample.bJumpPressed ? 1 : 0, Sample.bCrouchHeld ? 1 : 0, Sample.bSprintHeld ? 1 : 0, Sample.DirectionPresses,
			Sample.DirectionKeys.X, Sample.DirectionKeys.Y, Sample.bInteractPressed ? 1 : 0, Sample.bAimHeld ? 1 : 0, static_cast<int32>(Sample.WeaponKeys), Sample.bFireHeld ? 1 : 0,
			Sample.bReloadHeld ? 1 : 0, Sample.WheelMouse.X, Sample.WheelMouse.Y, Sample.WheelStick.X, Sample.WheelStick.Y, Sample.bSwitchShoulderPressed ? 1 : 0);
	}
	return Builder.ToString();
}

bool FGinkgoInputRecording::FromCsv(const FString& Text, FGinkgoInputRecording& OutRecording, FString& OutError)
{
	OutRecording = FGinkgoInputRecording();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);

	bool bHeaderSeen = false;
	bool bStartSeen = false;
	int32 Columns = ColumnCount;
	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Line = Lines[LineIndex].TrimStartAndEnd();
		const int32 LineNumber = LineIndex + 1;
		if (Line.IsEmpty())
		{
			continue;
		}
		if (Line.StartsWith(TEXT("#")))
		{
			if (Line.StartsWith(TEXT("# start")))
			{
				if (!ParseStartLine(Line, OutRecording.Start, OutError))
				{
					OutError = FString::Printf(TEXT("第 %d 行：%s"), LineNumber, *OutError);
					return false;
				}
				bStartSeen = true;
			}
			continue;
		}
		if (!bHeaderSeen)
		{
			if (Line != HeaderLine && Line != HeaderLineV8 && Line != HeaderLineV7 && Line != HeaderLineV6 && Line != HeaderLineV5 && Line != HeaderLineV4 && Line != HeaderLineV3 && Line != HeaderLineV2
				&& Line != HeaderLineV1)
			{
				OutError = FString::Printf(TEXT("第 %d 行：表头应为 %s（也接受 v1–v8 的旧表头；v8 没有最后的 switch_shoulder 列）"),
					LineNumber, HeaderLine);
				return false;
			}
			Columns = Line == HeaderLine ? ColumnCount : Line == HeaderLineV8 ? ColumnCountV8 : (Line == HeaderLineV7 ? ColumnCountV7 : (Line == HeaderLineV6 ? ColumnCountV6 : (Line == HeaderLineV5 ? ColumnCountV5
				: (Line == HeaderLineV4 ? ColumnCountV4 : (Line == HeaderLineV3 ? ColumnCountV3 : (Line == HeaderLineV2 ? ColumnCountV2 : ColumnCountV1))))));
			bHeaderSeen = true;
			continue;
		}

		TArray<FString> Fields;
		Line.ParseIntoArray(Fields, TEXT(","), false);
		if (Fields.Num() != Columns)
		{
			OutError = FString::Printf(TEXT("第 %d 行：应有 %d 列，实际 %d 列"), LineNumber, Columns, Fields.Num());
			return false;
		}
		FGinkgoInputSample Sample;
		Sample.Time = FCString::Atod(*Fields[0]);
		Sample.ControlRotation = FRotator(FCString::Atod(*Fields[1]), FCString::Atod(*Fields[2]), 0.0);
		Sample.Move = FVector2D(FCString::Atod(*Fields[3]), FCString::Atod(*Fields[4]));
		Sample.bJumpPressed = FCString::Atoi(*Fields[5]) != 0;
		Sample.bCrouchHeld = FCString::Atoi(*Fields[6]) != 0;
		Sample.bSprintHeld = FCString::Atoi(*Fields[7]) != 0;
		Sample.DirectionPresses = Columns > ColumnCountV1 ? static_cast<uint8>(FMath::Clamp(FCString::Atoi(*Fields[8]), 0, int32(MAX_uint8))) : 0;
		// v1、v2 没有方向键的朝向：按移动输入每个分量的符号取。只在两个反方向的键一起按着时与真的朝向不同，那时移动输入是 0
		Sample.DirectionKeys = Columns > ColumnCountV2 ? FIntPoint(ParseKeyAxis(Fields[9]), ParseKeyAxis(Fields[10]))
			: FIntPoint(static_cast<int32>(FMath::Sign(Sample.Move.X)), static_cast<int32>(FMath::Sign(Sample.Move.Y)));
		Sample.bSwitchShoulderPressed = Columns > ColumnCountV8 && FCString::Atoi(*Fields[20]) != 0;
		Sample.bInteractPressed = Columns > ColumnCountV3 && FCString::Atoi(*Fields[11]) != 0;
		Sample.bAimHeld = Columns > ColumnCountV4 && FCString::Atoi(*Fields[12]) != 0;
		Sample.WeaponKeys = Columns > ColumnCountV5 ? static_cast<EGinkgoWeaponKey>(FCString::Atoi(*Fields[13]) & 0xFF) : EGinkgoWeaponKey::None;
		Sample.bFireHeld = Columns > ColumnCountV6 && FCString::Atoi(*Fields[14]) != 0;
		if (Columns > ColumnCountV7)
		{
			Sample.bReloadHeld = FCString::Atoi(*Fields[15]) != 0;
			Sample.WheelMouse = FVector2D(FCString::Atod(*Fields[16]), FCString::Atod(*Fields[17]));
			Sample.WheelStick = FVector2D(FCString::Atod(*Fields[18]), FCString::Atod(*Fields[19]));
		}
		if (!OutRecording.Samples.IsEmpty() && Sample.Time < OutRecording.Samples.Last().Time)
		{
			OutError = FString::Printf(TEXT("第 %d 行：时刻 %.6f 早于上一行"), LineNumber, Sample.Time);
			return false;
		}
		OutRecording.Samples.Add(Sample);
	}

	if (!bStartSeen)
	{
		OutError = TEXT("缺少开始状态（# start ...）");
		return false;
	}
	if (!bHeaderSeen)
	{
		OutError = TEXT("缺少表头");
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING

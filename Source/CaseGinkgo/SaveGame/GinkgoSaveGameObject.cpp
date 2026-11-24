#include "SaveGame/GinkgoSaveGameObject.h"

#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"     // FProperty::FindRedirectedPropertyName

FGinkgoSaveGameArchive::FGinkgoSaveGameArchive(FStructuredArchive::FRecord& InRecord, UObject* InObject)
	: Record(&InRecord)
	, Object(InObject)
{
	FArchive& Archive = Record->GetUnderlyingArchive();

	// 代理存档可以借这对标记知道正在读写哪个对象（比如嵌套的子对象）
	Archive.MarkScriptSerializationStart(Object.Get());

	if (Archive.IsTextFormat())
	{
		return;
	}

	StartPosition = Archive.Tell();

	// 存档时先占位，析构时回填；读档时靠它马上把字段表读进来
	uint64 FieldsOffset = 0;
	Archive << FieldsOffset;

	if (Archive.IsLoading())
	{
		Archive.Seek(StartPosition + FieldsOffset);
		Archive << Fields;

		// 这一段真正的末尾：析构时跳过去，读少了、字段调过顺序都不影响后面
		EndPosition = Archive.Tell();

		// 属性在 CoreRedirects 里改过名的，把表里的名字换成新的
		if (const UObject* Owner = Object.Get())
		{
			for (TPair<FName, uint64>& Field : Fields)
			{
				for (const UStruct* CheckStruct = Owner->GetClass(); CheckStruct; CheckStruct = CheckStruct->GetSuperStruct())
				{
					const FName NewProperty = FProperty::FindRedirectedPropertyName(CheckStruct, Field.Key);
					if (!NewProperty.IsNone())
					{
						Field.Key = NewProperty;
						break;
					}
				}
			}
		}
	}
}

FGinkgoSaveGameArchive::~FGinkgoSaveGameArchive()
{
	if (!IsValid())
	{
		return;
	}

	FArchive& Archive = Record->GetUnderlyingArchive();

	ON_SCOPE_EXIT
	{
		Archive.MarkScriptSerializationEnd(Object.Get());
	};

	if (Archive.IsTextFormat())
	{
		return;
	}

	if (Archive.IsSaving())
	{
		uint64 FieldsOffset = Archive.Tell() - StartPosition;

		Archive << Fields;

		EndPosition = Archive.Tell();

		// 回填字段表的位置
		Archive.Seek(StartPosition);
		Archive << FieldsOffset;
	}

	Archive.Seek(EndPosition);
}

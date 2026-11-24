#pragma once

#include "CoreMinimal.h"
#include "Serialization/NameAsStringProxyArchive.h"

/**
 * @struct TGinkgoSaveGameProxyArchive : 存档用的代理
 * 对象引用一律按软路径（FSoftObjectPath）存，名字按字符串存，读档时可以把旧路径重定向到新路径。
 *
 * 为什么关掉增量（ArNoDelta）：标了 SaveGame 的属性等于默认值时，增量序列化不写它。
 * 读档如果是在同一张地图上原地恢复，Actor 没有重建，这种属性就回不到默认值，所以全都写。
 *
 * @tparam bIsLoading 是否在读档
 * @tparam bIsOnlySaveGame 是否只序列化标了 SaveGame 的属性
 */
template<bool bIsLoading, bool bIsOnlySaveGame = true>
struct TGinkgoSaveGameProxyArchive final : FNameAsStringProxyArchive
{
private:
	TMap<FSoftObjectPath, FSoftObjectPath> Redirects;

public:
	explicit TGinkgoSaveGameProxyArchive(FArchive& InInnerArchive) : FNameAsStringProxyArchive(InInnerArchive)
	{
		ArIsSaveGame = bIsOnlySaveGame;
		ArNoDelta = true;
	}

	void AddRedirect(const FSoftObjectPath& From, const FSoftObjectPath& To)
	{
		if (From != To)
		{
			Redirects.Add(From, To);
		}
	}

	virtual FArchive& operator<<(FSoftObjectPath& Value) override
	{
		Value.SerializePath(*this);

		if (bIsLoading && !Value.IsNull())
		{
			Value.FixupCoreRedirects();
		}

		if (bIsLoading && Redirects.Contains(Value))
		{
			Value = Redirects[Value];
		}

		return *this;
	}

	virtual FArchive& operator<<(FSoftObjectPtr& Value) override
	{
		FSoftObjectPath Path;
		if (!bIsLoading)
		{
			Path = Value.ToSoftObjectPath();
		}

		*this << Path;

		if (bIsLoading)
		{
			Value = FSoftObjectPtr(Path);
		}
		return *this;
	}

	virtual FArchive& operator<<(UObject*& Value) override
	{
		return SerializeObject(Value);
	}

	virtual FArchive& operator<<(FWeakObjectPtr& Value) override
	{
		return SerializeObject(Value);
	}

	virtual FArchive& operator<<(FObjectPtr& Value) override
	{
		return SerializeObject(Value);
	}

private:
	template <class ObjectType>
	static FSoftObjectPath ToSoftObjectPath(const ObjectType& Value)
	{
		return FSoftObjectPath(Value);
	}

	static FSoftObjectPath ToSoftObjectPath(const FWeakObjectPtr& Value)
	{
		return FSoftObjectPath(Value.Get());
	}

	template <class ObjectType>
	FArchive& SerializeObject(ObjectType& Value)
	{
		FSoftObjectPath Path;

		if (!bIsLoading)
		{
			Path = ToSoftObjectPath(Value);
		}

		*this << Path;

		if (bIsLoading)
		{
			UObject* Object = Path.ResolveObject();
			Value = Object;

			if (!IsValid(Object) && !Path.IsNull())
			{
				Value = Path.TryLoad();
			}
		}

		return *this;
	}
};

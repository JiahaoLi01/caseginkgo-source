// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Concepts.h"

/**
 * Source views: the entry point of any query pipeline. They adapt a raw UE container
 * (TArray / TSet / TMap) or an owned moved-in container to the CQueryView protocol.
 *
 * Reference semantics: all source views expose elements by const reference — no copies
 * are made until a terminal forces materialization.
 *
 * Ownership model:
 *   - TArraySourceView / TSetSourceView / TMapSourceView hold a non-owning const pointer.
 *     The source container MUST outlive the view (lvalue overloads of MakeSourceView).
 *   - TOwnedArraySourceView holds the array by value. It is selected for rvalue sources
 *     and by OrderBy after materialization, guaranteeing right-value safety.
 */
namespace UE::Query
{
	/** Non-owning view over an external TArray<T>. Random-access, reports Num(). */
	template <typename T>
	class TArraySourceView
	{
		const TArray<T>* Source = nullptr;
		int32 Cursor = 0;

	public:
		using ValueType = T;

		explicit TArraySourceView(const TArray<T>& InSource)
			: Source(&InSource)
			, Cursor(0)
		{
		}

		bool HasNext() const { return Source && Cursor < Source->Num(); }
		const ValueType& Next() { return (*Source)[Cursor++]; }
		int32 Num() const { return Source ? (Source->Num() - Cursor) : 0; }
	};

	/** Owns a moved-in TArray<T>. Used for right-values and for OrderBy-style actions that materialize. */
	template <typename T>
	class TOwnedArraySourceView
	{
		TArray<T> Storage;
		int32 Cursor = 0;

	public:
		using ValueType = T;

		explicit TOwnedArraySourceView(TArray<T>&& InStorage)
			: Storage(MoveTemp(InStorage))
			, Cursor(0)
		{
		}

		bool HasNext() const { return Cursor < Storage.Num(); }
		const ValueType& Next() { return Storage[Cursor++]; }
		int32 Num() const { return Storage.Num() - Cursor; }
	};

	/**
	 * Non-owning view over an external TSet<T>. Iteration order matches TSet's internal order.
	 * Advance-after-return semantics: Next() captures the current iterator's reference, then
	 * advances — the returned reference is valid until the next HasNext()/Next() call.
	 *
	 * Num() reports REMAINING elements (total minus elements already consumed via Next()), to
	 * stay consistent with TArraySourceView's Cursor-based Num() and the CSizedView contract.
	 * A partially-consumed view that reported total would cause ToArray/ToSet to over-reserve.
	 */
	template <typename T>
	class TSetSourceView
	{
		const TSet<T>* Source = nullptr;
		using IteratorType = decltype(std::declval<const TSet<T>&>().CreateConstIterator());
		TOptional<IteratorType> Iter;
		int32 ConsumedCount = 0;

	public:
		using ValueType = T;

		explicit TSetSourceView(const TSet<T>& InSource)
			: Source(&InSource)
		{
			Iter.Emplace(InSource.CreateConstIterator());
		}

		bool HasNext() const { return Iter.IsSet() && (bool)(*Iter); }

		const ValueType& Next()
		{
			check(HasNext());
			const ValueType& R = **Iter;
			++(*Iter);
			++ConsumedCount;
			return R;
		}

		int32 Num() const { return Source ? FMath::Max(Source->Num() - ConsumedCount, 0) : 0; }
	};

	/**
	 * Non-owning view over an external TMap<K,V>. ValueType is TPair<K,V> — the standard
	 * element type of a TMap. Use Select([](const TPair<K,V>& P){ ... }) to project keys or values.
	 *
	 * Num() reports remaining elements (see TSetSourceView for rationale).
	 */
	template <typename K, typename V>
	class TMapSourceView
	{
		const TMap<K, V>* Source = nullptr;
		using IteratorType = decltype(std::declval<const TMap<K, V>&>().CreateConstIterator());
		TOptional<IteratorType> Iter;
		int32 ConsumedCount = 0;

	public:
		using ValueType = TPair<K, V>;

		explicit TMapSourceView(const TMap<K, V>& InSource)
			: Source(&InSource)
		{
			Iter.Emplace(InSource.CreateConstIterator());
		}

		bool HasNext() const { return Iter.IsSet() && (bool)(*Iter); }

		const ValueType& Next()
		{
			check(HasNext());
			const ValueType& R = **Iter;
			++(*Iter);
			++ConsumedCount;
			return R;
		}

		int32 Num() const { return Source ? FMath::Max(Source->Num() - ConsumedCount, 0) : 0; }
	};

	/*
	 * MakeSourceView — lvalue/rvalue dispatch.
	 *
	 * Left-values produce non-owning views: the source container lives outside and must outlive the view.
	 * Right-values (temporaries) are moved into owning views, preventing dangling references.
	 *
	 * NOTE: TSet and TMap rvalue sources are intentionally not supported in this MVP — callers must
	 * materialize them first or move them into a named variable. Attempting to pipe an rvalue TSet/TMap
	 * is a deleted overload, so the compile error is explicit rather than silently binding to the
	 * lvalue const-reference overload (which would dangle).
	 */
	template <typename T>
	auto MakeSourceView(const TArray<T>& A) { return TArraySourceView<T>(A); }

	template <typename T>
	auto MakeSourceView(TArray<T>&& A) { return TOwnedArraySourceView<T>(MoveTemp(A)); }

	template <typename T>
	auto MakeSourceView(const TSet<T>& S) { return TSetSourceView<T>(S); }

	template <typename T>
	void MakeSourceView(TSet<T>&&) = delete;

	template <typename K, typename V>
	auto MakeSourceView(const TMap<K, V>& M) { return TMapSourceView<K, V>(M); }

	template <typename K, typename V>
	void MakeSourceView(TMap<K, V>&&) = delete;

	/** Convenience entry: `auto V = From(Container);` — useful when you want the view itself, pre-pipe. */
	template <typename TContainer>
		requires CUEContainer<TContainer>
	auto From(TContainer&& C)
	{
		return MakeSourceView(std::forward<TContainer>(C));
	}
}

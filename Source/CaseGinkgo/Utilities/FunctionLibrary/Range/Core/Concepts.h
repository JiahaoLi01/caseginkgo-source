// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include <concepts>
#include <type_traits>

/**
 * Core concepts for the UEQuery pipe-style lazy query library.
 *
 * - CQueryView:      a lazy iterator-like view that exposes ValueType / HasNext() / Next().
 * - CSizedView:      a view that additionally reports a count via Num(), used for Reserve().
 * - CQueryClosure:   a pipe-able callable derived from FQueryClosureBase (tag dispatch).
 * - CUEContainer:    a raw Unreal container (TArray / TSet / TMap) detected via ElementType + Num().
 */
namespace UE::Query
{
	/** Tag base for every pipe-able closure; used to route operator| without matching raw callables. */
	struct FQueryClosureBase
	{
	};

	/**
	 * A lazy query view. Next() returns a reference that is valid until the next call to
	 * HasNext()/Next() on the same view — callers must not retain it across further advances.
	 */
	template <typename T>
	concept CQueryView = requires(std::remove_reference_t<T>& V)
	{
		typename std::remove_reference_t<T>::ValueType;
		{ V.HasNext() } -> std::convertible_to<bool>;
		{ V.Next() };
	};

	/** Optional capability: a view that can report its remaining element count. */
	template <typename T>
	concept CSizedView = CQueryView<T> && requires(const std::remove_reference_t<T>& V)
	{
		{ V.Num() } -> std::convertible_to<int32>;
	};

	/** A pipe-able closure: carries operator() and is dispatched via operator|. */
	template <typename T>
	concept CQueryClosure = std::derived_from<std::decay_t<T>, FQueryClosureBase>;

	/**
	 * A raw UE container (TArray / TSet / TMap).
	 * Must NOT be a CQueryView — that distinction lets operator| choose between applying a
	 * closure to an existing view vs. wrapping a container first.
	 */
	template <typename T>
	concept CUEContainer = requires(std::remove_reference_t<T>& C)
	{
		typename std::remove_reference_t<T>::ElementType;
		{ C.Num() } -> std::convertible_to<int32>;
	} && !CQueryView<T>;

	namespace Private
	{
		/**
		 * TIsTPair<T>::value is true iff T is a TPair<K,V>.
		 * TPair<K,V> is a type alias for TTuple<K,V>, so the specialization matches both spellings.
		 * Shared by ToMap and MapCover — both need pair-valued upstreams.
		 */
		template <typename T>
		struct TIsTPair : std::false_type
		{
		};

		template <typename K, typename V>
		struct TIsTPair<TPair<K, V>> : std::true_type
		{
		};
	}
}

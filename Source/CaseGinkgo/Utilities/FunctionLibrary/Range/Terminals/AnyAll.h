// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Any / All / None — boolean terminals that short-circuit on the first decisive element.
 *
 *   Any(Pred):  true  if any element matches Pred; stops at the first match.
 *   All(Pred):  true  if every element matches Pred; stops at the first non-match.
 *   None(Pred): true  if no element matches Pred; stops at the first match.
 */
namespace UE::Query
{
	template <typename TPred>
	struct TAnyClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TAnyClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		bool operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"Any: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			while (R.HasNext())
			{
				if (Pred(R.Next()))
				{
					return true;
				}
			}
			return false;
		}
	};

	template <typename TPred>
	struct TAllClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TAllClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		bool operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"All: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			while (R.HasNext())
			{
				if (!Pred(R.Next()))
				{
					return false;
				}
			}
			return true;
		}
	};

	template <typename TPred>
	struct TNoneClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TNoneClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		bool operator()(TRange&& R) const
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"None: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			while (R.HasNext())
			{
				if (Pred(R.Next()))
				{
					return false;
				}
			}
			return true;
		}
	};

	/** true if any element satisfies Pred; short-circuits. */
	template <typename TPred>
	auto Any(TPred Pred) { return TAnyClosure<std::decay_t<TPred>>(MoveTemp(Pred)); }

	/** true if every element satisfies Pred; short-circuits at the first failure. */
	template <typename TPred>
	auto All(TPred Pred) { return TAllClosure<std::decay_t<TPred>>(MoveTemp(Pred)); }

	/** true if no element satisfies Pred; short-circuits at the first match. */
	template <typename TPred>
	auto None(TPred Pred) { return TNoneClosure<std::decay_t<TPred>>(MoveTemp(Pred)); }
}

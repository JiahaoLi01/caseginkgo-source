// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Concepts.h"
#include "SourceView.h"

/**
 * Pipe infrastructure: the operator| overload set that lets us write
 *   Container | Op1(...) | Op2(...) | Terminal()
 *
 * Three dispatch rules, disambiguated by concept constraints:
 *   1) view      | closure  -> apply closure to view
 *   2) container | closure  -> wrap container into a source view, then apply
 *   3) closure   | closure  -> fuse into a single TComposedClosure (reusable recipe)
 *
 * Composition rule (3) is how pipelines become first-class values that can be named,
 * stored, and applied to multiple inputs without re-typing the chain.
 */
namespace UE::Query
{
	/**
	 * A closure that represents the sequential application of two closures.
	 * Applying it means: first apply Left to the input view, then feed the result to Right.
	 */
	template <typename TLeft, typename TRight>
	struct TComposedClosure : FQueryClosureBase
	{
		TLeft Left;
		TRight Right;

		template <typename L, typename R>
		TComposedClosure(L&& InLeft, R&& InRight)
			: Left(std::forward<L>(InLeft))
			, Right(std::forward<R>(InRight))
		{
		}

		template <typename TInput>
		auto operator()(TInput&& In) const &
		{
			return Right(Left(std::forward<TInput>(In)));
		}

		template <typename TInput>
		auto operator()(TInput&& In) &&
		{
			return MoveTemp(Right)(MoveTemp(Left)(std::forward<TInput>(In)));
		}
	};

	/** Rule 1: a view piped through a closure applies the closure. */
	template <typename TView, typename TClosure>
		requires CQueryView<TView> && CQueryClosure<TClosure>
	auto operator|(TView&& V, TClosure&& C)
	{
		return std::forward<TClosure>(C)(std::forward<TView>(V));
	}

	/** Rule 2: a raw UE container is wrapped in a source view, then piped through the closure. */
	template <typename TContainer, typename TClosure>
		requires CUEContainer<TContainer> && CQueryClosure<TClosure>
	auto operator|(TContainer&& Container, TClosure&& C)
	{
		return std::forward<TClosure>(C)(MakeSourceView(std::forward<TContainer>(Container)));
	}

	/** Rule 3: two closures compose into one. Reusable pipelines are expressed this way. */
	template <typename TLeft, typename TRight>
		requires CQueryClosure<TLeft> && CQueryClosure<TRight>
	auto operator|(TLeft&& L, TRight&& R)
	{
		return TComposedClosure<std::decay_t<TLeft>, std::decay_t<TRight>>(
			std::forward<TLeft>(L), std::forward<TRight>(R));
	}
}

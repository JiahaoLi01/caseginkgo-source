// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * ForEach(Func): side-effect terminal. Drives the view to completion, invoking Func on each element.
 * Returns void.
 */
namespace UE::Query
{
	template <typename TFunc>
	struct TForEachClosure : FQueryClosureBase
	{
		TFunc Func;

		explicit TForEachClosure(TFunc InFunc) : Func(MoveTemp(InFunc)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		void operator()(TRange&& R) const &
		{
			while (R.HasNext())
			{
				Func(R.Next());
			}
		}

		template <typename TRange>
			requires CQueryView<TRange>
		void operator()(TRange&& R) &&
		{
			while (R.HasNext())
			{
				Func(R.Next());
			}
		}
	};

	/** Consume the pipeline purely for side-effects. */
	template <typename TFunc>
	auto ForEach(TFunc Func)
	{
		return TForEachClosure<std::decay_t<TFunc>>(MoveTemp(Func));
	}
}

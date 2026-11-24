// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"
#include "../Core/SourceView.h"

/**
 * Concat(Other) — append Other's elements after the upstream view completes. Other may be:
 *   - a CQueryView (stored by value, copied into each fresh application),
 *   - a UE container (TArray/TSet/TMap; rvalue moved in, lvalue copied — the closure then owns
 *     its copy and builds a fresh non-owning source view for each application).
 *
 * Both sides must share the same decayed ValueType (static_assert).
 * Num() propagates only when both sides are sized.
 *
 * Lifetime: the returned TConcatView holds a non-owning reference to the container copy stored
 * inside the closure. As with every view, the pipeline must complete (via a terminal) while the
 * closure is still alive — which is the normal case for `A | Concat(B) | ToArray()` and for
 * composed closures stored in a named variable.
 */
namespace UE::Query
{
	template <typename TA, typename TB>
		requires CQueryView<TA> && CQueryView<TB>
	class TConcatView
	{
		TA A;
		TB B;

	public:
		using ValueType = typename std::decay_t<TA>::ValueType;

		static_assert(std::is_same_v<ValueType, typename std::decay_t<TB>::ValueType>,
			"Concat: both views must share the same decayed ValueType.");

		TConcatView(TA InA, TB InB)
			: A(MoveTemp(InA))
			, B(MoveTemp(InB))
		{
		}

		bool HasNext() { return A.HasNext() || B.HasNext(); }

		const ValueType& Next()
		{
			if (A.HasNext())
			{
				return A.Next();
			}
			return B.Next();
		}

		int32 Num() const requires CSizedView<TA> && CSizedView<TB>
		{
			return A.Num() + B.Num();
		}
	};

	template <typename TOther>
	struct TConcatClosure : FQueryClosureBase
	{
		TOther Other;

		explicit TConcatClosure(TOther InOther) : Other(MoveTemp(InOther)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			if constexpr (CQueryView<TOther>)
			{
				TOther OtherCopy = Other;
				return TConcatView<std::decay_t<TRange>, TOther>(
					std::forward<TRange>(R), MoveTemp(OtherCopy));
			}
			else
			{
				auto OtherView = MakeSourceView(Other);
				return TConcatView<std::decay_t<TRange>, decltype(OtherView)>(
					std::forward<TRange>(R), MoveTemp(OtherView));
			}
		}
	};

	/** Append another view or container after the upstream view. */
	template <typename TOther>
	auto Concat(TOther&& Other)
	{
		using Stored = std::decay_t<TOther>;
		return TConcatClosure<Stored>(Stored(std::forward<TOther>(Other)));
	}
}

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Take(N)        — emit at most N upstream elements, then stop.
 * TakeWhile(Pred) — emit while Pred holds; once Pred fails, stop permanently (no re-open).
 *
 * Both are transparent reference forwarders: they do not copy, they just bound the stream.
 * Take preserves Num() when upstream is sized; TakeWhile cannot — its length is data-dependent.
 */
namespace UE::Query
{
	template <typename TUp>
		requires CQueryView<TUp>
	class TTakeView
	{
		TUp Up;
		int32 Remaining = 0;

	public:
		using ValueType = typename std::decay_t<TUp>::ValueType;

		TTakeView(TUp InUp, int32 InN)
			: Up(MoveTemp(InUp))
			, Remaining(FMath::Max(InN, 0))
		{
		}

		bool HasNext() { return Remaining > 0 && Up.HasNext(); }

		const ValueType& Next()
		{
			check(Remaining > 0);
			--Remaining;
			return Up.Next();
		}

		int32 Num() const requires CSizedView<TUp>
		{
			return FMath::Min(Remaining, Up.Num());
		}
	};

	struct TTakeClosure : FQueryClosureBase
	{
		int32 N = 0;

		explicit TTakeClosure(int32 InN) : N(InN) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			return TTakeView<std::decay_t<TRange>>(std::forward<TRange>(R), N);
		}
	};

	/** Limit the stream to the first N elements. Negative N is treated as zero. */
	inline auto Take(int32 N) { return TTakeClosure(N); }

	template <typename TUp, typename TPred>
		requires CQueryView<TUp>
	class TTakeWhileView
	{
		TUp Up;
		TPred Pred;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		const UpValue* Current = nullptr;
		bool bPrimed = false;
		bool bStopped = false;

		void PrimeIfNeeded()
		{
			if (bPrimed)
			{
				return;
			}
			if (bStopped || !Up.HasNext())
			{
				Current = nullptr;
				bPrimed = true;
				return;
			}
			const UpValue& V = Up.Next();
			if (Pred(V))
			{
				Current = &V;
			}
			else
			{
				Current = nullptr;
				bStopped = true;
			}
			bPrimed = true;
		}

	public:
		using ValueType = UpValue;

		TTakeWhileView(TUp InUp, TPred InPred)
			: Up(MoveTemp(InUp))
			, Pred(MoveTemp(InPred))
		{
		}

		bool HasNext()
		{
			PrimeIfNeeded();
			return Current != nullptr;
		}

		const ValueType& Next()
		{
			PrimeIfNeeded();
			check(Current);
			const ValueType& R = *Current;
			Current = nullptr;
			bPrimed = false;
			return R;
		}
	};

	template <typename TPred>
	struct TTakeWhileClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TTakeWhileClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"TakeWhile: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			return TTakeWhileView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), Pred);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, TPred&, const UpValue&>,
				"TakeWhile: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			return TTakeWhileView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), MoveTemp(Pred));
		}
	};

	/** Emit elements while Pred returns true; stop at the first false, permanently. */
	template <typename TPred>
	auto TakeWhile(TPred Pred)
	{
		return TTakeWhileClosure<std::decay_t<TPred>>(MoveTemp(Pred));
	}
}

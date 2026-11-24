// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "../Core/Concepts.h"
#include "../Core/ClosureBase.h"

/**
 * Drop(N)         — skip the first N upstream elements, then forward the rest.
 * DropWhile(Pred) — skip elements while Pred holds; once it fails, forward that element and all remaining.
 *
 * DropWhile does NOT re-check Pred after the first failing element — once the "dropping phase" ends, it's over.
 */
namespace UE::Query
{
	template <typename TUp>
		requires CQueryView<TUp>
	class TDropView
	{
		TUp Up;
		int32 ToDrop = 0;
		bool bDropped = false;

		void DropIfNeeded()
		{
			if (bDropped)
			{
				return;
			}
			while (ToDrop > 0 && Up.HasNext())
			{
				Up.Next();
				--ToDrop;
			}
			bDropped = true;
		}

	public:
		using ValueType = typename std::decay_t<TUp>::ValueType;

		TDropView(TUp InUp, int32 InN)
			: Up(MoveTemp(InUp))
			, ToDrop(FMath::Max(InN, 0))
		{
		}

		bool HasNext()
		{
			DropIfNeeded();
			return Up.HasNext();
		}

		const ValueType& Next()
		{
			DropIfNeeded();
			return Up.Next();
		}

		int32 Num() const requires CSizedView<TUp>
		{
			return FMath::Max(Up.Num() - ToDrop, 0);
		}
	};

	struct TDropClosure : FQueryClosureBase
	{
		int32 N = 0;

		explicit TDropClosure(int32 InN) : N(InN) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const
		{
			return TDropView<std::decay_t<TRange>>(std::forward<TRange>(R), N);
		}
	};

	/** Skip the first N elements. Negative N is treated as zero. */
	inline auto Drop(int32 N) { return TDropClosure(N); }

	template <typename TUp, typename TPred>
		requires CQueryView<TUp>
	class TDropWhileView
	{
		TUp Up;
		TPred Pred;

		using UpValue = typename std::decay_t<TUp>::ValueType;
		const UpValue* Current = nullptr;
		bool bPrimed = false;
		bool bDroppedPhaseDone = false;

		void PrimeIfNeeded()
		{
			if (bPrimed)
			{
				return;
			}
			while (Up.HasNext())
			{
				const UpValue& V = Up.Next();
				if (!bDroppedPhaseDone)
				{
					if (Pred(V))
					{
						continue;
					}
					bDroppedPhaseDone = true;
				}
				Current = &V;
				bPrimed = true;
				return;
			}
			Current = nullptr;
			bPrimed = true;
		}

	public:
		using ValueType = UpValue;

		TDropWhileView(TUp InUp, TPred InPred)
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
	struct TDropWhileClosure : FQueryClosureBase
	{
		TPred Pred;

		explicit TDropWhileClosure(TPred InPred) : Pred(MoveTemp(InPred)) {}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) const &
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, const TPred&, const UpValue&>,
				"DropWhile: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			return TDropWhileView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), Pred);
		}

		template <typename TRange>
			requires CQueryView<TRange>
		auto operator()(TRange&& R) &&
		{
			using UpValue = typename std::decay_t<TRange>::ValueType;
			static_assert(std::is_invocable_r_v<bool, TPred&, const UpValue&>,
				"DropWhile: predicate must be invocable with a const-reference argument of ValueType and return bool.");
			return TDropWhileView<std::decay_t<TRange>, TPred>(std::forward<TRange>(R), MoveTemp(Pred));
		}
	};

	/** Skip elements while Pred returns true; once it fails, emit that element and everything after. */
	template <typename TPred>
	auto DropWhile(TPred Pred)
	{
		return TDropWhileClosure<std::decay_t<TPred>>(MoveTemp(Pred));
	}
}

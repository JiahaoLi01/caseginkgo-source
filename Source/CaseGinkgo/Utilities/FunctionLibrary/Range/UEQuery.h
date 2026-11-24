// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

/**
 * UEQuery — a pipe-style lazy query library tailored for Unreal Engine containers.
 *
 *   TArray<int32> Nums = {1, 2, 3, 4, 5};
 *   auto Evens = Nums
 *       | UE::Query::Where([](int32 N){ return N % 2 == 0; })
 *       | UE::Query::Select([](int32 N){ return N * 10; })
 *       | UE::Query::ToArray();
 *
 * This header aggregates every public symbol. Include it once and you get the full surface area.
 * For selective inclusion, pull in the corresponding Core/Views/Actions/Terminals headers directly.
 */

// Core
#include "Core/Concepts.h"
#include "Core/SourceView.h"
#include "Core/ClosureBase.h"

// Views
#include "Views/FilterView.h"
#include "Views/TransformView.h"
#include "Views/TakeView.h"
#include "Views/DropView.h"
#include "Views/SelectMany.h"
#include "Views/Distinct.h"
#include "Views/Concat.h"
#include "Views/KeysValues.h"
#include "Views/OfType.h"
#include "Views/MapCover.h"
#include "Views/MapMerge.h"

// Actions
#include "Actions/OrderByAction.h"

// Terminals
#include "Terminals/ToArray.h"
#include "Terminals/ToSet.h"
#include "Terminals/ToMap.h"
#include "Terminals/ForEach.h"
#include "Terminals/Count.h"
#include "Terminals/AnyAll.h"
#include "Terminals/First.h"
#include "Terminals/LastSingle.h"
#include "Terminals/ContainsFind.h"
#include "Terminals/MaxMinBy.h"
#include "Terminals/Aggregate.h"

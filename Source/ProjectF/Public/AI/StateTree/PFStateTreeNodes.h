#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "Core/PFTypes.h"
#include "PFStateTreeNodes.generated.h"

USTRUCT()
struct PROJECTF_API FPFBehaviorInstanceData
{
	GENERATED_BODY()
	UPROPERTY() float Elapsed = 0.f;
};

USTRUCT(meta=(DisplayName="ProjectF Behavior", Category="ProjectF"))
struct PROJECTF_API FPFBehaviorTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FPFBehaviorInstanceData;
	UPROPERTY(EditAnywhere, Category="Parameter") EPFAlertState State = EPFAlertState::Routine;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct PROJECTF_API FPFStateConditionData
{
	GENERATED_BODY()
};

USTRUCT(meta=(DisplayName="ProjectF Perception State", Category="ProjectF"))
struct PROJECTF_API FPFStateCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FPFStateConditionData;
	UPROPERTY(EditAnywhere, Category="Parameter") EPFAlertState State = EPFAlertState::Routine;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

#include "AI/StateTree/PFStateTreeNodes.h"
#include "AI/PFAIController.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FPFBehaviorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	Context.GetInstanceData(*this).Elapsed = 0.f;
	return Cast<APFAIController>(Context.GetOwner()) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FPFBehaviorTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	APFAIController* AI = Cast<APFAIController>(Context.GetOwner());
	if (!AI) return EStateTreeRunStatus::Failed;
	if (AI->DesiredState != State) return EStateTreeRunStatus::Succeeded;
	FInstanceDataType& Data = Context.GetInstanceData(*this); Data.Elapsed += DeltaTime;
	if (Data.Elapsed >= .2f) { AI->PerformBehavior(State, Data.Elapsed); Data.Elapsed = 0.f; }
	return EStateTreeRunStatus::Running;
}

void FPFBehaviorTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (APFAIController* AI = Cast<APFAIController>(Context.GetOwner())) AI->ExitBehavior();
}

bool FPFStateCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const APFAIController* AI = Cast<APFAIController>(Context.GetOwner());
	return AI && AI->DesiredState == State;
}

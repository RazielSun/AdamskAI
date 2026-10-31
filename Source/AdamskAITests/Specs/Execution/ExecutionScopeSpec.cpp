// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Support/ExecutionTestSupport.h"
#include "Support/CompilerSpecSupport.h"
#include "CQTest.h"

namespace FExecutionScopeSpecPrivate
{
	FString ScopeXml(const TCHAR* Child)
	{
		return FString::Printf(TEXT("<Agent version=\"1\" id=\"scope\"><Inputs><Input name=\"Goal\" type=\"Vector\" default=\"1,2,3\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"><On event=\"tree.success\" goto=\"won\"/><On event=\"tree.failure\" goto=\"lost\"/></State>")
			TEXT("<State id=\"won\"/><State id=\"lost\"/></StateMachine>")
			TEXT("<Tree id=\"t\"><Scoped id=\"sc\">%s</Scoped></Tree></Agent>"), Child);
	}

	const TCHAR* MoveChild = TEXT("<MoveTo id=\"move\" in-goal=\"Goal\" acceptanceRadius=\"1\"/>");

	int32 LineIndex(const FADSKTestRuntime& Runtime, const TCHAR* Fragment)
	{
		return Runtime.Log->Lines.IndexOfByPredicate([Fragment](const FString& Line) { return Line.Contains(Fragment); });
	}
}

using namespace FExecutionScopeSpecPrivate;

TEST_CLASS_WITH_FLAGS(FExecutionScopeSpec, "AdamskAI.Execution.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
	TEST_METHOD(StartsBeforeAndEndsAfterItsChild)
	{
		FADSKTestRuntime Runtime(ScopeXml(MoveChild));
		Runtime.Instance->Start();
		ASSERT_THAT(IsTrue(LineIndex(Runtime, TEXT("Start:Scoped:sc")) < LineIndex(Runtime, TEXT("Start:MoveTo:move")), TEXT("log:\n  ") + FString::Join(Runtime.Log->Lines, TEXT("\n  "))));
		ASSERT_THAT(AreEqual(static_cast<int32>(INDEX_NONE), LineIndex(Runtime, TEXT("Update:"))));
		Runtime.Instance->Stop();
		const int32 ChildEnd = LineIndex(Runtime, TEXT("End:MoveTo:move:Cancelled"));
		ASSERT_THAT(IsTrue(ChildEnd != INDEX_NONE && ChildEnd < LineIndex(Runtime, TEXT("End:Scoped:sc:Cancelled")), TEXT("log:\n  ") + FString::Join(Runtime.Log->Lines, TEXT("\n  "))));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(UpdatesOutermostFirstFromTheNextTick)
	{
		FADSKTestRuntime Runtime(ScopeXml(MoveChild));
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.25f);
		const int32 ScopeUpdate = LineIndex(Runtime, TEXT("Update:Scoped:sc:0.250"));
		ASSERT_THAT(IsTrue(ScopeUpdate != INDEX_NONE && ScopeUpdate < LineIndex(Runtime, TEXT("Update:MoveTo:move:0.250")), TEXT("log:\n  ") + FString::Join(Runtime.Log->Lines, TEXT("\n  "))));
		Runtime.Instance->Stop();
	}

	TEST_METHOD(UpdateResultCancelsTheChildAndFinishesTheScope)
	{
		FADSKTestRuntime Runtime(ScopeXml(MoveChild));
		Runtime.Log->ScriptedUpdates.Add(TEXT("sc"), EADSKNodeStatus::Failed);
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:MoveTo:move:Cancelled")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:Scoped:sc:Failed")));
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Update:MoveTo:move")));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("lost")}));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(SucceededUpdateSucceedsTheScope)
	{
		FADSKTestRuntime Runtime(ScopeXml(MoveChild));
		Runtime.Log->ScriptedUpdates.Add(TEXT("sc"), EADSKNodeStatus::Succeeded);
		Runtime.Instance->Start();
		Runtime.Instance->Tick(0.1f);
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:Scoped:sc:Succeeded")));
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("won")}));
	}

	TEST_METHOD(FailedStartSkipsTheChild)
	{
		FADSKTestRuntime Runtime(ScopeXml(MoveChild));
		Runtime.Log->ScriptedStarts.Add(TEXT("sc"), EADSKNodeStatus::Failed);
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.LacksLine(TEXT("Start:MoveTo")));
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:Scoped:sc:Failed")));
		ADSK_ASSERT(Runtime.Balanced());
	}

	TEST_METHOD(EndsWithItsChildsResult)
	{
		FADSKTestRuntime Runtime(ScopeXml(TEXT("<Wait id=\"w\" duration=\"0\"/>")));
		Runtime.Instance->Start();
		ADSK_ASSERT(Runtime.HasLine(TEXT("End:Scoped:sc:Succeeded")));
		Runtime.Instance->Tick(0.1f);
		ADSK_ASSERT(Runtime.PathIs({TEXT("m"), TEXT("won")}));
	}

	TEST_METHOD(ShapeIsOneChildWithAStart)
	{
		const TSharedRef<FADSKNodeRegistry> Registry = FADSKNodeRegistry::Create();
		FADSKNodeDescriptor Scope;
		Scope.Name = TEXT("Wrap");
		Scope.Kind = EADSKNodeKind::Scope;
		Scope.MinChildren = Scope.MaxChildren = 1;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Scope)));
		Scope.OnStart = [](FADSKNodeContext&) { return EADSKNodeStatus::Running; };
		Scope.MaxChildren = 2;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::InvalidArity, Registry->Register(Scope)));
		Scope.MaxChildren = 1;
		ASSERT_THAT(AreEqual(EADSKRegisterResult::Accepted, Registry->Register(Scope)));
	}

	TEST_METHOD(CompilerTakesExactlyOneChild)
	{
		using namespace FCompilerSpecSupport;
		ASSERT_THAT(AreEqual(1, Load(ScopeXml(TEXT(""))).Count(EADSKDiagnosticCode::BadArity)));
		ASSERT_THAT(AreEqual(1, Load(ScopeXml(TEXT("<Wait id=\"a\"/><Wait id=\"b\"/>"))).Count(EADSKDiagnosticCode::BadArity)));
		ASSERT_THAT(IsTrue(FADSKNodeRegistry::GetDefault()->ExportSchemaJson().Contains(TEXT("\"kind\":\"sequence\"")), TEXT("the default schema has no sequence kind")));
		const TSharedRef<FADSKTestLeafLog> Log = MakeShared<FADSKTestLeafLog>();
		ASSERT_THAT(IsTrue(FADSKTestNodes::MakeFrozenRegistry(Log)->ExportSchemaJson().Contains(TEXT("\"name\":\"Scoped\",\"version\":1,\"kind\":\"scope\"")), TEXT("the test schema has no Scoped scope")));
	}
};

#endif

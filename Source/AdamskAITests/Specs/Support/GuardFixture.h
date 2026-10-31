// Copyright (c) 2026 Aleksandr Kardakov. Licensed under the MIT License. See LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

namespace FADSKGuardFixture
{
	inline const TCHAR* CloseTag = TEXT("</Agent>");

	/** Joins lines with LF so a spec can name an element's line by its position. */
	inline FString Join(std::initializer_list<const TCHAR*> Lines)
	{
		FString Result;
		for (const TCHAR* Line : Lines)
		{
			Result += Line;
			Result += TEXT("\n");
		}
		return Result;
	}

	/** 1-based line of the first occurrence of Needle, or 0. */
	inline int32 LineOf(const FString& Text, const char* Needle)
	{
		const int32 At = Text.Find(ANSI_TO_TCHAR(Needle), ESearchCase::CaseSensitive);
		if (At == INDEX_NONE)
		{
			return 0;
		}
		int32 Line = 1;
		for (int32 Index = 0; Index < At; ++Index)
		{
			Line += Text[Index] == TEXT('\n') ? 1 : 0;
		}
		return Line;
	}

	/** Inserts Fragment immediately before the root's closing tag; fails the test when it is absent. */
	inline FString InsertBeforeClose(FAutomationTestBase& Test, const FString& Xml, const FString& Fragment)
	{
		const int32 At = Xml.Find(CloseTag, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At == INDEX_NONE)
		{
			Test.AddError(TEXT("fixture has no closing </Agent> tag"));
			return Xml;
		}
		return Xml.Left(At) + Fragment + Xml.Mid(At);
	}

	/** An agent with sections in their required order: Inputs, Locals, Events, StateMachine, Trees. */
	inline FString AgentWith(const FString& Inputs, const FString& Locals, const FString& Events, const FString& Machine, const FString& Trees)
	{
		return TEXT("<Agent version=\"1\" id=\"guard\">\n") + Inputs + Locals + Events + Machine + Trees + CloseTag + TEXT("\n");
	}

	inline FString MinimalAgent()
	{
		return AgentWith(FString(), FString(), FString(),
			TEXT("<StateMachine id=\"mode\" initial=\"idle\">\n<State id=\"idle\"/>\n</StateMachine>\n"), FString());
	}

	inline FString HsmXml()
	{
		return Join({
			TEXT("<Agent version=\"1\" id=\"hsm\">"),
			TEXT("<Inputs><Input name=\"Req\" type=\"Vector\" required=\"true\"/></Inputs>"),
			TEXT("<Locals><Local name=\"L\" type=\"Vector\"/></Locals>"),
			TEXT("<Events>"),
			TEXT("<Event name=\"E1\"/><Event name=\"Lo\" priority=\"1\"><Param name=\"V\" type=\"Vector\"/></Event>"),
			TEXT("<Event name=\"Hi\" priority=\"10\"/><Event name=\"Up\"/><Event name=\"Self\"/><Event name=\"Back\"/><Event name=\"Noise\"/>"),
			TEXT("</Events>"),
			TEXT("<StateMachine id=\"m\" initial=\"a\">"),
			TEXT("<State id=\"a\" initial=\"a1\"><On event=\"E1\" goto=\"b\"/>"),
			TEXT("<State id=\"a1\" initial=\"a1x\"><On event=\"Up\" goto=\"a2\"/><On event=\"Self\" goto=\"a1\"/>"),
			TEXT("<State id=\"a1x\"><On event=\"E1\"/><On event=\"Lo\"><Bind param=\"V\" to=\"L\"/></On><On event=\"Hi\"/></State>"),
			TEXT("</State><State id=\"a2\"/></State>"),
			TEXT("<State id=\"b\"><On event=\"Back\" goto=\"a\"/></State>"),
			TEXT("</StateMachine></Agent>"),
		});
	}

	inline FString LeafXml()
	{
		return Join({
			TEXT("<Agent version=\"1\" id=\"leaf\">"),
			TEXT("<Inputs><Input name=\"Goal\" type=\"Vector\" required=\"true\"/></Inputs>"),
			TEXT("<Events><Event name=\"Go\" priority=\"10\"/><Event name=\"Again\"/><Event name=\"Instant\"/></Events>"),
			TEXT("<StateMachine id=\"m\" initial=\"idle\">"),
			TEXT("<State id=\"idle\" tree=\"move\"><On event=\"Go\" goto=\"fail\"/><On event=\"Again\" goto=\"idle\"/><On event=\"Instant\" goto=\"success\"/></State>"),
			TEXT("<State id=\"success\" tree=\"instant\"><On event=\"Again\" goto=\"idle\"/></State>"),
			TEXT("<State id=\"fail\" tree=\"acquire\"><On event=\"Again\" goto=\"idle\"/></State>"),
			TEXT("</StateMachine>"),
			TEXT("<Tree id=\"move\"><MoveTo id=\"move\" in-goal=\"Goal\" acceptanceRadius=\"5\"/></Tree>"),
			TEXT("<Tree id=\"instant\"><Wait id=\"instantWait\" duration=\"0\"/></Tree>"),
			TEXT("<Tree id=\"acquire\"><AcquireResource id=\"acquire\"/></Tree>"),
			TEXT("</Agent>"),
		});
	}

	inline FString CompositeXml()
	{
		return Join({
			TEXT("<Agent version=\"1\" id=\"composite\">"),
			TEXT("<Inputs><Input name=\"Goal\" type=\"Vector\" required=\"true\"/><Input name=\"Ok\" type=\"Bool\" default=\"true\"/><Input name=\"P\" type=\"Vector\" default=\"9,8,7\"/></Inputs>"),
			TEXT("<Locals><Local name=\"Seen\" type=\"Vector\"/></Locals>"),
			TEXT("<Events><Event name=\"ToSeq\"/><Event name=\"ToSense\"/><Event name=\"ToImmediate\"/></Events>"),
			TEXT("<StateMachine id=\"m\" initial=\"guard\">"),
			TEXT("<State id=\"guard\" tree=\"guardTree\"><On event=\"ToSeq\" goto=\"seq\"/></State>"),
			TEXT("<State id=\"seq\" tree=\"seqTree\"><On event=\"ToSense\" goto=\"sense\"/></State>"),
			TEXT("<State id=\"sense\" tree=\"senseTree\"><On event=\"ToImmediate\" goto=\"immediate\"/></State>"),
			TEXT("<State id=\"immediate\" tree=\"immediateTree\"/>"),
			TEXT("</StateMachine>"),
			TEXT("<Tree id=\"guardTree\"><Guard id=\"g\" in-condition=\"Ok\"><MoveTo id=\"guardMove\" in-goal=\"Goal\" acceptanceRadius=\"1\"/></Guard></Tree>"),
			TEXT("<Tree id=\"seqTree\"><Sequence id=\"seqRoot\"><MoveTo id=\"seqMove\" in-goal=\"Goal\" acceptanceRadius=\"1\"/><Wait id=\"wait\" duration=\"1\"/></Sequence></Tree>"),
			TEXT("<Tree id=\"senseTree\"><Sequence id=\"senseRoot\"><Sense id=\"senseNow\" out-point=\"Seen\"/><MoveTo id=\"seenMove\" in-goal=\"Seen\" acceptanceRadius=\"1\"/></Sequence></Tree>"),
			TEXT("<Tree id=\"immediateTree\"><Wait id=\"zero\" duration=\"0\"/></Tree>"),
			TEXT("</Agent>"),
		});
	}

	inline FString BudgetXml(bool bGuardTail = false)
	{
		FString Children;
		const int32 PlainCount = bGuardTail ? 63 : 70;
		for (int32 Index = 0; Index < PlainCount; ++Index)
		{
			Children += FString::Printf(TEXT("<Wait id=\"w%d\" duration=\"0\"/>"), Index);
		}
		if (bGuardTail)
		{
			Children += TEXT("<Guard id=\"tailGuard\" in-condition=\"Ok\"><Wait id=\"never\" duration=\"0\"/></Guard>");
		}
		return TEXT("<Agent version=\"1\" id=\"budget\"><Inputs><Input name=\"Ok\" type=\"Bool\" default=\"true\"/></Inputs>")
			TEXT("<StateMachine id=\"m\" initial=\"s\"><State id=\"s\" tree=\"t\"/></StateMachine><Tree id=\"t\"><Sequence id=\"root\">")
			+ Children + TEXT("</Sequence></Tree></Agent>");
	}

	/** The sample guard with only the `patrol` tree; the other trees are inserted before </Agent>. */
	inline FString GuardSample()
	{
		return Join({
			TEXT("<Agent version=\"1\" id=\"guard\">"),
			TEXT("  <Inputs>"),
			TEXT("    <Input name=\"PatrolA\" type=\"Vector\" required=\"true\"/>"),
			TEXT("    <Input name=\"PatrolB\" type=\"Vector\" required=\"true\"/>"),
			TEXT("    <Input name=\"MayPatrol\" type=\"Bool\" default=\"true\"/>"),
			TEXT("  </Inputs>"),
			TEXT("  <Locals>"),
			TEXT("    <Local name=\"Interest\" type=\"Vector\"/>"),
			TEXT("  </Locals>"),
			TEXT("  <Events>"),
			TEXT("    <Event name=\"Suspicious\" priority=\"10\">"),
			TEXT("      <Param name=\"Location\" type=\"Vector\"/>"),
			TEXT("    </Event>"),
			TEXT("    <Event name=\"ThreatConfirmed\" priority=\"20\">"),
			TEXT("      <Param name=\"Location\" type=\"Vector\"/>"),
			TEXT("    </Event>"),
			TEXT("    <Event name=\"ContactLost\" priority=\"20\"/>"),
			TEXT("  </Events>"),
			TEXT("  <StateMachine id=\"mode\" initial=\"active\">"),
			TEXT("    <State id=\"active\" initial=\"routine\">"),
			TEXT("      <On event=\"ThreatConfirmed\" goto=\"alert\">"),
			TEXT("        <Bind param=\"Location\" to=\"Interest\"/>"),
			TEXT("      </On>"),
			TEXT("      <State id=\"routine\" tree=\"patrol\">"),
			TEXT("        <On event=\"Suspicious\" goto=\"investigate\">"),
			TEXT("          <Bind param=\"Location\" to=\"Interest\"/>"),
			TEXT("        </On>"),
			TEXT("        <On event=\"tree.success\" goto=\"routine\"/>"),
			TEXT("      </State>"),
			TEXT("      <State id=\"investigate\" tree=\"go-look\">"),
			TEXT("        <On event=\"tree.success\" goto=\"search\"/>"),
			TEXT("        <On event=\"tree.failure\" goto=\"search\"/>"),
			TEXT("      </State>"),
			TEXT("      <State id=\"search\" tree=\"look-around\">"),
			TEXT("        <On event=\"tree.success\" goto=\"routine\"/>"),
			TEXT("        <On event=\"tree.failure\" goto=\"routine\"/>"),
			TEXT("      </State>"),
			TEXT("      <State id=\"alert\" tree=\"watch\">"),
			TEXT("        <On event=\"ThreatConfirmed\">"),
			TEXT("          <Bind param=\"Location\" to=\"Interest\"/>"),
			TEXT("        </On>"),
			TEXT("        <On event=\"ContactLost\" goto=\"search\"/>"),
			TEXT("        <On event=\"tree.success\" goto=\"alert\"/>"),
			TEXT("      </State>"),
			TEXT("    </State>"),
			TEXT("  </StateMachine>"),
			TEXT("  <Tree id=\"patrol\">"),
			TEXT("    <Guard id=\"may\" in-condition=\"MayPatrol\">"),
			TEXT("      <Sequence id=\"legs\">"),
			TEXT("        <MoveTo id=\"toA\" in-goal=\"PatrolA\" acceptanceRadius=\"50\"/>"),
			TEXT("        <Wait id=\"waitA\" duration=\"2\"/>"),
			TEXT("        <MoveTo id=\"toB\" in-goal=\"PatrolB\" acceptanceRadius=\"50\"/>"),
			TEXT("        <Wait id=\"waitB\" duration=\"2\"/>"),
			TEXT("      </Sequence>"),
			TEXT("    </Guard>"),
			TEXT("  </Tree>"),
			TEXT("</Agent>"),
		});
	}

	inline FString GuardTrees()
	{
		return Join({
			TEXT("  <Tree id=\"go-look\">"),
			TEXT("    <Sequence id=\"goLook\">"),
			TEXT("      <MoveTo id=\"toInterest\" in-goal=\"Interest\" acceptanceRadius=\"75\"/>"),
			TEXT("      <LookAtPoint id=\"lookInterest\" in-target=\"Interest\"/>"),
			TEXT("    </Sequence>"),
			TEXT("  </Tree>"),
			TEXT("  <Tree id=\"look-around\">"),
			TEXT("    <Sequence id=\"lookAround\">"),
			TEXT("      <LookAtPoint id=\"lookLeft\" in-target=\"PatrolA\"/>"),
			TEXT("      <Wait id=\"waitLook\" duration=\"1\"/>"),
			TEXT("      <LookAtPoint id=\"lookRight\" in-target=\"PatrolB\"/>"),
			TEXT("      <Wait id=\"waitLookEnd\" duration=\"1\"/>"),
			TEXT("    </Sequence>"),
			TEXT("  </Tree>"),
			TEXT("  <Tree id=\"watch\">"),
			TEXT("    <LookAtPoint id=\"watchInterest\" in-target=\"Interest\"/>"),
			TEXT("  </Tree>"),
		});
	}

	inline FString GuardXml(FAutomationTestBase& Test)
	{
		return InsertBeforeClose(Test, GuardSample(), GuardTrees());
	}

	inline FString GuardXmlWithUnusedTree(FAutomationTestBase& Test)
	{
		return InsertBeforeClose(Test, GuardXml(Test), TEXT("  <Tree id=\"unused\">\n    <Wait id=\"idleWait\" duration=\"1\"/>\n  </Tree>\n"));
	}
}

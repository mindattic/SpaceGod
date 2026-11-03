#include "Game/SpaceHUD.h"
#include "Game/SolSystem.h"
#include "Game/SpaceGameState.h"
#include "Game/SpacePawn.h"
#include "Game/SpacePlayerController.h"
#include "Sim/Astro.h"
#include "Sim/SimClock.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"

namespace
{
	const FLinearColor Text(0.85f, 0.9f, 1.f);
	const FLinearColor Dim(0.55f, 0.6f, 0.7f);
	const FLinearColor Accent(1.f, 0.8f, 0.35f);
	const FLinearColor Teammate(0.35f, 1.f, 0.55f);
	const FLinearColor PingColor(1.f, 0.45f, 0.3f);

	constexpr double LogSeconds = 8.0;
	constexpr double PingSeconds = 6.0;

	struct FHelpLine { const TCHAR* Keys; const TCHAR* Text; };

	/** The god-view keys of Unity.SpaceGod's ControlsHelp that this port has, plus the co-op ones. */
	const FHelpLine HelpLines[] =
	{
		{ TEXT("W A S D"), TEXT("swim (leaves focus)") },
		{ TEXT("Q / E  or  C / ]"), TEXT("down / up") },
		{ TEXT("Shift / Ctrl"), TEXT("boost / fine") },
		{ TEXT("Wheel"), TEXT("zoom (focused) / swim speed (free)") },
		{ TEXT("Right-drag"), TEXT("look / orbit") },
		{ TEXT("Left-click"), TEXT("select (double-click: focus)") },
		{ TEXT("F"), TEXT("focus the selection") },
		{ TEXT("Home"), TEXT("system view") },
		{ TEXT("1-9, 0"), TEXT("planets, Pluto, the Sun (twice to focus)") },
		{ TEXT("Tab / Shift+Tab"), TEXT("next / previous body") },
		{ TEXT("Space"), TEXT("pause (for everyone)") },
		{ TEXT(",  ."), TEXT("time warp slower / faster (for everyone)") },
		{ TEXT("T"), TEXT("back to now (for everyone)") },
		{ TEXT("G"), TEXT("toggle GAMEPLAY / REALISTIC scale (just you)") },
		{ TEXT("O / L"), TEXT("orbits / labels") },
		{ TEXT("M"), TEXT("CO-OP: mark the selection for everyone") },
		{ TEXT("J"), TEXT("CO-OP: jump to the next teammate's view") },
		{ TEXT("F9"), TEXT("CO-OP: host (friends join with  open <your ip>  in the ` console)") },
		{ TEXT("H / F1"), TEXT("this help") },
		{ TEXT("Esc"), TEXT("close help / deselect") },
	};

	FString FormatKm(double Km)
	{
		if (Km >= 0.1 * Astro::AuKm) return FString::Printf(TEXT("%.3f AU"), Km / Astro::AuKm);
		return FString::Printf(TEXT("%s km"), *FText::AsNumber(FMath::RoundToInt64(Km)).ToString());
	}
}

bool ASpaceHUD::ToScreen(const FVector& World, FVector2D& Out) const
{
	return PlayerOwner && PlayerOwner->ProjectWorldLocationToScreen(World, Out);
}

float ASpaceHUD::Line(const FString& String, float X, float Y, const FLinearColor& Color, UFont* Font)
{
	UFont* Use = Font ? Font : GEngine->GetSmallFont();
	DrawText(String, Color, X, Y, Use);
	float W = 0, H = 0;
	GetTextSize(String, W, H, Use);
	return Y + H + 2;
}

void ASpaceHUD::DrawCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Segments = 40;
	FVector2D Prev = Center + FVector2D(Radius, 0);
	for (int32 I = 1; I <= Segments; ++I)
	{
		const double A = Astro::TwoPi * I / Segments;
		const FVector2D Next = Center + FVector2D(std::cos(A), std::sin(A)) * Radius;
		DrawLine(Prev.X, Prev.Y, Next.X, Next.Y, Color, Thickness);
		Prev = Next;
	}
}

void ASpaceHUD::DrawHUD()
{
	Super::DrawHUD();
	const ASolSystem* Sol = ASolSystem::Get(this);
	const ASpaceGameState* State = GetWorld()->GetGameState<ASpaceGameState>();
	const ASpacePlayerController* PC = Cast<ASpacePlayerController>(PlayerOwner);
	if (!Sol || !State || !PC || !Canvas) return;

	if (Sol->bShowLabels) DrawLabels(Sol, PC);
	DrawTeammates(Sol);
	DrawPings(Sol, State);
	DrawStatus(Sol, State, PC);
	DrawLog(State);
	if (PC->IsHelpShown()) DrawHelp();
}

void ASpaceHUD::DrawLabels(const ASolSystem* Sol, const ASpacePlayerController* PC)
{
	const FBodyCatalog& Cat = Sol->GetCatalog();
	for (int32 I = 0; I < Cat.Num(); ++I)
	{
		const FBodyDefinition& B = Cat[I];
		if (!B.IsVisible()) continue;
		FVector2D P;
		if (!ToScreen(Sol->BodyWorld(I), P)) continue;

		// A moon's label waits until it has drawn clear of its planet's.
		if (B.Kind == EBodyKind::Moon)
		{
			FVector2D Parent;
			if (ToScreen(Sol->BodyWorld(B.ParentIndex), Parent) && (Parent - P).Size() < 40) continue;
		}

		const bool bSelected = I == PC->GetSelectedBody();
		const FLinearColor Color = bSelected ? Accent : (B.Kind == EBodyKind::Moon || B.Kind == EBodyKind::Asteroid ? Dim : Text);
		DrawRect(Color, P.X - 1.5f, P.Y - 1.5f, 3, 3);
		Line(B.Name, P.X + 6, P.Y - 7, Color);

		if (bSelected)
		{
			FVector2D Edge;
			const FVector Right = FRotationMatrix(PlayerOwner->PlayerCameraManager->GetCameraRotation()).GetScaledAxis(EAxis::Y);
			const float R = ToScreen(Sol->BodyWorld(I) + Right * Sol->BodyRadiusWorld(I), Edge) ? FMath::Max(10.f, float((Edge - P).Size()) + 6.f) : 10.f;
			DrawCircle(P, R, Accent, 1.f);
		}
	}
}

void ASpaceHUD::DrawTeammates(const ASolSystem* Sol)
{
	for (TActorIterator<ASpacePawn> It(GetWorld()); It; ++It)
	{
		const ASpacePawn* Other = *It;
		if (Other == PlayerOwner->GetPawn() || Other->GetSharedFrameBody() == INDEX_NONE) continue;
		FVector2D P;
		if (!ToScreen(Other->GetActorLocation(), P)) continue;
		DrawLine(P.X, P.Y - 7, P.X + 7, P.Y, Teammate, 2);
		DrawLine(P.X + 7, P.Y, P.X, P.Y + 7, Teammate, 2);
		DrawLine(P.X, P.Y + 7, P.X - 7, P.Y, Teammate, 2);
		DrawLine(P.X - 7, P.Y, P.X, P.Y - 7, Teammate, 2);
		const FString Near = Sol->GetCatalog()[Other->GetSharedFrameBody()].Name;
		Line(FString::Printf(TEXT("%s (near %s)"), *Other->GetPlayerLabel(), *Near), P.X + 10, P.Y - 7, Teammate);
	}
}

void ASpaceHUD::DrawPings(const ASolSystem* Sol, const ASpaceGameState* State)
{
	const double Now = FPlatformTime::Seconds();
	for (const FCoopPing& Ping : State->GetPings())
	{
		const double Age = Now - Ping.RealSeconds;
		if (Age > PingSeconds || Ping.Body < 0 || Ping.Body >= Sol->NumBodies()) continue;
		FVector2D P;
		if (!ToScreen(Sol->BodyWorld(Ping.Body), P)) continue;
		const float Pulse = float(FMath::Fmod(Age, 1.0));
		const FLinearColor Color(PingColor.R, PingColor.G, PingColor.B, 1.f - Pulse);
		DrawCircle(P, 14.f + 26.f * Pulse, Color, 2.f);
		Line(FString::Printf(TEXT("%s's mark"), *Ping.Player), P.X + 10, P.Y + 10, PingColor);
	}
}

void ASpaceHUD::DrawStatus(const ASolSystem* Sol, const ASpaceGameState* State, const ASpacePlayerController* PC)
{
	float Y = 16;
	const float X = 16;
	Y = Line(TEXT("UNREAL.SPACEGOD"), X, Y, Accent, GEngine->GetMediumFont());

	const int32 Players = State->PlayerArray.Num();
	const ENetMode Mode = GetNetMode();
	const TCHAR* Session = Mode == NM_Standalone ? TEXT("solo (F9 to host)") : Mode == NM_Client ? TEXT("co-op, joined") : TEXT("co-op, hosting");
	Y = Line(FString::Printf(TEXT("%s - %d player%s"), Session, Players, Players == 1 ? TEXT("") : TEXT("s")), X, Y, Dim);

	const FDateTime When = Astro::ToDateTime(State->CurrentJd());
	Y = Line(When.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")), X, Y + 6, Text, GEngine->GetMediumFont());
	const FString Warp = State->IsPaused() ? FString(TEXT("PAUSED")) : FString::Printf(TEXT("time warp %s"), SimClock::Levels[State->GetWarpIndex()].Label);
	Y = Line(Warp, X, Y, State->IsPaused() ? Accent : Text);
	const double Blend = Sol->GetScale().GetBlend();
	const FString ScaleText = Blend > 0.999 ? FString(TEXT("GAMEPLAY scale")) : Blend < 0.001 ? FString(TEXT("REALISTIC scale"))
		: FString::Printf(TEXT("%s scale (%.0f%%)"), Sol->IsGameplayScale() ? TEXT("-> GAMEPLAY") : TEXT("-> REALISTIC"), Blend * 100);
	Y = Line(ScaleText, X, Y, Text);

	const ASpacePawn* Me = Cast<ASpacePawn>(PlayerOwner->GetPawn());
	const int32 Shown = PC->GetSelectedBody() != INDEX_NONE ? PC->GetSelectedBody() : Me ? Me->GetFrameBody() : INDEX_NONE;
	if (Shown != INDEX_NONE)
	{
		const FBodyCatalog& Cat = Sol->GetCatalog();
		const FBodyDefinition& B = Cat[Shown];
		const FEphemeris& Eph = Sol->GetEphemeris();
		Y += 10;
		Y = Line(B.Name, X, Y, Accent, GEngine->GetMediumFont());
		FString Kind = BodyKindName(B.Kind);
		if (B.ParentIndex > 0) Kind += FString::Printf(TEXT(" of %s"), *Cat[B.ParentIndex].Name);
		if (Me && Me->IsFocused() && Me->GetFrameBody() == Shown) Kind += TEXT(" - focused");
		Y = Line(Kind, X, Y, Dim);
		Y = Line(FString::Printf(TEXT("radius %s"), *FormatKm(B.RadiusKm)), X, Y, Text);
		if (Shown != 0) Y = Line(FString::Printf(TEXT("from the Sun %s"), *FormatKm(Eph.Helio(Shown).Size())), X, Y, Text);
		if (B.ParentIndex > 0) Y = Line(FString::Printf(TEXT("from %s %s"), *Cat[B.ParentIndex].Name, *FormatKm(Eph.Relative(Shown).Size())), X, Y, Text);
		if (B.ParentIndex >= 0) Y = Line(FString::Printf(TEXT("orbital speed %.2f km/s"), Eph.OrbitalSpeedKmS(Shown)), X, Y, Text);
		if (!B.Summary.IsEmpty()) Y = Line(B.Summary, X, Y + 4, Dim);
	}

	Line(TEXT("H / F1: help"), X, Canvas->ClipY - 28, Dim);

	// Who is here, bottom right.
	float RY = Canvas->ClipY - 28 - 16 * Players;
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (!Player) continue;
		const bool bMe = Player == PlayerOwner->PlayerState;
		RY = Line(Player->GetPlayerName() + (bMe ? TEXT(" (you)") : TEXT("")), Canvas->ClipX - 260, RY, bMe ? Text : Teammate);
	}
}

void ASpaceHUD::DrawLog(const ASpaceGameState* State)
{
	const double Now = FPlatformTime::Seconds();
	float Y = Canvas->ClipY * 0.62f;
	for (const FCoopAnnouncement& Entry : State->GetAnnouncements())
	{
		const double Age = Now - Entry.RealSeconds;
		if (Age > LogSeconds) continue;
		const float Alpha = float(FMath::Clamp((LogSeconds - Age) / 2.0, 0.0, 1.0));
		Y = Line(Entry.Text, 16, Y, FLinearColor(Teammate.R, Teammate.G, Teammate.B, Alpha));
	}
}

void ASpaceHUD::DrawHelp()
{
	const float W = 620, H = 40 + 18 * UE_ARRAY_COUNT(HelpLines);
	const float X = (Canvas->ClipX - W) / 2, Top = (Canvas->ClipY - H) / 2;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.85f), X, Top, W, H);
	float Y = Line(TEXT("CONTROLS"), X + 16, Top + 10, Accent, GEngine->GetMediumFont());
	for (const FHelpLine& L : HelpLines)
	{
		Line(L.Keys, X + 16, Y, Accent);
		Y = Line(L.Text, X + 200, Y, Text);
	}
}

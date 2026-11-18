#include "UI/PFMapWidget.h"
#include "Framework/PFGameState.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Progression/PFPlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"

int32 UPFMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
	const int32 Base = Super::NativePaint(Args, Geometry, Culling, Elements, Layer, Style, Enabled);
	const APFGameState* State = GetWorld() ? GetWorld()->GetGameState<APFGameState>() : nullptr; if (!State) return Base;
	const FVector2D Size = Geometry.GetLocalSize();
	auto Point = [Size](const FVector& Location)
	{
		return FVector2D(FMath::Clamp(.5f + Location.Y / 100000.f, 0.f, 1.f) * Size.X,
			FMath::Clamp(.5f - Location.X / 100000.f, 0.f, 1.f) * Size.Y);
	};
	auto Line = [&](const TArray<FVector2D>& Points, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(Elements, Base + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, Thickness);
	};
	TArray<FSlateRect> LabelRects;
	auto Text = [&](FVector2D Anchor, const FString& Value, FLinearColor Color)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12);
		const FVector2D LabelSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, Font) + FVector2D(8.f, 4.f);
		for (int32 Attempt = 0; Attempt < 40; ++Attempt)
		{
			const float Row = static_cast<float>((Attempt + 1) / 2);
			const float Offset = Row * (Attempt % 2 ? 20.f : -20.f);
			const FVector2D Position(FMath::Clamp(Anchor.X, 4., FMath::Max(4., Size.X - LabelSize.X - 4.)),
				FMath::Clamp(Anchor.Y + Offset, 4., FMath::Max(4., Size.Y - LabelSize.Y - 4.)));
			const FSlateRect Rect(Position.X - 2.f, Position.Y - 2.f, Position.X + LabelSize.X + 2.f, Position.Y + LabelSize.Y + 2.f);
			const bool Overlap = LabelRects.ContainsByPredicate([&Rect](const FSlateRect& Other)
			{
				return Rect.Left < Other.Right && Rect.Right > Other.Left && Rect.Top < Other.Bottom && Rect.Bottom > Other.Top;
			});
			if (Overlap) continue;
			LabelRects.Add(Rect);
			if (FVector2D::Distance(Position, Anchor) > 8.f) Line({Anchor, Position + FVector2D(0.f, LabelSize.Y * .5f)}, Color.CopyWithNewOpacity(.55f), 1.f);
			FSlateDrawElement::MakeBox(Elements, Base + 2, Geometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(Position)),
				FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, .68f));
			FSlateDrawElement::MakeText(Elements, Base + 3, Geometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(Position + FVector2D(4.f, 2.f))),
				Value, Font, ESlateDrawEffect::None, Color);
			return;
		}
	};
	FSlateDrawElement::MakeBox(Elements, Base, Geometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, FLinearColor(.025f, .07f, .065f, 1.f));
	if (MapTexture)
	{
		FSlateBrush Brush; Brush.SetResourceObject(MapTexture); Brush.ImageSize = FVector2D(MapTexture->GetSizeX(), MapTexture->GetSizeY()); Brush.DrawAs = ESlateBrushDrawType::Image;
		FSlateDrawElement::MakeBox(Elements, Base, Geometry.ToPaintGeometry(), &Brush, ESlateDrawEffect::None, FLinearColor::White);
	}
	for (int32 Index = 1; Index < 10; ++Index)
	{
		Line({FVector2D(Size.X * Index / 10.f, 0), FVector2D(Size.X * Index / 10.f, Size.Y)}, FLinearColor(.1f, .2f, .17f, 1.f), 1.f);
		Line({FVector2D(0, Size.Y * Index / 10.f), FVector2D(Size.X, Size.Y * Index / 10.f)}, FLinearColor(.1f, .2f, .17f, 1.f), 1.f);
	}
	Text(FVector2D(10, 8), TEXT("N ↑   /   확인한 정보만 표시"), FLinearColor(.75f, .85f, .75f));
	for (const FPFOutpostRecord& Record : State->Outposts)
	{
		if (Record.Phase == EPFOutpostPhase::Undiscovered) continue;
		const FVector2D Center = Point(Record.MapLocation);
		const FLinearColor Color = Record.Phase == EPFOutpostPhase::Captured ? FLinearColor(.7f, .85f, .3f) : FLinearColor(1.f, .65f, .3f);
		Line({Center + FVector2D(0, -8), Center + FVector2D(8, 0), Center + FVector2D(0, 8), Center + FVector2D(-8, 0), Center + FVector2D(0, -8)}, Color, 2.f);
		Text(Center + FVector2D(12, -8), (Record.Title.IsEmpty() ? Record.Id.ToString() : Record.Title.ToString()) + (Record.Phase == EPFOutpostPhase::Captured ? TEXT(" [확보]") : TEXT(" [발견]")), Color);
		for (FName Target : Record.Supports)
			if (const FPFOutpostRecord* Other = State->Outposts.FindByPredicate([Target](const FPFOutpostRecord& Entry) { return Entry.Id == Target; }))
				if (Other->Phase != EPFOutpostPhase::Undiscovered) Line({Center, Point(Other->MapLocation)}, Color.CopyWithNewOpacity(.45f), 1.f);
	}
	const float Now = State->GetServerWorldTimeSeconds();
	for (int32 Index = 0; Index < State->PlayerArray.Num(); ++Index)
	{
		const APFPlayerState* Mate = Cast<APFPlayerState>(State->PlayerArray[Index]);
		if (!Mate || !Mate->bJoinedWorld || (GetOwningPlayer() && Mate == GetOwningPlayer()->PlayerState)) continue;
		const FVector2D Center = Point(Mate->TeamLocation);
		const FLinearColor Color = Mate->TeamLife == EPFLifeState::Downed ? FLinearColor(1.f, .6f, .2f) : FLinearColor(.4f, .8f, 1.f);
		Line({Center + FVector2D(-5, 0), Center + FVector2D(0, -5), Center + FVector2D(5, 0), Center + FVector2D(0, 5), Center + FVector2D(-5, 0)}, Color, 2.f);
		Text(Center + FVector2D(9, 4), FString::Printf(TEXT("%d · %s"), Index + 1, Mate->TeamLife == EPFLifeState::Downed ? TEXT("소생 필요") : TEXT("팀원")), Color);
	}
	for (const FPFReconMark& Mark : State->ReconMarks)
	{
		if (Now - Mark.ObservedTime > 90.f) continue;
		const FVector2D Center = Point(Mark.Location);
		const bool Fresh = Now - Mark.ObservedTime < 1.f;
		const FLinearColor Color = Fresh ? FLinearColor(1.f, .45f, .3f) : FLinearColor(.65f, .55f, .45f);
		Line({Center + FVector2D(-3, -3), Center + FVector2D(3, 3)}, Color, 2.f);
		Line({Center + FVector2D(-3, 3), Center + FVector2D(3, -3)}, Color, 2.f);
		if (!Fresh) Text(Center + FVector2D(7, 0), TEXT("마지막 관찰"), Color);
	}
	if (const APlayerController* PC = GetOwningPlayer()) if (const APawn* Pawn = PC->GetPawn())
	{
		const FVector2D Center = Point(Pawn->GetActorLocation());
		Line({Center + FVector2D(0, -7), Center + FVector2D(5, 5), Center + FVector2D(-5, 5), Center + FVector2D(0, -7)}, FLinearColor::White, 2.f);
		Text(Center + FVector2D(9, -9), TEXT("현재 위치"), FLinearColor::White);
	}
	return Base + 4;
}

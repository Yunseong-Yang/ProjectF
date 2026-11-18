#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PFMapWidget.generated.h"

/** The tactical map draws confirmed facilities and last observed recon positions only. */
UCLASS()
class PROJECTF_API UPFMapWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(Transient) TObjectPtr<class UTexture2D> MapTexture;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
		FSlateWindowElementList& DrawElements, int32 Layer, const FWidgetStyle& Style, bool Enabled) const override;
};

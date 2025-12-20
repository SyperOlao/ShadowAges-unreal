// Copyright Narrative Tools 2022. 

#include "DialogueConnectionDrawingPolicy.h"
#include "QuestSM.h"
#include "DialogueGraphNode_NPC.h"
#include <EdGraph/EdGraphNode.h>

FVector2D FDialogueGraphConnectionDrawingPolicy::ComputeSplineTangent(const FVector2D& Start, const FVector2D& End) const
{
	return FConnectionDrawingPolicy::ComputeSplineTangent(Start, End);
}

void FDialogueGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.WireColor = FLinearColor::White;
	Params.WireThickness = 2.f;

	UEdGraphNode* OutNode = OutputPin ? OutputPin->GetOwningNode() : nullptr;
	UEdGraphNode* InNode = InputPin ? InputPin->GetOwningNode() : nullptr;

	if (OutNode && InNode)
	{
		if (OutNode->NodePosX > InNode->NodePosX)
		{
			Params.WireColor.A = 0.3f;
		}
	}
}

void FDialogueGraphConnectionDrawingPolicy::DrawConnection(int32 LayerId, const FVector2D& Start, const FVector2D& End, const FConnectionParams& Params)
{
	//If we're linking backwards, actually draw two splines that meet halfway, to act like a reroute pin might.
	//Basically just makes backlinks beautiful instead of messy and hard to follow
	if (Start.X > End.X)
	{
		FVector2D Halfway = (Start + End) / 2;
		Halfway.Y -= 300.f * ZoomFactor;

		DrawConnection(LayerId, Halfway, Start, Params);
		DrawConnection(LayerId, End, Halfway, Params);

		// Draw the arrow
		if (BacklinkImage != nullptr)
		{
			FVector2D ArrowPoint = Halfway - ArrowRadius;

			FSlateDrawElement::MakeRotatedBox(
				DrawElementsList,
				ArrowLayerID,
				FPaintGeometry(ArrowPoint, BacklinkImage->ImageSize * ZoomFactor, ZoomFactor),
				BacklinkImage,
				ESlateDrawEffect::None,
				PI,
				TOptional<FVector2D>(),
				FSlateDrawElement::RelativeToElement,
				Params.WireColor
			);
		}
	}
	else
	{
		FConnectionDrawingPolicy::DrawConnection(LayerId, Start, End, Params);
	}
}

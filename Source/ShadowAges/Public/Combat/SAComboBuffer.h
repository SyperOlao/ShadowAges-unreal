#pragma once
#include "CoreMinimal.h"
#include "../Core/Types/SAActionTypes.h"

enum class ESAComboBufferState: uint8
{
	Empty,
	Pending,
	Reserved
};

struct SHADOWAGES_API FSAComboBuffer
{
public:
	void Reset();
	bool Submit(FSAActionHandle Action, int32 Generation, double Now, float Lifetime);
	bool TryReserve(FSAActionHandle Action, int32 Generation, double AcceptTime);
	bool IsReserved(FSAActionHandle Action, int32 Generation);
	void Expire(double Now);
	ESAComboBufferState GetState() const;
private:
	bool BelongsTo(FSAActionHandle Action, int32 Generation) const;
	ESAComboBufferState State = ESAComboBufferState::Empty;
	FSAActionHandle OwnerAction;
	int32 OwnerGeneration = 0;
	double SubmittedAt = 0.0;
	double ExpiresAt = 0.0;
};

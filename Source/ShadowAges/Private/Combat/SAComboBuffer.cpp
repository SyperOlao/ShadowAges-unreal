#include "Combat/SAComboBuffer.h"

void FSAComboBuffer::Reset()
{
	*this = FSAComboBuffer{};
}

bool FSAComboBuffer::Submit(FSAActionHandle Action, int32 Generation, double Now, float Lifetime)
{
	if (!Action.IsValid() || Generation <= 0 || !FMath::IsFinite(Now) || !FMath::IsFinite(Lifetime) || Lifetime < 0.0f)
	{
		return false;
	}
	Expire(Now);
	if (BelongsTo(Action, Generation))
	{
		return true;
	}
	Reset();
	State = ESAComboBufferState::Pending;
	OwnerAction = Action;
	OwnerGeneration = Generation;
	SubmittedAt = Now;
	ExpiresAt = Now + Lifetime;
	return true;
}

bool FSAComboBuffer::TryReserve(FSAActionHandle Action, int32 Generation, double AcceptTime)
{
	if (!BelongsTo(Action, Generation))
	{
		return false;
	}
	if (State == ESAComboBufferState::Reserved)
	{
		return true;
	}
	if (!FMath::IsFinite(AcceptTime) || AcceptTime < SubmittedAt || AcceptTime > ExpiresAt)
	{
		return false;
	}
	State = ESAComboBufferState::Reserved;
	return true;
}

bool FSAComboBuffer::IsReserved(FSAActionHandle Action, int32 Generation)
{
	return State == ESAComboBufferState::Reserved && BelongsTo(Action, Generation);
}

bool FSAComboBuffer::Consume(FSAActionHandle Action, int32 Generation)
{
	if (!IsReserved(Action, Generation))
	{
		return false;
	}
	Reset();
	return true;
}

void FSAComboBuffer::Expire(double Now)
{
	if (State == ESAComboBufferState::Pending && Now > ExpiresAt)
	{
		Reset();
	}
}

ESAComboBufferState FSAComboBuffer::GetState() const
{
	return State;
}

bool FSAComboBuffer::BelongsTo(FSAActionHandle Action, int32 Generation) const
{
	return State != ESAComboBufferState::Empty
		&& OwnerAction == Action && OwnerGeneration == Generation;
}

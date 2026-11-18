#pragma once

#include "CoreMinimal.h"
#include "Core/PFTypes.h"

/** Bounded local cosmetic history. It never authorizes a shot, damage or ammunition. */
struct FPFShotEffectLedger
{
	bool Record(uint32 Request, uint32 Ordinal, EPFWeaponType Weapon, double Now)
	{
		Prune(Now);
		const uint64 Id = Key(Request, Ordinal, Weapon);
		if (Pending.Contains(Id)) return false;
		if (Pending.Num() >= 64)
		{
			uint64 OldestKey = 0; double Oldest = TNumericLimits<double>::Max();
			for (const auto& Entry : Pending) if (Entry.Value < Oldest) { Oldest = Entry.Value; OldestKey = Entry.Key; }
			Pending.Remove(OldestKey);
		}
		Pending.Add(Id, Now); return true;
	}
	int32 Num() const { return Pending.Num(); }
private:
	TMap<uint64, double> Pending;
	static uint64 Key(uint32 Request, uint32 Ordinal, EPFWeaponType Weapon)
	{
		return (uint64(Request) << 32) | (uint64(uint8(Weapon)) << 24) | (Ordinal & 0xffffffu);
	}
	void Prune(double Now)
	{
		for (auto It = Pending.CreateIterator(); It; ++It) if (Now - It.Value() > 5.0) It.RemoveCurrent();
	}
};

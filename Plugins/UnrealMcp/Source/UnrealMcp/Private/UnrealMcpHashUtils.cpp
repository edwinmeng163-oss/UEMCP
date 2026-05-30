#include "UnrealMcpHashUtils.h"

namespace UnrealMcp::HashUtils
{
	namespace
	{
		uint32 HashUtilsSha256RotateRight(uint32 Value, uint32 Shift)
		{
			return (Value >> Shift) | (Value << (32 - Shift));
		}
	}

	FString Sha256LowerHex(const TArray<uint8>& Bytes)
	{
		static const uint32 Constants[64] =
		{
			0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
			0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
			0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
			0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
			0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
			0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
			0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
			0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
		};

		TArray<uint8> Padded = Bytes;
		const uint64 BitLength = static_cast<uint64>(Bytes.Num()) * 8ull;
		Padded.Add(0x80u);
		while ((Padded.Num() % 64) != 56)
		{
			Padded.Add(0u);
		}
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Padded.Add(static_cast<uint8>((BitLength >> Shift) & 0xffu));
		}

		uint32 Hash[8] =
		{
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
		};

		for (int32 ChunkOffset = 0; ChunkOffset < Padded.Num(); ChunkOffset += 64)
		{
			uint32 Words[64] = {};
			for (int32 Index = 0; Index < 16; ++Index)
			{
				const int32 Offset = ChunkOffset + Index * 4;
				Words[Index] =
					(static_cast<uint32>(Padded[Offset]) << 24)
					| (static_cast<uint32>(Padded[Offset + 1]) << 16)
					| (static_cast<uint32>(Padded[Offset + 2]) << 8)
					| static_cast<uint32>(Padded[Offset + 3]);
			}
			for (int32 Index = 16; Index < 64; ++Index)
			{
				const uint32 S0 = HashUtilsSha256RotateRight(Words[Index - 15], 7) ^ HashUtilsSha256RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
				const uint32 S1 = HashUtilsSha256RotateRight(Words[Index - 2], 17) ^ HashUtilsSha256RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
				Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
			}

			uint32 A = Hash[0];
			uint32 B = Hash[1];
			uint32 C = Hash[2];
			uint32 D = Hash[3];
			uint32 E = Hash[4];
			uint32 F = Hash[5];
			uint32 G = Hash[6];
			uint32 H = Hash[7];

			for (int32 Index = 0; Index < 64; ++Index)
			{
				const uint32 S1 = HashUtilsSha256RotateRight(E, 6) ^ HashUtilsSha256RotateRight(E, 11) ^ HashUtilsSha256RotateRight(E, 25);
				const uint32 Choice = (E & F) ^ ((~E) & G);
				const uint32 Temp1 = H + S1 + Choice + Constants[Index] + Words[Index];
				const uint32 S0 = HashUtilsSha256RotateRight(A, 2) ^ HashUtilsSha256RotateRight(A, 13) ^ HashUtilsSha256RotateRight(A, 22);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = S0 + Majority;

				H = G;
				G = F;
				F = E;
				E = D + Temp1;
				D = C;
				C = B;
				B = A;
				A = Temp1 + Temp2;
			}

			Hash[0] += A;
			Hash[1] += B;
			Hash[2] += C;
			Hash[3] += D;
			Hash[4] += E;
			Hash[5] += F;
			Hash[6] += G;
			Hash[7] += H;
		}

		static const TCHAR Hex[] = TEXT("0123456789abcdef");
		FString Result;
		Result.Reserve(64);
		for (uint32 Word : Hash)
		{
			for (int32 Shift = 24; Shift >= 0; Shift -= 8)
			{
				const uint8 Byte = static_cast<uint8>((Word >> Shift) & 0xffu);
				Result.AppendChar(Hex[(Byte >> 4) & 0x0f]);
				Result.AppendChar(Hex[Byte & 0x0f]);
			}
		}
		return Result;
	}

	FString Sha256LowerHexFromUtf8(const FString& Text)
	{
		TArray<uint8> Bytes;
		FTCHARToUTF8 Converter(*Text);
		Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
		return Sha256LowerHex(Bytes);
	}
}

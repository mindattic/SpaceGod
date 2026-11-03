// The C# compatibility layer's out-of-line half: System.Random, Math.Round, double/float/int ToString with the
// standard and custom numeric formats, Parse/TryParse and the string helpers. Each reproduces .NET (Core 3.0+,
// as the C# Core runs on net10.0) with the invariant culture; Tests/CsParity checks them against real .NET.

#include "Cs/Cs.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <random>
#include <cstdio>

namespace cs
{
	// ============================================================ layout guard (US-T49) ========================
	// Every object that includes Cs.h calls this while its module loads (Cs.h, "layout guard"). A difference means
	// the objects were compiled against different versions of Cs.h (a stale incremental build): stop before any
	// container is touched, rather than crash later in BucketOf or corrupt the heap.
	int Abi::Check(uint64_t objectFingerprint)
	{
		if (objectFingerprint == Fingerprint) return 0;
		std::fprintf(stderr, "cs::Abi::Check: an object was compiled against another Cs/Cs.h (layout fingerprint %llu, "
			"SpaceGodCore's Cs.cpp has %llu). The build is stale or mixed: rebuild the modules (UBT -Clean, or "
			"tools/build-core.ps1 -Rebuild) so every object sees the same header.\n",
			static_cast<unsigned long long>(objectFingerprint), static_cast<unsigned long long>(Fingerprint));
		std::fflush(stderr);
		std::abort();
	}

	// ============================================================ System.Random ================================
	// dotnet/runtime src/libraries/System.Private.CoreLib/src/System/Random.Net5CompatImpl.cs (CompatPrng).
	namespace
	{
		constexpr int MBIG = std::numeric_limits<int>::max();
		constexpr int MSEED = 161803398;
	}

	Random::Random() : Random(static_cast<int>(std::random_device{}() & 0x7fffffffu)) {}

	Random::Random(int seed)
	{
		int* seedArrayP = seedArray;
		int ii = 0;
		int subtraction = (seed == std::numeric_limits<int>::min()) ? std::numeric_limits<int>::max() : (seed < 0 ? -seed : seed);
		int mj = MSEED - subtraction;
		seedArrayP[55] = mj;
		int mk = 1;
		for (int i = 1; i < 55; i++)
		{
			// Apparently the range [1..55] is special (Knuth) and so we're wasting the 0'th position.
			if ((ii += 21) >= 55) ii -= 55;
			seedArrayP[ii] = mk;
			mk = mj - mk;
			if (mk < 0) mk += MBIG;
			mj = seedArrayP[ii];
		}
		for (int k = 1; k < 5; k++)
		{
			for (int i = 1; i < 56; i++)
			{
				int n = i + 30;
				if (n >= 55) n -= 55;
				seedArrayP[i] = WrapSub(seedArrayP[i], seedArrayP[1 + n]);
				if (seedArrayP[i] < 0) seedArrayP[i] += MBIG;
			}
		}
		inext = 0;
		inextp = 21;
	}

	int Random::InternalSample()
	{
		int locINext = inext;
		if (++locINext >= 56) locINext = 1;
		int locINextp = inextp;
		if (++locINextp >= 56) locINextp = 1;
		int retVal = seedArray[locINext] - seedArray[locINextp];
		if (retVal == MBIG) retVal--;
		if (retVal < 0) retVal += MBIG;
		seedArray[locINext] = retVal;
		inext = locINext;
		inextp = locINextp;
		return retVal;
	}

	// Including the division at the end gives us significantly improved random number distribution.
	double Random::Sample() { return InternalSample() * (1.0 / MBIG); }

	double Random::GetSampleForLargeRange()
	{
		// The distribution of the double returned by Sample is not good enough for a large range.
		// If we use Sample for a range [int.MinValue..int.MaxValue), we will end up getting even numbers only.
		int result = InternalSample();
		// We can't use addition here: the distribution will be bad if we do that.
		if (InternalSample() % 2 == 0) result = -result; // decide the sign based on second sample
		double d = result;
		d += (std::numeric_limits<int>::max() - 1); // get a number in range [0..2*int.MaxValue-1)
		d /= 2.0 * std::numeric_limits<int>::max() - 1;
		return d;
	}

	int Random::Next() { return InternalSample(); }

	int Random::Next(int maxValue)
	{
		if (maxValue < 0) throw ArgumentOutOfRangeException("'maxValue' must be greater than or equal to '0'.");
		return static_cast<int>(Sample() * maxValue);
	}

	int Random::Next(int minValue, int maxValue)
	{
		if (minValue > maxValue) throw ArgumentOutOfRangeException("'minValue' cannot be greater than maxValue.");
		long long range = static_cast<long long>(maxValue) - minValue;
		if (range <= std::numeric_limits<int>::max())
			return static_cast<int>(static_cast<int>(Sample() * static_cast<double>(range)) + static_cast<long long>(minValue));
		return static_cast<int>(static_cast<long long>(GetSampleForLargeRange() * static_cast<double>(range)) + minValue);
	}

	double Random::NextDouble() { return Sample(); }

	void Random::NextBytes(List<uint8_t>& buffer)
	{
		for (auto& b : buffer) b = static_cast<uint8_t>(InternalSample());
	}

	// ============================================================ Math.Round ===================================
	// dotnet/runtime Math.cs: Round(double value, int digits, MidpointRounding mode).
	namespace Math
	{
		namespace
		{
			constexpr double RoundPower10Double[] = { 1E0, 1E1, 1E2, 1E3, 1E4, 1E5, 1E6, 1E7, 1E8, 1E9, 1E10, 1E11, 1E12, 1E13, 1E14, 1E15 };
			constexpr double DoubleRoundLimit = 1e16;
		}

		double Round(double value, int digits, MidpointRounding mode)
		{
			if (static_cast<unsigned>(digits) > 15u) throw ArgumentOutOfRangeException("Rounding digits must be between 0 and 15, inclusive.");
			if (std::fabs(value) < DoubleRoundLimit)
			{
				double power10 = RoundPower10Double[digits];
				value *= power10;
				switch (mode)
				{
				case MidpointRounding::ToEven:
					// Rounds to the nearest value; if the number falls midway, it is rounded to the nearest value with an even least significant digit
					value = std::nearbyint(value);
					break;
				case MidpointRounding::AwayFromZero:
					// For compatibility, this is "round half away from zero": Truncate(value + CopySign(BitDecrement(0.5), value))
					value = std::trunc(value + std::copysign(0.49999999999999994, value));
					break;
				}
				value /= power10;
			}
			return value;
		}

		double Round(double value, int digits) { return Round(value, digits, MidpointRounding::ToEven); }
		double Round(double value, MidpointRounding mode) { return Round(value, 0, mode); }
	}

	// ============================================================ number formatting ============================
	// A port of dotnet/runtime src/libraries/Common/src/System/Number.Formatting.cs (NumberBuffer, RoundNumber,
	// NumberToString, NumberToStringFormat, FormatFixed/General/Scientific/Exponent) for the invariant culture.
	// .NET produces a double's digits with Grisu/Dragon4: the shortest round-trip digits for "R"/"G"/ToString(),
	// correctly rounded digits of the exact binary value for a precision ("F2", "G6", "E3", "N0", "P1"), and for a
	// custom format ("0.##") 15 significant digits (7 for float) that the custom formatter then rounds again,
	// half away from zero on the decimal digits -- .NET's documented double rounding, kept for parity.
	namespace
	{
		struct Number
		{
			std::string Digits; // significant digits, no trailing zeros; value = 0.Digits * 10^Scale
			int Scale = 0;
			bool IsNegative = false;
			bool IsFloatingPoint = false;
			char Dig(int i) const { return i >= 0 && i < static_cast<int>(Digits.size()) ? Digits[static_cast<size_t>(i)] : '\0'; }
		};

		constexpr int DoubleMaxRoundTripDigits = 17;
		constexpr int SingleMaxRoundTripDigits = 9;
		constexpr int DoublePrecisionCustomFormat = 15;
		constexpr int SinglePrecisionCustomFormat = 7;

		// Splits to_chars scientific output "d.ddde+XX" into digits (trailing zeros dropped) and scale.
		void ParseScientific(const char* first, const char* last, Number& n)
		{
			n.Digits.clear();
			const char* p = first;
			const char* e = static_cast<const char*>(std::memchr(first, 'e', static_cast<size_t>(last - first)));
			for (; p < e; ++p)
				if (*p >= '0' && *p <= '9') n.Digits.push_back(*p);
			int exp = 0;
			std::from_chars(e + 1 + (e[1] == '+' ? 1 : 0), last, exp);
			while (!n.Digits.empty() && n.Digits.back() == '0') n.Digits.pop_back();
			n.Scale = n.Digits.empty() ? 0 : exp + 1;
		}

		// The exact decimal expansion of a finite nonzero double (at most 767 significant digits).
		void ExactDigits(double a, Number& n)
		{
			char buf[800];
			auto r = std::to_chars(buf, buf + sizeof(buf), a, std::chars_format::scientific, 767);
			ParseScientific(buf, r.ptr, n);
		}

		void ShortestDigits(double a, bool isFloat, Number& n)
		{
			char buf[64];
			auto r = isFloat ? std::to_chars(buf, buf + sizeof(buf), static_cast<float>(a), std::chars_format::scientific)
			                 : std::to_chars(buf, buf + sizeof(buf), a, std::chars_format::scientific);
			ParseScientific(buf, r.ptr, n);
		}

		// Correct rounding of exact digits to `pos` digits (Dragon4's cutoff). An exact tie (the dropped part is
		// exactly one half) goes to the even digit, as .NET's Dragon4 does: 0.5 "F0" -> "0", 2.5 -> "2", 0.125 "F2" -> "0.12".
		void RoundExact(Number& n, int pos)
		{
			int count = static_cast<int>(n.Digits.size());
			if (pos >= count) return;
			if (pos < 0) { n.Digits.clear(); n.Scale = 0; return; }
			char first = n.Digits[static_cast<size_t>(pos)];
			bool up = first > '5' || (first == '5' && (count > pos + 1 || (pos > 0 && ((n.Digits[static_cast<size_t>(pos - 1)] - '0') & 1) != 0)));
			n.Digits.resize(static_cast<size_t>(pos));
			if (up)
			{
				int i = pos;
				while (i > 0 && n.Digits[static_cast<size_t>(i - 1)] == '9') i--;
				if (i > 0) { n.Digits.resize(static_cast<size_t>(i)); n.Digits[static_cast<size_t>(i - 1)]++; }
				else { n.Digits = "1"; n.Scale++; }
			}
			while (!n.Digits.empty() && n.Digits.back() == '0') n.Digits.pop_back();
			if (n.Digits.empty()) n.Scale = 0;
		}

		// Number.RoundNumber.
		void RoundNumber(Number& number, int pos, bool isCorrectlyRounded)
		{
			std::string& dig = number.Digits;
			int i = 0;
			while (i < pos && number.Dig(i) != '\0') i++;
			auto shouldRoundUp = [&](int at)
			{
				// We only want to round up if the digit is greater than or equal to 5 and we are not rounding a
				// floating-point number whose standard-format digits are already exact and correctly rounded.
				char digit = number.Dig(at);
				if (digit == '\0' || isCorrectlyRounded) return false;
				return digit >= '5';
			};
			if (i == pos && shouldRoundUp(i))
			{
				while (i > 0 && dig[static_cast<size_t>(i - 1)] == '9') i--;
				if (i > 0) dig[static_cast<size_t>(i - 1)]++;
				else { number.Scale++; dig = "1"; i = 1; }
			}
			else
			{
				while (i > 0 && dig[static_cast<size_t>(i - 1)] == '0') i--;
			}
			if (i == 0)
			{
				// The integer types don't have a concept of -0 (floating point keeps the sign).
				if (!number.IsFloatingPoint) number.IsNegative = false;
				number.Scale = 0; // Decimals with scale ('0.00') should be rounded.
			}
			dig.resize(static_cast<size_t>(i));
		}

		void AppendUInt(std::string& sb, unsigned value, int minDigits)
		{
			char buf[16]; int n = 0;
			do { buf[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value != 0);
			for (int k = n; k < minDigits; k++) sb.push_back('0');
			while (n > 0) sb.push_back(buf[--n]);
		}

		void FormatExponent(std::string& sb, int value, char expChar, int minDigits, bool positiveSign)
		{
			sb.push_back(expChar);
			if (value < 0) { sb.push_back('-'); value = -value; }
			else if (positiveSign) sb.push_back('+');
			AppendUInt(sb, static_cast<unsigned>(value), minDigits);
		}

		// FormatFixed; groups == true uses the invariant NumberGroupSizes {3} and ",".
		void FormatFixed(std::string& sb, const Number& number, int nMaxDigits, bool groups)
		{
			int digPos = number.Scale;
			int d = 0;
			if (digPos > 0)
			{
				std::string intPart;
				int digLength = static_cast<int>(number.Digits.size());
				int digStart = digPos < digLength ? digPos : digLength;
				for (int i = 0; i < digPos; i++) intPart.push_back(i < digStart ? number.Digits[static_cast<size_t>(i)] : '0');
				if (groups)
				{
					std::string g;
					int len = static_cast<int>(intPart.size());
					for (int i = 0; i < len; i++)
					{
						g.push_back(intPart[static_cast<size_t>(i)]);
						int rest = len - 1 - i;
						if (rest > 0 && rest % 3 == 0) g.push_back(',');
					}
					sb += g;
				}
				else sb += intPart;
				d = digStart;
				digPos = 0;
			}
			else
			{
				sb.push_back('0');
			}
			if (nMaxDigits > 0)
			{
				sb.push_back('.');
				if (digPos < 0 && nMaxDigits > 0)
				{
					int zeroes = std::min(-digPos, nMaxDigits);
					sb.append(static_cast<size_t>(zeroes), '0');
					digPos += zeroes;
					nMaxDigits -= zeroes;
				}
				while (nMaxDigits > 0)
				{
					char c = number.Dig(d);
					if (c != '\0') { sb.push_back(c); d++; } else sb.push_back('0');
					nMaxDigits--;
				}
			}
		}

		void FormatScientific(std::string& sb, const Number& number, int nMaxDigits, char expChar)
		{
			int d = 0;
			char c = number.Dig(d);
			sb.push_back(c != '\0' ? (d++, c) : '0');
			if (nMaxDigits != 1) sb.push_back('.'); // For E0 we would like to suppress the decimal point
			while (--nMaxDigits > 0)
			{
				c = number.Dig(d);
				sb.push_back(c != '\0' ? (d++, c) : '0');
			}
			int e = number.Dig(0) == '\0' ? 0 : number.Scale - 1;
			FormatExponent(sb, e, expChar, 3, true);
		}

		void FormatGeneral(std::string& sb, const Number& number, int nMaxDigits, char expChar, bool suppressScientific)
		{
			int digPos = number.Scale;
			bool scientific = false;
			if (!suppressScientific)
			{
				// Don't switch to scientific notation
				if (digPos > nMaxDigits || digPos < -3) { digPos = 1; scientific = true; }
			}
			int d = 0;
			if (digPos > 0)
			{
				do
				{
					char c = number.Dig(d);
					sb.push_back(c != '\0' ? (d++, c) : '0');
				} while (--digPos > 0);
			}
			else sb.push_back('0');
			if (number.Dig(d) != '\0' || digPos < 0)
			{
				sb.push_back('.');
				while (digPos < 0) { sb.push_back('0'); digPos++; }
				while (number.Dig(d) != '\0') sb.push_back(number.Digits[static_cast<size_t>(d++)]);
			}
			if (scientific) FormatExponent(sb, number.Scale - 1, expChar, 2, true);
		}

		// Number.NumberToString for the standard formats (invariant culture).
		bool NumberToString(std::string& sb, Number& number, char format, int nMaxDigits)
		{
			bool isCorrectlyRounded = number.IsFloatingPoint;
			switch (format)
			{
			case 'C': case 'c':
			{
				if (nMaxDigits < 0) nMaxDigits = 2;
				RoundNumber(number, number.Scale + nMaxDigits, isCorrectlyRounded);
				// Invariant: CurrencyPositivePattern 0 "$n", CurrencyNegativePattern 0 "($n)", symbol U+00A4.
				if (number.IsNegative) sb.push_back('(');
				sb += "\xC2\xA4";
				FormatFixed(sb, number, nMaxDigits, true);
				if (number.IsNegative) sb.push_back(')');
				return true;
			}
			case 'F': case 'f':
			{
				if (nMaxDigits < 0) nMaxDigits = 2;
				RoundNumber(number, number.Scale + nMaxDigits, isCorrectlyRounded);
				if (number.IsNegative) sb.push_back('-');
				FormatFixed(sb, number, nMaxDigits, false);
				return true;
			}
			case 'N': case 'n':
			{
				if (nMaxDigits < 0) nMaxDigits = 2;
				RoundNumber(number, number.Scale + nMaxDigits, isCorrectlyRounded);
				if (number.IsNegative) sb.push_back('-'); // NumberNegativePattern 1: "-n"
				FormatFixed(sb, number, nMaxDigits, true);
				return true;
			}
			case 'E': case 'e':
			{
				if (nMaxDigits < 0) nMaxDigits = 6;
				nMaxDigits++;
				RoundNumber(number, nMaxDigits, isCorrectlyRounded);
				if (number.IsNegative) sb.push_back('-');
				FormatScientific(sb, number, nMaxDigits, format);
				return true;
			}
			case 'G': case 'g':
			{
				if (nMaxDigits < 1) nMaxDigits = static_cast<int>(number.Digits.size());
				RoundNumber(number, nMaxDigits, isCorrectlyRounded);
				if (number.IsNegative) sb.push_back('-');
				FormatGeneral(sb, number, nMaxDigits, static_cast<char>(format - ('G' - 'E')), false);
				return true;
			}
			case 'P': case 'p':
			{
				if (nMaxDigits < 0) nMaxDigits = 2;
				number.Scale += 2;
				RoundNumber(number, number.Scale + nMaxDigits, isCorrectlyRounded);
				// Invariant: PercentPositivePattern 0 "n %", PercentNegativePattern 0 "-n %".
				if (number.IsNegative) sb.push_back('-');
				FormatFixed(sb, number, nMaxDigits, true);
				sb += " %";
				return true;
			}
			default:
				return false;
			}
		}

		int FindSection(std::string_view format, int section)
		{
			if (section == 0) return 0;
			size_t src = 0;
			while (true)
			{
				if (src >= format.size()) return 0;
				char ch = format[src++];
				switch (ch)
				{
				case '\'': case '"':
					while (src < format.size() && format[src] != 0 && format[src++] != ch) {}
					break;
				case '\\':
					if (src < format.size() && format[src] != 0) src++;
					break;
				case ';':
					if (--section != 0) break;
					if (src < format.size() && format[src] != 0 && format[src] != ';') return static_cast<int>(src);
					return 0;
				case '\0':
					return 0;
				default:
					break;
				}
			}
		}

		// Number.NumberToStringFormat: the custom numeric format strings ("0.##", "#,0", "+#,0;-#,0;0", "0%", ...).
		void NumberToStringFormat(std::string& sb, Number& number, std::string_view format)
		{
			int digitCount, decimalPos, firstDigit, lastDigit, digPos;
			bool scientific;
			int thousandPos, thousandCount = 0;
			bool thousandSeps;
			int scaleAdjust, adjust;
			int section;
			size_t src;
			char ch;

			section = FindSection(format, number.Dig(0) == '\0' ? 2 : number.IsNegative ? 1 : 0);

			while (true)
			{
				digitCount = 0; decimalPos = -1; firstDigit = 0x7FFFFFFF; lastDigit = 0; scientific = false;
				thousandPos = -1; thousandSeps = false; scaleAdjust = 0;
				src = static_cast<size_t>(section);

				while (src < format.size() && (ch = format[src++]) != 0 && ch != ';')
				{
					switch (ch)
					{
					case '#': digitCount++; break;
					case '0':
						if (firstDigit == 0x7FFFFFFF) firstDigit = digitCount;
						digitCount++;
						lastDigit = digitCount;
						break;
					case '.':
						if (decimalPos < 0) decimalPos = digitCount;
						break;
					case ',':
						if (digitCount > 0 && decimalPos < 0)
						{
							if (thousandPos >= 0)
							{
								if (thousandPos == digitCount) { thousandCount++; break; }
								thousandSeps = true;
							}
							thousandPos = digitCount;
							thousandCount = 1;
						}
						break;
					case '%': scaleAdjust += 2; break;
					case '\'': case '"':
						while (src < format.size() && format[src] != 0 && format[src++] != ch) {}
						break;
					case '\\':
						if (src < format.size() && format[src] != 0) src++;
						break;
					case 'E': case 'e':
						if ((src < format.size() && format[src] == '0') ||
							(src + 1 < format.size() && (format[src] == '+' || format[src] == '-') && format[src + 1] == '0'))
						{
							while (++src < format.size() && format[src] == '0') {}
							scientific = true;
						}
						break;
					default:
						// UTF-8 per-mille sign U+2030 (E2 80 B0) scales by 1000.
						if (static_cast<unsigned char>(ch) == 0xE2 && src + 1 < format.size() && static_cast<unsigned char>(format[src]) == 0x80 && static_cast<unsigned char>(format[src + 1]) == 0xB0)
						{
							scaleAdjust += 3;
							src += 2;
						}
						break;
					}
				}

				if (decimalPos < 0) decimalPos = digitCount;

				if (thousandPos >= 0)
				{
					if (thousandPos == decimalPos) scaleAdjust -= thousandCount * 3;
					else thousandSeps = true;
				}

				if (number.Dig(0) != '\0')
				{
					number.Scale += scaleAdjust;
					int pos = scientific ? digitCount : number.Scale + digitCount - decimalPos;
					RoundNumber(number, pos, false);
					if (number.Dig(0) == '\0')
					{
						int s2 = FindSection(format, 2);
						if (s2 != section) { section = s2; continue; }
					}
				}
				else
				{
					if (!number.IsFloatingPoint) number.IsNegative = false; // integers have no -0
					number.Scale = 0; // Decimals with scale ('0.00') should be rounded.
				}
				break;
			}

			firstDigit = firstDigit < decimalPos ? decimalPos - firstDigit : 0;
			lastDigit = lastDigit > decimalPos ? decimalPos - lastDigit : 0;
			if (scientific) { digPos = decimalPos; adjust = 0; }
			else
			{
				digPos = number.Scale > decimalPos ? number.Scale : decimalPos;
				adjust = number.Scale - decimalPos;
			}
			src = static_cast<size_t>(section);

			std::vector<int> thousandsSepPos;
			int thousandsSepCtr = -1;
			if (thousandSeps)
			{
				// NumberGroupSizes {3}: precompute where separators go, since formatting runs forwards.
				int groupTotalSizeCount = 3;
				int totalDigits = digPos + ((adjust < 0) ? adjust : 0);
				int numDigits = (firstDigit > totalDigits) ? firstDigit : totalDigits;
				while (numDigits > groupTotalSizeCount)
				{
					++thousandsSepCtr;
					thousandsSepPos.push_back(groupTotalSizeCount);
					groupTotalSizeCount += 3;
				}
			}

			if (number.IsNegative && section == 0 && number.Scale != 0) sb.push_back('-');

			bool decimalWritten = false;
			int cur = 0;
			auto curDig = [&]() { return number.Dig(cur); };
			auto groupSep = [&]()
			{
				if (thousandSeps && digPos > 1 && thousandsSepCtr >= 0)
				{
					if (digPos == thousandsSepPos[static_cast<size_t>(thousandsSepCtr)] + 1)
					{
						sb.push_back(',');
						thousandsSepCtr--;
					}
				}
			};
			while (src < format.size() && (ch = format[src++]) != 0 && ch != ';')
			{
				if (adjust > 0)
				{
					switch (ch)
					{
					case '#': case '0': case '.':
						while (adjust > 0)
						{
							// digPos will be one greater than thousandsSepPos[thousandsSepCtr] since we are at
							// the character after which the groupSeparator needs to be appended.
							char c = curDig();
							sb.push_back(c != '\0' ? (cur++, c) : '0');
							groupSep();
							digPos--;
							adjust--;
						}
						break;
					default:
						break;
					}
				}

				switch (ch)
				{
				case '#': case '0':
				{
					char c;
					if (adjust < 0)
					{
						adjust++;
						c = digPos <= firstDigit ? '0' : '\0';
					}
					else
					{
						char dc = curDig();
						c = dc != '\0' ? (cur++, dc) : digPos > lastDigit ? '0' : '\0';
					}
					if (c != '\0')
					{
						sb.push_back(c);
						groupSep();
					}
					digPos--;
					break;
				}
				case '.':
				{
					if (digPos != 0 || decimalWritten) break; // For compatibility, don't echo repeated decimals
					// If the format has trailing zeros or the format has a decimal and digits remain
					if (lastDigit < 0 || (decimalPos < digitCount && curDig() != '\0'))
					{
						sb.push_back('.');
						decimalWritten = true;
					}
					break;
				}
				case '%': sb.push_back('%'); break;
				case ',': break;
				case '\'': case '"':
					while (src < format.size() && format[src] != 0 && format[src] != ch) sb.push_back(format[src++]);
					if (src < format.size() && format[src] != 0) src++;
					break;
				case '\\':
					if (src < format.size() && format[src] != 0) sb.push_back(format[src++]);
					break;
				case 'E': case 'e':
				{
					bool positiveSign = false;
					int i = 0;
					if (scientific)
					{
						if (src < format.size() && format[src] == '0') i++;
						else if (src + 1 < format.size() && format[src] == '+' && format[src + 1] == '0') positiveSign = true;
						else if (src + 1 < format.size() && format[src] == '-' && format[src + 1] == '0') {}
						else { sb.push_back(ch); break; }

						while (++src < format.size() && format[src] == '0') i++;
						if (i > 10) i = 10;

						int exp = number.Dig(0) == '\0' ? 0 : number.Scale - decimalPos;
						FormatExponent(sb, exp, ch, i, positiveSign);
						scientific = false;
					}
					else
					{
						sb.push_back(ch);
						if (src < format.size())
						{
							if (format[src] == '+' || format[src] == '-') sb.push_back(format[src++]);
							while (src < format.size() && format[src] == '0') sb.push_back(format[src++]);
						}
					}
					break;
				}
				default:
					if (static_cast<unsigned char>(ch) == 0xE2 && src + 1 < format.size() && static_cast<unsigned char>(format[src]) == 0x80 && static_cast<unsigned char>(format[src + 1]) == 0xB0)
					{
						sb += "\xE2\x80\xB0";
						src += 2;
					}
					else sb.push_back(ch);
					break;
				}
			}

			if (number.IsNegative && section == 0 && number.Scale == 0 && !sb.empty()) sb.insert(sb.begin(), '-');
		}

		// Number.ParseFormatSpecifier: a letter plus optional digits is a standard format; anything else is custom ('\0').
		char ParseFormatSpecifier(std::string_view format, int& precision)
		{
			char c = '\0';
			if (!format.empty())
			{
				c = format[0];
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
				{
					size_t i = 1;
					int p = 0;
					bool any = false;
					while (i < format.size() && format[i] >= '0' && format[i] <= '9')
					{
						if (p >= 100000000) throw FormatException("Format specifier was invalid.");
						p = p * 10 + (format[i++] - '0');
						any = true;
					}
					if (i == format.size() || format[i] == '\0')
					{
						precision = any ? p : -1;
						return c;
					}
				}
			}
			precision = -1;
			return (format.empty() || c == '\0') ? 'G' : '\0';
		}

		// Number.FormatDouble / FormatSingle.
		String FormatFloating(double value, std::string_view format, bool isFloat)
		{
			if (std::isnan(value)) return "NaN";
			if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";

			int precision;
			char fmt = ParseFormatSpecifier(format, precision);
			bool isSignificantDigits = true;
			if (fmt == '\0') precision = isFloat ? SinglePrecisionCustomFormat : DoublePrecisionCustomFormat;

			switch (fmt)
			{
			case 'R': case 'r':
				fmt = static_cast<char>(fmt - ('R' - 'G'));
				precision = -1;
				break;
			case 'G': case 'g':
				if (precision == 0) precision = -1;
				break;
			case 'E': case 'e':
				if (precision == -1) precision = 6;
				precision++;
				break;
			case 'F': case 'f': case 'N': case 'n': case 'C': case 'c':
				if (precision == -1) precision = 2;
				isSignificantDigits = false;
				break;
			case 'P': case 'p':
				if (precision == -1) precision = 2;
				precision += 2;
				isSignificantDigits = false;
				break;
			case '\0':
				break;
			default:
				throw FormatException("Format specifier was invalid.");
			}
			int nMaxDigits = precision;

			Number number;
			number.IsFloatingPoint = true;
			number.IsNegative = std::signbit(value);
			double a = std::fabs(value);
			if (a != 0.0)
			{
				if (precision == -1) ShortestDigits(a, isFloat, number);
				else
				{
					ExactDigits(a, number);
					RoundExact(number, isSignificantDigits ? precision : number.Scale + precision);
				}
			}

			std::string sb;
			if (fmt != '\0')
			{
				if (precision == -1)
				{
					// Shortest round-trip: G switches to E once the exponent passes the round-trip digit count
					// (17 for double, 9 for float): 1e16 prints "10000000000000000", 1e17 "1E+17".
					nMaxDigits = std::max(static_cast<int>(number.Digits.size()), isFloat ? SingleMaxRoundTripDigits : DoubleMaxRoundTripDigits);
				}
				else if (fmt == 'E' || fmt == 'e') nMaxDigits = precision - 1;
				else if (fmt == 'P' || fmt == 'p') nMaxDigits = precision - 2;
				if (!NumberToString(sb, number, fmt, nMaxDigits)) throw FormatException("Format specifier was invalid.");
			}
			else
			{
				NumberToStringFormat(sb, number, format);
			}
			return sb;
		}

		// Number.FormatInt32/Int64/UInt64.
		String FormatInteger(unsigned long long magnitude, bool negative, std::string_view format, int bits, bool isSigned)
		{
			int precision;
			char fmt = ParseFormatSpecifier(format, precision);
			std::string sb;
			char upper = static_cast<char>(fmt & ~0x20);
			if (fmt != '\0' && (upper == 'G' && precision < 1))
			{
				if (negative) sb.push_back('-');
				sb += std::to_string(magnitude);
				return sb;
			}
			if (upper == 'D')
			{
				if (negative) sb.push_back('-');
				std::string d = std::to_string(magnitude);
				for (int k = static_cast<int>(d.size()); k < precision; k++) sb.push_back('0');
				return sb + d;
			}
			if (upper == 'X')
			{
				// Hex prints the two's complement bits of the value's own width.
				unsigned long long v = negative ? (0ull - magnitude) : magnitude;
				if (bits == 32) v &= 0xFFFFFFFFull;
				const char* hex = fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
				std::string d;
				do { d.insert(d.begin(), hex[v & 0xF]); v >>= 4; } while (v != 0);
				for (int k = static_cast<int>(d.size()); k < precision; k++) sb.push_back('0');
				return sb + d;
			}
			(void)isSigned;
			Number number;
			number.IsNegative = negative;
			if (magnitude != 0)
			{
				number.Digits = std::to_string(magnitude);
				number.Scale = static_cast<int>(number.Digits.size());
				while (!number.Digits.empty() && number.Digits.back() == '0') number.Digits.pop_back();
			}
			if (fmt != '\0')
			{
				if (!NumberToString(sb, number, fmt, precision)) throw FormatException("Format specifier was invalid.");
			}
			else
			{
				NumberToStringFormat(sb, number, format);
			}
			return sb;
		}
	}

	String Str(double v) { return FormatFloating(v, std::string_view(), false); }
	String Str(float v) { return FormatFloating(static_cast<double>(v), std::string_view(), true); }
	String Fmt(double v, std::string_view format) { return FormatFloating(v, format, false); }
	String Fmt(float v, std::string_view format) { return FormatFloating(static_cast<double>(v), format, true); }
	String Fmt(int v, std::string_view format)
	{
		long long w = v;
		return FormatInteger(static_cast<unsigned long long>(w < 0 ? -w : w), w < 0, format, 32, true);
	}
	String Fmt(long long v, std::string_view format)
	{
		unsigned long long m = v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
		return FormatInteger(m, v < 0, format, 64, true);
	}
	String Fmt(unsigned long long v, std::string_view format) { return FormatInteger(v, false, format, 64, false); }

	namespace FormatDetail
	{
		String Composite(std::string_view f, const Func<String, int, std::string_view>& arg, int count)
		{
			String r;
			size_t i = 0;
			while (i < f.size())
			{
				char c = f[i];
				if (c == '{')
				{
					if (i + 1 < f.size() && f[i + 1] == '{') { r.push_back('{'); i += 2; continue; }
					size_t close = f.find('}', i);
					if (close == std::string_view::npos) throw FormatException("Input string was not in a correct format.");
					std::string_view item = f.substr(i + 1, close - i - 1);
					std::string_view spec;
					size_t colon = item.find(':');
					if (colon != std::string_view::npos) { spec = item.substr(colon + 1); item = item.substr(0, colon); }
					int align = 0;
					size_t comma = item.find(',');
					if (comma != std::string_view::npos)
					{
						std::string_view a = item.substr(comma + 1);
						while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
						while (!a.empty() && a.back() == ' ') a.remove_suffix(1);
						std::from_chars(a.data(), a.data() + a.size(), align);
						item = item.substr(0, comma);
					}
					while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
					int index = -1;
					auto res = std::from_chars(item.data(), item.data() + item.size(), index);
					if (res.ec != std::errc() || index < 0 || index >= count) throw FormatException("Index (zero based) must be greater than or equal to zero and less than the size of the argument list.");
					String s = arg(index, spec);
					int pad = std::abs(align) - static_cast<int>(s.size());
					if (pad > 0 && align > 0) r.append(static_cast<size_t>(pad), ' ');
					r += s;
					if (pad > 0 && align < 0) r.append(static_cast<size_t>(pad), ' ');
					i = close + 1;
				}
				else if (c == '}')
				{
					if (i + 1 < f.size() && f[i + 1] == '}') { r.push_back('}'); i += 2; continue; }
					throw FormatException("Input string was not in a correct format.");
				}
				else { r.push_back(c); i++; }
			}
			return r;
		}
	}

	// ============================================================ Parse =========================================
	namespace
	{
		bool IsWhite(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

		std::string_view TrimWhite(std::string_view s)
		{
			while (!s.empty() && IsWhite(s.front())) s.remove_prefix(1);
			while (!s.empty() && IsWhite(s.back())) s.remove_suffix(1);
			return s;
		}

		bool EqualsIgnoreCase(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size()) return false;
			for (size_t i = 0; i < a.size(); i++)
			{
				char x = a[i], y = b[i];
				if (x >= 'A' && x <= 'Z') x = static_cast<char>(x + 32);
				if (y >= 'A' && y <= 'Z') y = static_cast<char>(y + 32);
				if (x != y) return false;
			}
			return true;
		}
	}

	namespace Double
	{
		// double.TryParse(s, NumberStyles.Float | NumberStyles.AllowThousands, CultureInfo.InvariantCulture):
		// surrounding white space, a leading sign, ',' group separators in the integer part, '.', an exponent;
		// "NaN", "Infinity", "-Infinity" (case-insensitive; the invariant culture rejects "∞"). Out-of-range values become ±Infinity / ±0
		// (.NET Core 3.0+), and the result is correctly rounded (IEEE), as from_chars is.
		bool TryParse(std::string_view s, double& out)
		{
			out = 0;
			s = TrimWhite(s);
			if (s.empty()) return false;
			bool neg = false;
			std::string_view body = s;
			if (body.front() == '+' || body.front() == '-') { neg = body.front() == '-'; body.remove_prefix(1); }
			if (EqualsIgnoreCase(s, "NaN") || EqualsIgnoreCase(body, "NaN")) { out = NaN; return true; }
			if (EqualsIgnoreCase(body, "Infinity")) { out = neg ? NegativeInfinity : PositiveInfinity; return true; }

			std::string clean;
			clean.reserve(body.size());
			size_t i = 0;
			bool digits = false;
			while (i < body.size() && ((body[i] >= '0' && body[i] <= '9') || (body[i] == ',' && digits)))
			{
				if (body[i] != ',') { clean.push_back(body[i]); digits = true; }
				i++;
			}
			if (i < body.size() && body[i] == '.')
			{
				clean.push_back('.');
				i++;
				while (i < body.size() && body[i] >= '0' && body[i] <= '9') { clean.push_back(body[i++]); digits = true; }
			}
			if (!digits) return false;
			if (i < body.size() && (body[i] == 'e' || body[i] == 'E'))
			{
				size_t j = i + 1;
				std::string e = "e";
				if (j < body.size() && (body[j] == '+' || body[j] == '-')) e.push_back(body[j++]);
				size_t startDigits = j;
				while (j < body.size() && body[j] >= '0' && body[j] <= '9') e.push_back(body[j++]);
				if (j == startDigits) return false;
				clean += e;
				i = j;
			}
			if (i != body.size()) return false;

			double v = 0;
			auto r = std::from_chars(clean.data(), clean.data() + clean.size(), v, std::chars_format::general);
			if (r.ec == std::errc::result_out_of_range)
			{
				// Overflow gives infinity, underflow zero: decide by the decimal exponent of the first significant digit.
				long long exp10 = 0;
				size_t ePos = clean.find('e');
				std::string_view mant = std::string_view(clean).substr(0, ePos);
				if (ePos != std::string::npos)
				{
					std::string_view es = std::string_view(clean).substr(ePos + 1);
					bool eneg = !es.empty() && es.front() == '-';
					if (!es.empty() && (es.front() == '+' || es.front() == '-')) es.remove_prefix(1);
					for (char c : es) { exp10 = exp10 * 10 + (c - '0'); if (exp10 > 100000) break; }
					if (eneg) exp10 = -exp10;
				}
				size_t dot = mant.find('.');
				long long intDigits = static_cast<long long>(dot == std::string_view::npos ? mant.size() : dot);
				long long firstSig = 0; bool found = false;
				for (size_t k = 0; k < mant.size(); k++)
				{
					if (mant[k] == '.') continue;
					if (mant[k] != '0') { found = true; break; }
					firstSig++;
				}
				if (!found) v = 0;
				else
				{
					long long magnitude = exp10 + intDigits - firstSig;
					v = magnitude > 0 ? PositiveInfinity : 0.0;
				}
			}
			else if (r.ec != std::errc()) return false;
			out = neg ? -v : v;
			return true;
		}

		double Parse(std::string_view s)
		{
			double v;
			if (!TryParse(s, v)) throw FormatException(Cat("The input string '", String(s), "' was not in a correct format."));
			return v;
		}
	}

	namespace Int
	{
		// int.TryParse(s, NumberStyles.Integer, Invariant): white space, a leading sign, decimal digits.
		bool TryParse(std::string_view s, int& out)
		{
			out = 0;
			s = TrimWhite(s);
			if (s.empty()) return false;
			bool neg = false;
			if (s.front() == '+' || s.front() == '-') { neg = s.front() == '-'; s.remove_prefix(1); }
			if (s.empty()) return false;
			long long v = 0;
			for (char c : s)
			{
				if (c < '0' || c > '9') return false;
				v = v * 10 + (c - '0');
				if (v > 2147483648LL) return false;
			}
			if (neg) v = -v;
			if (v > MaxValue || v < MinValue) return false;
			out = static_cast<int>(v);
			return true;
		}

		int Parse(std::string_view s)
		{
			int v;
			if (TryParse(s, v)) return v;
			// Distinguish overflow from bad format, as .NET does.
			std::string_view t = TrimWhite(s);
			if (!t.empty() && (t.front() == '+' || t.front() == '-')) t.remove_prefix(1);
			bool allDigits = !t.empty();
			for (char c : t) if (c < '0' || c > '9') allDigits = false;
			if (allDigits) throw OverflowException("Value was either too large or too small for an Int32.");
			throw FormatException(Cat("The input string '", String(s), "' was not in a correct format."));
		}
	}

	// ============================================================ Strings =======================================
	namespace Strings
	{
		bool IsNullOrWhiteSpace(const String& s)
		{
			for (char c : s) if (!IsWhite(c)) return false;
			return true;
		}
		String ToLowerInvariant(const String& s)
		{
			String r = s;
			for (char& c : r) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
			return r;
		}
		String ToUpperInvariant(const String& s)
		{
			String r = s;
			for (char& c : r) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
			return r;
		}
		String Trim(const String& s) { return String(TrimWhite(s)); }
		String TrimStart(const String& s)
		{
			size_t i = 0;
			while (i < s.size() && IsWhite(s[i])) i++;
			return s.substr(i);
		}
		String TrimEnd(const String& s)
		{
			size_t n = s.size();
			while (n > 0 && IsWhite(s[n - 1])) n--;
			return s.substr(0, n);
		}
		String Substring(const String& s, int start)
		{
			if (start < 0 || start > static_cast<int>(s.size())) throw ArgumentOutOfRangeException("startIndex cannot be larger than length of string.");
			return s.substr(static_cast<size_t>(start));
		}
		String Substring(const String& s, int start, int length)
		{
			if (start < 0 || length < 0 || start > static_cast<int>(s.size()) || length > static_cast<int>(s.size()) - start)
				throw ArgumentOutOfRangeException("Index and length must refer to a location within the string.");
			return s.substr(static_cast<size_t>(start), static_cast<size_t>(length));
		}
		bool StartsWith(const String& s, std::string_view prefix) { return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0; }
		bool EndsWith(const String& s, std::string_view suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }
		bool Contains(const String& s, std::string_view part) { return s.find(part) != String::npos; }
		int IndexOf(const String& s, std::string_view part, int start)
		{
			if (start < 0 || start > static_cast<int>(s.size())) throw ArgumentOutOfRangeException("startIndex");
			size_t p = s.find(part, static_cast<size_t>(start));
			return p == String::npos ? -1 : static_cast<int>(p);
		}
		int IndexOf(const String& s, char c, int start)
		{
			if (start < 0 || start > static_cast<int>(s.size())) throw ArgumentOutOfRangeException("startIndex");
			size_t p = s.find(c, static_cast<size_t>(start));
			return p == String::npos ? -1 : static_cast<int>(p);
		}
		int LastIndexOf(const String& s, char c)
		{
			size_t p = s.rfind(c);
			return p == String::npos ? -1 : static_cast<int>(p);
		}
		String Replace(const String& s, std::string_view from, std::string_view to)
		{
			if (from.empty()) throw ArgumentException("String cannot be of zero length.");
			String r;
			size_t i = 0;
			while (true)
			{
				size_t p = s.find(from, i);
				if (p == String::npos) { r.append(s, i, String::npos); break; }
				r.append(s, i, p - i);
				r += to;
				i = p + from.size();
			}
			return r;
		}
		List<String> Split(const String& s, char sep) { return Split(s, std::string_view(&sep, 1)); }
		List<String> Split(const String& s, std::string_view seps)
		{
			List<String> r;
			size_t start = 0;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (seps.find(s[i]) != std::string_view::npos)
				{
					r.Add(s.substr(start, i - start));
					start = i + 1;
				}
			}
			r.Add(s.substr(start));
			return r;
		}
		String PadLeft(const String& s, int width, char pad)
		{
			if (width < 0) throw ArgumentOutOfRangeException("totalWidth");
			return static_cast<int>(s.size()) >= width ? s : String(static_cast<size_t>(width) - s.size(), pad) + s;
		}
		String PadRight(const String& s, int width, char pad)
		{
			if (width < 0) throw ArgumentOutOfRangeException("totalWidth");
			return static_cast<int>(s.size()) >= width ? s : s + String(static_cast<size_t>(width) - s.size(), pad);
		}
		int CompareOrdinal(const String& a, const String& b)
		{
			size_t n = std::min(a.size(), b.size());
			for (size_t i = 0; i < n; i++)
			{
				int d = static_cast<int>(static_cast<unsigned char>(a[i])) - static_cast<int>(static_cast<unsigned char>(b[i]));
				if (d != 0) return d;
			}
			return static_cast<int>(a.size()) - static_cast<int>(b.size());
		}
		bool EqualsOrdinalIgnoreCase(const String& a, const String& b) { return EqualsIgnoreCase(a, b); }
	}

	namespace Char
	{
		uint32_t At(const String& s, int utf16Index)
		{
			if (utf16Index < 0) throw IndexOutOfRangeException("index");
			int i = 0;
			uint32_t found = 0;
			bool got = false;
			ForEachUtf16(s, [&](uint32_t c) { if (!got && i++ == utf16Index) { found = c; got = true; } });
			if (!got) throw IndexOutOfRangeException("index");
			return found;
		}

		bool IsLetter(uint32_t c)
		{
			if (c < 0x80) return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
			if (c == 0xAA || c == 0xB5 || c == 0xBA) return true;
			if (c >= 0xC0 && c <= 0x2C1) return c != 0xD7 && c != 0xF7;
			if (c >= 0x370 && c <= 0x373) return true;
			if (c == 0x376 || c == 0x377 || (c >= 0x37B && c <= 0x37D) || c == 0x37F || c == 0x386 || (c >= 0x388 && c <= 0x38A) || c == 0x38C) return true;
			if (c >= 0x38E && c <= 0x3A1) return true;
			if (c >= 0x3A3 && c <= 0x3F5) return true;
			if (c >= 0x3F7 && c <= 0x481) return true;
			if (c >= 0x48A && c <= 0x52F) return true;
			return false;
		}

		namespace
		{
			// A run of case pairs [first, last] where the upper case of each pair sits on the even (evenUpper) or odd code point.
			bool InPairs(uint32_t c, uint32_t first, uint32_t last) { return c >= first && c <= last; }
			uint32_t PairUpper(uint32_t c, bool evenUpper) { bool even = (c & 1) == 0; return even == evenUpper ? c : c - 1; }
			uint32_t PairLower(uint32_t c, bool evenUpper) { bool even = (c & 1) == 0; return even == evenUpper ? c + 1 : c; }

			// The pair ranges of Latin Extended-A/B and Cyrillic: (first, last, upper on the even code point).
			struct PairRun { uint32_t First, Last; bool EvenUpper; };
			const PairRun Runs[] = {
				{ 0x100, 0x12F, true }, { 0x132, 0x137, true }, { 0x139, 0x148, false }, { 0x14A, 0x177, true }, { 0x179, 0x17E, false },
				{ 0x1CD, 0x1DC, false }, { 0x1DE, 0x1EF, true }, { 0x1F8, 0x21F, true }, { 0x222, 0x233, true }, { 0x246, 0x24F, true },
				{ 0x460, 0x481, true }, { 0x48A, 0x4BF, true }, { 0x4C1, 0x4CE, false }, { 0x4D0, 0x52F, true },
			};
		}

		uint32_t ToUpperInvariant(uint32_t c)
		{
			if (c < 0x80) return c >= 'a' && c <= 'z' ? c - 32 : c;
			if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
			if (c == 0xFF) return 0x178;
			if (c == 0xB5) return 0x39C;
			if (c == 0x131) return 'I';
			if (c == 0x17F) return 'S';
			for (const auto& r : Runs) if (InPairs(c, r.First, r.Last)) return PairUpper(c, r.EvenUpper);
			if ((c >= 0x3B1 && c <= 0x3C1) || (c >= 0x3C3 && c <= 0x3CB)) return c - 0x20;
			if (c == 0x3C2) return 0x3A3;
			if (c == 0x3AC) return 0x386;
			if (c >= 0x3AD && c <= 0x3AF) return c - 0x25;
			if (c == 0x3CC) return 0x38C;
			if (c == 0x3CD || c == 0x3CE) return c - 0x3F;
			if (c >= 0x430 && c <= 0x44F) return c - 0x20;
			if (c >= 0x450 && c <= 0x45F) return c - 0x50;
			if (c == 0x4CF) return 0x4C0;
			return c;
		}

		uint32_t ToLowerInvariant(uint32_t c)
		{
			if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 32 : c;
			if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
			if (c == 0x178) return 0xFF;
			if (c == 0x130) return 'i';
			for (const auto& r : Runs) if (InPairs(c, r.First, r.Last)) return PairLower(c, r.EvenUpper);
			if ((c >= 0x391 && c <= 0x3A1) || (c >= 0x3A3 && c <= 0x3AB)) return c + 0x20;
			if (c == 0x386) return 0x3AC;
			if (c >= 0x388 && c <= 0x38A) return c + 0x25;
			if (c == 0x38C) return 0x3CC;
			if (c == 0x38E || c == 0x38F) return c + 0x3F;
			if (c >= 0x410 && c <= 0x42F) return c + 0x20;
			if (c >= 0x400 && c <= 0x40F) return c + 0x50;
			if (c == 0x4C0) return 0x4CF;
			return c;
		}

		void Append(String& s, uint32_t c)
		{
			// A lone surrogate unit cannot be UTF-8; .NET's UTF-8 encoder writes U+FFFD for it.
			if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
			if (c < 0x80) s += static_cast<char>(c);
			else if (c < 0x800) { s += static_cast<char>(0xC0 | (c >> 6)); s += static_cast<char>(0x80 | (c & 0x3F)); }
			else { s += static_cast<char>(0xE0 | (c >> 12)); s += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); s += static_cast<char>(0x80 | (c & 0x3F)); }
		}
	}
}

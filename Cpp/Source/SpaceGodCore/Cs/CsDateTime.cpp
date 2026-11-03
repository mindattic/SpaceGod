// System.DateTime / System.TimeSpan: ports of dotnet/runtime DateTime.cs, TimeSpan.cs and DateTimeFormat.cs
// (invariant culture). Tests/CsParity checks them against .NET.

#include "Cs/Cs.h"

#include <chrono>
#include <ctime>

namespace cs
{
	const char* ToString(DateTimeKind v)
	{
		switch (v)
		{
		case DateTimeKind::Unspecified: return "Unspecified";
		case DateTimeKind::Utc: return "Utc";
		case DateTimeKind::Local: return "Local";
		}
		return "?";
	}

	const char* ToString(DayOfWeek v)
	{
		static const char* names[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
		int i = static_cast<int>(v);
		return i >= 0 && i < 7 ? names[i] : "?";
	}

	namespace
	{
		constexpr long long TicksPerMillisecond = TimeSpan::TicksPerMillisecond;
		constexpr long long TicksPerSecond = TimeSpan::TicksPerSecond;
		constexpr long long TicksPerMinute = TimeSpan::TicksPerMinute;
		constexpr long long TicksPerHour = TimeSpan::TicksPerHour;
		constexpr long long TicksPerDay = TimeSpan::TicksPerDay;
		constexpr int DaysPerYear = 365;
		constexpr int DaysPer4Years = DaysPerYear * 4 + 1;       // 1461
		constexpr int DaysPer100Years = DaysPer4Years * 25 - 1;  // 36524
		constexpr int DaysPer400Years = DaysPer100Years * 4 + 1; // 146097
		constexpr long long MaxDays = 3652059;                   // DateTime.MaxValue's day count + 1
		constexpr long long MaxHours = MaxDays * 24;
		constexpr long long MaxMinutes = MaxHours * 60;
		constexpr long long MaxSeconds = MaxMinutes * 60;
		constexpr long long MaxMillis = MaxSeconds * 1000;
		constexpr unsigned long long TicksMask = 0x3FFFFFFFFFFFFFFFull;
		constexpr unsigned long long KindUtc = 0x4000000000000000ull;
		constexpr unsigned long long KindLocal = 0x8000000000000000ull;
		constexpr long long TicksCeiling = 0x4000000000000000LL;

		constexpr int DaysToMonth365[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 };
		constexpr int DaysToMonth366[] = { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 };

		long long DateToTicks(int year, int month, int day)
		{
			if (year < 1 || year > 9999 || month < 1 || month > 12 || day < 1)
				throw ArgumentOutOfRangeException("Year, Month, and Day parameters describe an un-representable DateTime.");
			const int* days = DateTime::IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;
			if (day > days[month] - days[month - 1])
				throw ArgumentOutOfRangeException("Year, Month, and Day parameters describe an un-representable DateTime.");
			int y = year - 1;
			long long n = static_cast<long long>(y) * 365 + y / 4 - y / 100 + y / 400 + days[month - 1] + day - 1;
			return n * TicksPerDay;
		}

		long long TimeToTicks(int hour, int minute, int second)
		{
			if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60 || second < 0 || second >= 60)
				throw ArgumentOutOfRangeException("Hour, Minute, and Second parameters describe an un-representable DateTime.");
			return (static_cast<long long>(hour) * 3600 + minute * 60 + second) * TicksPerSecond;
		}

		void GetDate(long long ticks, int& year, int& month, int& day, int* dayOfYear = nullptr)
		{
			int n = static_cast<int>(ticks / TicksPerDay);
			int y400 = n / DaysPer400Years;
			n -= y400 * DaysPer400Years;
			int y100 = n / DaysPer100Years;
			if (y100 == 4) y100 = 3;
			n -= y100 * DaysPer100Years;
			int y4 = n / DaysPer4Years;
			n -= y4 * DaysPer4Years;
			int y1 = n / DaysPerYear;
			if (y1 == 4) y1 = 3;
			year = y400 * 400 + y100 * 100 + y4 * 4 + y1 + 1;
			n -= y1 * DaysPerYear;
			if (dayOfYear) *dayOfYear = n + 1;
			bool leapYear = y1 == 3 && (y4 != 24 || y100 == 3);
			const int* days = leapYear ? DaysToMonth366 : DaysToMonth365;
			int m = (n >> 5) + 1;
			while (n >= days[m]) m++;
			month = m;
			day = n - days[m - 1] + 1;
		}

		// The OS time zone's UTC offset (in ticks) at a UTC instant.
		long long LocalOffsetAtUtc(long long utcTicks)
		{
			long long secs = (utcTicks - DateTime::UnixEpochTicks) / TicksPerSecond;
			// The CRT's local-time functions cover 1970..3000; clamp, as the offset rules are only known there anyway.
			if (secs < 86400) secs = 86400;
			if (secs > 32503593600LL) secs = 32503593600LL;
			std::time_t t = static_cast<std::time_t>(secs);
			std::tm local{};
#ifdef _WIN32
			if (localtime_s(&local, &t) != 0) return 0;
			std::time_t asUtc = _mkgmtime(&local);
#else
			if (!localtime_r(&t, &local)) return 0;
			std::time_t asUtc = timegm(&local);
#endif
			return static_cast<long long>(asUtc - t) * TicksPerSecond;
		}

		long long LocalOffsetAtLocal(long long localTicks)
		{
			long long guess = LocalOffsetAtUtc(localTicks);
			return LocalOffsetAtUtc(localTicks - guess);
		}

		void Pad(String& sb, long long v, int digits)
		{
			String d = std::to_string(v < 0 ? -v : v);
			if (v < 0) sb.push_back('-');
			for (int k = static_cast<int>(d.size()); k < digits; k++) sb.push_back('0');
			sb += d;
		}

		const char* AbbrevDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		const char* Days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
		const char* AbbrevMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		const char* Months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

		int RepeatCount(std::string_view f, size_t pos)
		{
			char c = f[pos];
			size_t i = pos + 1;
			while (i < f.size() && f[i] == c) i++;
			return static_cast<int>(i - pos);
		}

		void FormatOffset(String& sb, long long offsetTicks, int tokenLen, bool colonForm)
		{
			sb.push_back(offsetTicks >= 0 ? '+' : '-');
			if (offsetTicks < 0) offsetTicks = -offsetTicks;
			long long hours = offsetTicks / TicksPerHour;
			long long minutes = (offsetTicks / TicksPerMinute) % 60;
			if (colonForm || tokenLen >= 3) { Pad(sb, hours, 2); sb.push_back(':'); Pad(sb, minutes, 2); }
			else if (tokenLen == 1) sb += std::to_string(hours);
			else Pad(sb, hours, 2);
		}

		// DateTimeFormat.FormatCustomized (invariant culture, Gregorian calendar).
		String FormatCustom(const DateTime& dt, std::string_view f)
		{
			String sb;
			long long ticks = dt.Ticks();
			int year, month, day;
			GetDate(ticks, year, month, day);
			size_t i = 0;
			while (i < f.size())
			{
				char ch = f[i];
				int tokenLen;
				switch (ch)
				{
				case 'g':
					tokenLen = RepeatCount(f, i);
					sb += "A.D.";
					break;
				case 'h':
				{
					tokenLen = RepeatCount(f, i);
					int h = dt.Hour() % 12;
					if (h == 0) h = 12;
					Pad(sb, h, tokenLen <= 1 ? 1 : 2);
					break;
				}
				case 'H':
					tokenLen = RepeatCount(f, i);
					Pad(sb, dt.Hour(), tokenLen <= 1 ? 1 : 2);
					break;
				case 'm':
					tokenLen = RepeatCount(f, i);
					Pad(sb, dt.Minute(), tokenLen <= 1 ? 1 : 2);
					break;
				case 's':
					tokenLen = RepeatCount(f, i);
					Pad(sb, dt.Second(), tokenLen <= 1 ? 1 : 2);
					break;
				case 'f': case 'F':
				{
					tokenLen = RepeatCount(f, i);
					if (tokenLen > 7) throw FormatException("Input string was not in a correct format.");
					long long fraction = ticks % TicksPerSecond;
					for (int k = tokenLen; k < 7; k++) fraction /= 10;
					if (ch == 'f') Pad(sb, fraction, tokenLen);
					else
					{
						int effective = tokenLen;
						while (effective > 0 && fraction % 10 == 0) { fraction /= 10; effective--; }
						if (effective > 0) Pad(sb, fraction, effective);
						else if (!sb.empty() && sb.back() == '.') sb.pop_back(); // a lone '.' before an all-zero F goes too
					}
					break;
				}
				case 't':
					tokenLen = RepeatCount(f, i);
					if (tokenLen == 1) sb.push_back(dt.Hour() < 12 ? 'A' : 'P');
					else sb += dt.Hour() < 12 ? "AM" : "PM";
					break;
				case 'd':
					tokenLen = RepeatCount(f, i);
					if (tokenLen <= 2) Pad(sb, day, tokenLen);
					else sb += (tokenLen == 3 ? AbbrevDays : Days)[static_cast<int>(dt.DayOfWeek())];
					break;
				case 'M':
					tokenLen = RepeatCount(f, i);
					if (tokenLen <= 2) Pad(sb, month, tokenLen);
					else sb += (tokenLen == 3 ? AbbrevMonths : Months)[month - 1];
					break;
				case 'y':
					tokenLen = RepeatCount(f, i);
					if (tokenLen <= 2) Pad(sb, year % 100, tokenLen);
					else Pad(sb, year, tokenLen);
					break;
				case 'z':
				{
					tokenLen = RepeatCount(f, i);
					long long offset = dt.Kind() == DateTimeKind::Utc ? 0 : LocalOffsetAtLocal(ticks);
					FormatOffset(sb, offset, tokenLen, false);
					break;
				}
				case 'K':
					tokenLen = 1;
					if (dt.Kind() == DateTimeKind::Utc) sb.push_back('Z');
					else if (dt.Kind() == DateTimeKind::Local) FormatOffset(sb, LocalOffsetAtLocal(ticks), 3, true);
					break;
				case ':':
					sb.push_back(':');
					tokenLen = 1;
					break;
				case '/':
					sb.push_back('/');
					tokenLen = 1;
					break;
				case '\'': case '"':
				{
					size_t j = i + 1;
					while (j < f.size() && f[j] != ch)
					{
						if (f[j] == '\\' && j + 1 < f.size()) j++;
						sb.push_back(f[j]);
						j++;
					}
					if (j >= f.size()) throw FormatException("Cannot find a matching quote character.");
					tokenLen = static_cast<int>(j - i + 1);
					break;
				}
				case '%':
				{
					// %c: c as a one-character custom format.
					if (i + 1 >= f.size() || f[i + 1] == '%') throw FormatException("Input string was not in a correct format.");
					sb += FormatCustom(dt, f.substr(i + 1, 1));
					tokenLen = 2;
					break;
				}
				case '\\':
					if (i + 1 >= f.size()) throw FormatException("Input string was not in a correct format.");
					sb.push_back(f[i + 1]);
					tokenLen = 2;
					break;
				default:
					sb.push_back(ch);
					tokenLen = 1;
					break;
				}
				i += static_cast<size_t>(tokenLen);
			}
			return sb;
		}
	}

	// ------------------------------------------------------------------ TimeSpan
	const TimeSpan TimeSpan::Zero = TimeSpan(0);
	const TimeSpan TimeSpan::MaxValue = TimeSpan(std::numeric_limits<long long>::max());
	const TimeSpan TimeSpan::MinValue = TimeSpan(std::numeric_limits<long long>::min());

	TimeSpan::TimeSpan(int hours, int minutes, int seconds) : TimeSpan(0, hours, minutes, seconds, 0) {}

	TimeSpan::TimeSpan(int days, int hours, int minutes, int seconds, int milliseconds)
	{
		long long totalMs = (static_cast<long long>(days) * 3600 * 24 + static_cast<long long>(hours) * 3600 + static_cast<long long>(minutes) * 60 + seconds) * 1000 + milliseconds;
		if (totalMs > 922337203685477LL || totalMs < -922337203685477LL) throw ArgumentOutOfRangeException("TimeSpan overflowed because the duration is too long.");
		ticks = totalMs * TicksPerMillisecond;
	}

	double TimeSpan::TotalMilliseconds() const
	{
		double temp = static_cast<double>(ticks) / TicksPerMillisecond;
		constexpr double MaxMs = static_cast<double>(std::numeric_limits<long long>::max()) / TicksPerMillisecond;
		constexpr double MinMs = static_cast<double>(std::numeric_limits<long long>::min()) / TicksPerMillisecond;
		if (temp > MaxMs) return MaxMs;
		if (temp < MinMs) return MinMs;
		return temp;
	}

	namespace
	{
		// TimeSpan.Interval -> IntervalFromDoubleTicks (.NET Core 3.0+: truncates, no millisecond rounding).
		TimeSpan Interval(double value, double scale)
		{
			if (std::isnan(value)) throw ArgumentException("TimeSpan does not accept floating point Not-a-Number values.");
			double t = value * scale;
			if (t > static_cast<double>(std::numeric_limits<long long>::max()) || t < static_cast<double>(std::numeric_limits<long long>::min()))
				throw OverflowException("TimeSpan overflowed because the duration is too long.");
			if (t == static_cast<double>(std::numeric_limits<long long>::max())) return TimeSpan::MaxValue;
			return TimeSpan(static_cast<long long>(t));
		}
	}

	TimeSpan TimeSpan::FromDays(double value) { return Interval(value, static_cast<double>(TicksPerDay)); }
	TimeSpan TimeSpan::FromHours(double value) { return Interval(value, static_cast<double>(TicksPerHour)); }
	TimeSpan TimeSpan::FromMinutes(double value) { return Interval(value, static_cast<double>(TicksPerMinute)); }
	TimeSpan TimeSpan::FromSeconds(double value) { return Interval(value, static_cast<double>(TicksPerSecond)); }
	TimeSpan TimeSpan::FromMilliseconds(double value) { return Interval(value, static_cast<double>(TicksPerMillisecond)); }

	TimeSpan TimeSpan::Duration() const
	{
		if (ticks == std::numeric_limits<long long>::min()) throw OverflowException("The duration cannot be returned for TimeSpan.MinValue because the absolute value of TimeSpan.MinValue exceeds the value of TimeSpan.MaxValue.");
		return TimeSpan(ticks >= 0 ? ticks : -ticks);
	}

	TimeSpan TimeSpan::Negate() const
	{
		if (ticks == std::numeric_limits<long long>::min()) throw OverflowException("Negating the minimum value of a twos complement number is invalid.");
		return TimeSpan(-ticks);
	}

	TimeSpan TimeSpan::Add(TimeSpan t) const
	{
		long long result = static_cast<long long>(static_cast<unsigned long long>(ticks) + static_cast<unsigned long long>(t.ticks));
		// Overflow if both arguments have the same sign and the result has a different sign.
		if ((ticks >> 63 == t.ticks >> 63) && (ticks >> 63 != result >> 63)) throw OverflowException("TimeSpan overflowed because the duration is too long.");
		return TimeSpan(result);
	}

	TimeSpan TimeSpan::Subtract(TimeSpan t) const
	{
		long long result = static_cast<long long>(static_cast<unsigned long long>(ticks) - static_cast<unsigned long long>(t.ticks));
		// Overflow if the arguments have different signs and the sign of the result differs from this.
		if ((ticks >> 63 != t.ticks >> 63) && (ticks >> 63 != result >> 63)) throw OverflowException("TimeSpan overflowed because the duration is too long.");
		return TimeSpan(result);
	}

	String TimeSpan::ToString() const
	{
		String sb;
		long long t = ticks;
		unsigned long long u = t < 0 ? 0ull - static_cast<unsigned long long>(t) : static_cast<unsigned long long>(t);
		if (t < 0) sb.push_back('-');
		unsigned long long days = u / TicksPerDay;
		unsigned long long rest = u % TicksPerDay;
		if (days != 0) { sb += std::to_string(days); sb.push_back('.'); }
		Pad(sb, static_cast<long long>(rest / TicksPerHour), 2); sb.push_back(':');
		Pad(sb, static_cast<long long>((rest / TicksPerMinute) % 60), 2); sb.push_back(':');
		Pad(sb, static_cast<long long>((rest / TicksPerSecond) % 60), 2);
		long long fraction = static_cast<long long>(rest % TicksPerSecond);
		if (fraction != 0) { sb.push_back('.'); Pad(sb, fraction, 7); }
		return sb;
	}

	// ------------------------------------------------------------------ DateTime
	const DateTime DateTime::MinValue = DateTime(DateTime::MinTicks);
	const DateTime DateTime::MaxValue = DateTime(DateTime::MaxTicks);
	const DateTime DateTime::UnixEpoch = DateTime(DateTime::UnixEpochTicks, DateTimeKind::Utc);

	DateTime::DateTime(long long t, DateTimeKind k) : ticks(t), kind(k)
	{
		if (t < MinTicks || t > MaxTicks) throw ArgumentOutOfRangeException("Ticks must be between DateTime.MinValue.Ticks and DateTime.MaxValue.Ticks.");
		if (static_cast<unsigned>(k) > 2u) throw ArgumentException("Invalid DateTimeKind value.");
	}
	DateTime::DateTime(int year, int month, int day, DateTimeKind k) : DateTime(DateToTicks(year, month, day), k) {}
	DateTime::DateTime(int year, int month, int day, int hour, int minute, int second, DateTimeKind k)
		: DateTime(DateToTicks(year, month, day) + TimeToTicks(hour, minute, second), k) {}
	DateTime::DateTime(int year, int month, int day, int hour, int minute, int second, int millisecond, DateTimeKind k)
		: DateTime(DateToTicks(year, month, day) + TimeToTicks(hour, minute, second) + static_cast<long long>(millisecond) * TicksPerMillisecond, k)
	{
		if (millisecond < 0 || millisecond >= 1000) throw ArgumentOutOfRangeException("millisecond");
	}

	DateTime DateTime::UtcNow()
	{
		auto since = std::chrono::duration_cast<std::chrono::duration<long long, std::ratio<1, 10000000>>>(std::chrono::system_clock::now().time_since_epoch());
		return DateTime(since.count() + UnixEpochTicks, DateTimeKind::Utc);
	}
	DateTime DateTime::Now() { return UtcNow().ToLocalTime(); }
	DateTime DateTime::Today() { return Now().Date(); }

	int DateTime::Year() const { int y, m, d; GetDate(ticks, y, m, d); return y; }
	int DateTime::Month() const { int y, m, d; GetDate(ticks, y, m, d); return m; }
	int DateTime::Day() const { int y, m, d; GetDate(ticks, y, m, d); return d; }
	int DateTime::DayOfYear() const { int y, m, d, n; GetDate(ticks, y, m, d, &n); return n; }

	bool DateTime::IsLeapYear(int year)
	{
		if (year < 1 || year > 9999) throw ArgumentOutOfRangeException("Year must be between 1 and 9999.");
		return (year & 3) == 0 && ((year % 100) != 0 || (year % 400) == 0);
	}

	int DateTime::DaysInMonth(int year, int month)
	{
		if (month < 1 || month > 12) throw ArgumentOutOfRangeException("Month must be between one and twelve.");
		const int* days = IsLeapYear(year) ? DaysToMonth366 : DaysToMonth365;
		return days[month] - days[month - 1];
	}

	DateTime DateTime::AddTicks(long long value) const
	{
		if (value > MaxTicks - ticks || value < -ticks) throw ArgumentOutOfRangeException("The added or subtracted value results in an un-representable DateTime.");
		return DateTime(ticks + value, kind);
	}

	namespace
	{
		// DateTime.AddUnits (.NET 7+): whole units exactly, the fraction truncated to ticks.
		DateTime AddUnits(const DateTime& dt, double value, long long maxUnitCount, long long ticksPerUnit)
		{
			if (std::fabs(value) > static_cast<double>(maxUnitCount) || std::isnan(value))
				throw ArgumentOutOfRangeException("The added or subtracted value results in an un-representable DateTime.");
			double integralPart = std::trunc(value);
			double fractionalPart = value - integralPart;
			long long t = static_cast<long long>(integralPart) * ticksPerUnit;
			t += static_cast<long long>(fractionalPart * static_cast<double>(ticksPerUnit));
			return dt.AddTicks(t);
		}
	}

	DateTime DateTime::AddMilliseconds(double value) const { return AddUnits(*this, value, MaxMillis, TicksPerMillisecond); }
	DateTime DateTime::AddSeconds(double value) const { return AddUnits(*this, value, MaxSeconds, TicksPerSecond); }
	DateTime DateTime::AddMinutes(double value) const { return AddUnits(*this, value, MaxMinutes, TicksPerMinute); }
	DateTime DateTime::AddHours(double value) const { return AddUnits(*this, value, MaxHours, TicksPerHour); }
	DateTime DateTime::AddDays(double value) const { return AddUnits(*this, value, MaxDays, TicksPerDay); }

	DateTime DateTime::AddMonths(int months) const
	{
		if (months < -120000 || months > 120000) throw ArgumentOutOfRangeException("Months value must be between +/-120000.");
		int y, m, d;
		GetDate(ticks, y, m, d);
		int i = m - 1 + months;
		if (i >= 0) { m = i % 12 + 1; y += i / 12; }
		else { m = 12 + (i + 1) % 12; y += (i - 11) / 12; }
		if (y < 1 || y > 9999) throw ArgumentOutOfRangeException("The added or subtracted value results in an un-representable DateTime.");
		int days = DaysInMonth(y, m);
		if (d > days) d = days;
		return DateTime(DateToTicks(y, m, d) + ticks % TicksPerDay, kind);
	}

	DateTime DateTime::AddYears(int value) const
	{
		if (value < -10000 || value > 10000) throw ArgumentOutOfRangeException("Years value must be between +/-10000.");
		return AddMonths(value * 12);
	}

	DateTime DateTime::ToUniversalTime() const
	{
		if (kind == DateTimeKind::Utc) return *this;
		long long t = ticks - LocalOffsetAtLocal(ticks);
		if (t > MaxTicks) return DateTime(MaxTicks, DateTimeKind::Utc);
		if (t < MinTicks) return DateTime(MinTicks, DateTimeKind::Utc);
		return DateTime(t, DateTimeKind::Utc);
	}

	DateTime DateTime::ToLocalTime() const
	{
		if (kind == DateTimeKind::Local) return *this;
		long long t = ticks + LocalOffsetAtUtc(ticks);
		if (t > MaxTicks) return DateTime(MaxTicks, DateTimeKind::Local);
		if (t < MinTicks) return DateTime(MinTicks, DateTimeKind::Local);
		return DateTime(t, DateTimeKind::Local);
	}

	long long DateTime::ToBinary() const
	{
		if (kind == DateTimeKind::Local)
		{
			// Local times are stored as UTC ticks so they survive a time-zone change, as .NET does.
			long long stored = ticks - LocalOffsetAtLocal(ticks);
			if (stored < 0) stored = TicksCeiling + stored;
			return static_cast<long long>(static_cast<unsigned long long>(stored) | KindLocal);
		}
		return static_cast<long long>(static_cast<unsigned long long>(ticks) | (kind == DateTimeKind::Utc ? KindUtc : 0ull));
	}

	DateTime DateTime::FromBinary(long long dateData)
	{
		unsigned long long u = static_cast<unsigned long long>(dateData);
		if ((u & KindLocal) != 0)
		{
			long long t = static_cast<long long>(u & TicksMask);
			if (t > TicksCeiling - TicksPerDay) t -= TicksCeiling;
			long long offset = LocalOffsetAtUtc(t < MinTicks ? MinTicks : (t > MaxTicks ? MaxTicks : t));
			t += offset;
			if (t < 0) t += TicksPerDay;
			if (t < MinTicks || t > MaxTicks) throw ArgumentException("The binary data must result in a DateTime value between DateTime.MinValue and DateTime.MaxValue.");
			return DateTime(t, DateTimeKind::Local);
		}
		long long t = static_cast<long long>(u & TicksMask);
		if (t < MinTicks || t > MaxTicks) throw ArgumentException("The binary data must result in a DateTime value between DateTime.MinValue and DateTime.MaxValue.");
		return DateTime(t, (u & KindUtc) != 0 ? DateTimeKind::Utc : DateTimeKind::Unspecified);
	}

	String DateTime::ToString() const { return ToString("G"); }

	String DateTime::ToString(std::string_view format) const
	{
		if (format.empty()) format = "G";
		if (format.size() == 1)
		{
			// Standard formats, invariant culture (DateTimeFormatInfo.InvariantInfo).
			switch (format[0])
			{
			case 'd': return FormatCustom(*this, "MM/dd/yyyy");
			case 'D': return FormatCustom(*this, "dddd, dd MMMM yyyy");
			case 'f': return FormatCustom(*this, "dddd, dd MMMM yyyy HH:mm");
			case 'F': return FormatCustom(*this, "dddd, dd MMMM yyyy HH:mm:ss");
			case 'g': return FormatCustom(*this, "MM/dd/yyyy HH:mm");
			case 'G': return FormatCustom(*this, "MM/dd/yyyy HH:mm:ss");
			case 'm': case 'M': return FormatCustom(*this, "MMMM dd");
			case 'o': case 'O': return FormatCustom(*this, "yyyy'-'MM'-'dd'T'HH':'mm':'ss'.'fffffffK");
			case 'r': case 'R': return FormatCustom(kind == DateTimeKind::Local ? ToUniversalTime() : *this, "ddd, dd MMM yyyy HH':'mm':'ss 'GMT'");
			case 's': return FormatCustom(*this, "yyyy'-'MM'-'dd'T'HH':'mm':'ss");
			case 't': return FormatCustom(*this, "HH:mm");
			case 'T': return FormatCustom(*this, "HH:mm:ss");
			case 'u': return FormatCustom(kind == DateTimeKind::Local ? ToUniversalTime() : *this, "yyyy'-'MM'-'dd HH':'mm':'ss'Z'");
			case 'U': return FormatCustom(ToUniversalTime(), "dddd, dd MMMM yyyy HH:mm:ss");
			case 'y': case 'Y': return FormatCustom(*this, "yyyy MMMM");
			default: throw FormatException("Input string was not in a correct format.");
			}
		}
		return FormatCustom(*this, format);
	}
}

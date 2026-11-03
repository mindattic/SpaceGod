#pragma once
// System.DateTime and System.TimeSpan, as .NET (Core 3.0+/net10.0) defines them: 100 ns ticks since
// 0001-01-01, a DateTimeKind, the same arithmetic (AddDays etc. split the double into whole and fractional
// units and truncate, as .NET 7+ does), ToBinary/FromBinary and invariant-culture ToString formats.
// Included at the end of Cs.h; implementation in CsDateTime.cpp.
// BCL properties are methods (`t.TotalDays()`, `d.Ticks()`, `DateTime::UtcNow()`), per docs/MIGRATION.md;
// BCL static fields are static members (`DateTime::MaxValue`, `TimeSpan::Zero`).

namespace cs
{
	enum class DateTimeKind : int { Unspecified = 0, Utc = 1, Local = 2 };
	SPACEGODCORE_API const char* ToString(DateTimeKind v);
	enum class DayOfWeek : int { Sunday = 0, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday };
	SPACEGODCORE_API const char* ToString(DayOfWeek v);

	struct SPACEGODCORE_API TimeSpan
	{
		static constexpr long long TicksPerMillisecond = 10000;
		static constexpr long long TicksPerSecond = TicksPerMillisecond * 1000;
		static constexpr long long TicksPerMinute = TicksPerSecond * 60;
		static constexpr long long TicksPerHour = TicksPerMinute * 60;
		static constexpr long long TicksPerDay = TicksPerHour * 24;

		constexpr TimeSpan() = default;
		constexpr explicit TimeSpan(long long ticks) : ticks(ticks) {}
		TimeSpan(int hours, int minutes, int seconds);
		TimeSpan(int days, int hours, int minutes, int seconds, int milliseconds = 0);

		static const TimeSpan Zero;
		static const TimeSpan MaxValue;
		static const TimeSpan MinValue;

		long long Ticks() const { return ticks; }

		int Days() const { return static_cast<int>(ticks / TicksPerDay); }
		int Hours() const { return static_cast<int>((ticks / TicksPerHour) % 24); }
		int Minutes() const { return static_cast<int>((ticks / TicksPerMinute) % 60); }
		int Seconds() const { return static_cast<int>((ticks / TicksPerSecond) % 60); }
		int Milliseconds() const { return static_cast<int>((ticks / TicksPerMillisecond) % 1000); }
		double TotalDays() const { return static_cast<double>(ticks) / TicksPerDay; }
		double TotalHours() const { return static_cast<double>(ticks) / TicksPerHour; }
		double TotalMinutes() const { return static_cast<double>(ticks) / TicksPerMinute; }
		double TotalSeconds() const { return static_cast<double>(ticks) / TicksPerSecond; }
		double TotalMilliseconds() const;

		static TimeSpan FromDays(double value);
		static TimeSpan FromHours(double value);
		static TimeSpan FromMinutes(double value);
		static TimeSpan FromSeconds(double value);
		static TimeSpan FromMilliseconds(double value);
		static constexpr TimeSpan FromTicks(long long ticks) { return TimeSpan(ticks); }

		TimeSpan Duration() const;
		TimeSpan Negate() const;
		TimeSpan Add(TimeSpan t) const;
		TimeSpan Subtract(TimeSpan t) const;
		int CompareTo(TimeSpan o) const { return ticks < o.ticks ? -1 : (ticks > o.ticks ? 1 : 0); }
		size_t HashCode() const { return std::hash<long long>{}(ticks); }
		String ToString() const; // "[-][d.]hh:mm:ss[.fffffff]" (the "c" format)

		TimeSpan operator-() const { return Negate(); }
		TimeSpan operator+(TimeSpan t) const { return Add(t); }
		TimeSpan operator-(TimeSpan t) const { return Subtract(t); }
		TimeSpan& operator+=(TimeSpan t) { return *this = Add(t); }
		TimeSpan& operator-=(TimeSpan t) { return *this = Subtract(t); }
		bool operator==(const TimeSpan& o) const { return ticks == o.ticks; }
		auto operator<=>(const TimeSpan& o) const { return ticks <=> o.ticks; }

	private:
		long long ticks = 0;
	};

	struct SPACEGODCORE_API DateTime
	{
		static constexpr long long MinTicks = 0;
		static constexpr long long MaxTicks = 3155378975999999999LL; // 9999-12-31 23:59:59.9999999
		static constexpr long long UnixEpochTicks = 621355968000000000LL;

		constexpr DateTime() = default;
		explicit DateTime(long long ticks, DateTimeKind kind = DateTimeKind::Unspecified);
		DateTime(int year, int month, int day, DateTimeKind kind = DateTimeKind::Unspecified);
		DateTime(int year, int month, int day, int hour, int minute, int second, DateTimeKind kind = DateTimeKind::Unspecified);
		DateTime(int year, int month, int day, int hour, int minute, int second, int millisecond, DateTimeKind kind = DateTimeKind::Unspecified);

		static const DateTime MinValue;
		static const DateTime MaxValue;
		static const DateTime UnixEpoch;
		static DateTime UtcNow();
		static DateTime Now();   // local time, from the OS time zone
		static DateTime Today(); // Now().Date()

		long long Ticks() const { return ticks; }
		DateTimeKind Kind() const { return kind; }
		int Year() const;
		int Month() const;
		int Day() const;
		int DayOfYear() const;
		cs::DayOfWeek DayOfWeek() const { return static_cast<cs::DayOfWeek>((ticks / TimeSpan::TicksPerDay + 1) % 7); }
		int Hour() const { return static_cast<int>((ticks / TimeSpan::TicksPerHour) % 24); }
		int Minute() const { return static_cast<int>((ticks / TimeSpan::TicksPerMinute) % 60); }
		int Second() const { return static_cast<int>((ticks / TimeSpan::TicksPerSecond) % 60); }
		int Millisecond() const { return static_cast<int>((ticks / TimeSpan::TicksPerMillisecond) % 1000); }
		DateTime Date() const { return DateTime(ticks - ticks % TimeSpan::TicksPerDay, kind); }
		TimeSpan TimeOfDay() const { return TimeSpan(ticks % TimeSpan::TicksPerDay); }

		DateTime Add(TimeSpan value) const { return AddTicks(value.Ticks()); }
		DateTime AddTicks(long long value) const;
		DateTime AddMilliseconds(double value) const;
		DateTime AddSeconds(double value) const;
		DateTime AddMinutes(double value) const;
		DateTime AddHours(double value) const;
		DateTime AddDays(double value) const;
		DateTime AddMonths(int months) const;
		DateTime AddYears(int value) const;
		DateTime Subtract(TimeSpan value) const { return AddTicks(-value.Ticks()); }
		TimeSpan Subtract(DateTime value) const { return TimeSpan(ticks - value.ticks); }

		DateTime ToUniversalTime() const; // Utc stays; Local and Unspecified are taken as local time
		DateTime ToLocalTime() const;     // Local stays; Utc and Unspecified are taken as UTC
		static DateTime SpecifyKind(DateTime value, DateTimeKind kind) { return DateTime(value.ticks, kind); }
		long long ToBinary() const;
		static DateTime FromBinary(long long dateData);
		static bool IsLeapYear(int year);
		static int DaysInMonth(int year, int month);

		int CompareTo(DateTime o) const { return ticks < o.ticks ? -1 : (ticks > o.ticks ? 1 : 0); }
		size_t HashCode() const { return std::hash<long long>{}(ticks); }
		// ToString(): invariant "MM/dd/yyyy HH:mm:ss". ToString(format): the standard date formats d D f F g G
		// M o O R s t T u U y, and custom formats (yyyy MM MMM dd d HH H hh h mm m ss s f..fffffff F.. tt t K z
		// 'literal' "literal" \c), invariant culture.
		String ToString() const;
		String ToString(std::string_view format) const;

		// Comparisons and differences use Ticks only (Kind is ignored), as .NET.
		bool operator==(const DateTime& o) const { return ticks == o.ticks; }
		auto operator<=>(const DateTime& o) const { return ticks <=> o.ticks; }
		TimeSpan operator-(const DateTime& o) const { return Subtract(o); }
		DateTime operator-(TimeSpan t) const { return Subtract(t); }
		DateTime operator+(TimeSpan t) const { return Add(t); }
		DateTime& operator+=(TimeSpan t) { return *this = Add(t); }
		DateTime& operator-=(TimeSpan t) { return *this = Subtract(t); }

	private:
		long long ticks = 0;
		DateTimeKind kind = DateTimeKind::Unspecified;
	};
}

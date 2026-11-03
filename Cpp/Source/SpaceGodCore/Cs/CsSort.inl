#pragma once
// List<T>.Sort: an exact port of .NET's ArraySortHelper<T> IntrospectiveSort (dotnet/runtime,
// System.Private.CoreLib/src/System/Collections/Generic/ArraySortHelper.cs, .NET 5+ span version).
// The sort is unstable, so equal keys land wherever .NET's partitioning puts them; reproducing the exact
// sequence of compares and swaps is what keeps the C++ and C# simulations in the same order.
// Included at the end of Cs.h.

namespace cs
{
	namespace SortDetail
	{
		constexpr int IntrosortSizeThreshold = 16;

		inline int Log2(uint32_t v)
		{
			int r = 0;
			while (v >>= 1) ++r;
			return r;
		}

		// Comparison<T> sort (ArraySortHelper<T>.IntrospectiveSort(Span<T>, Comparison<T>)).
		template <class T, class Cmp> struct Introsort
		{
			T* keys;
			Cmp& comparer;

			int C(const T& a, const T& b) { return static_cast<int>(comparer(a, b)); }

			void SwapIfGreater(int i, int j)
			{
				if (C(keys[i], keys[j]) > 0) std::swap(keys[i], keys[j]);
			}

			static void Swap(T* a, int i, int j) { std::swap(a[i], a[j]); }

			void InsertionSort(int lo, int n)
			{
				T* k = keys + lo;
				for (int i = 0; i < n - 1; i++)
				{
					T t = std::move(k[i + 1]);
					int j = i;
					while (j >= 0 && C(t, k[j]) < 0)
					{
						k[j + 1] = std::move(k[j]);
						j--;
					}
					k[j + 1] = std::move(t);
				}
			}

			void DownHeap(T* k, int i, int n)
			{
				T d = std::move(k[i - 1]);
				while (i <= n >> 1)
				{
					int child = 2 * i;
					if (child < n && C(k[child - 1], k[child]) < 0) child++;
					if (!(C(d, k[child - 1]) < 0)) break;
					k[i - 1] = std::move(k[child - 1]);
					i = child;
				}
				k[i - 1] = std::move(d);
			}

			void HeapSort(int lo, int n)
			{
				T* k = keys + lo;
				for (int i = n >> 1; i >= 1; i--) DownHeap(k, i, n);
				for (int i = n; i > 1; i--)
				{
					Swap(k, 0, i - 1);
					DownHeap(k, 1, i - 1);
				}
			}

			// Returns the pivot's index relative to lo.
			int PickPivotAndPartition(int lo, int n)
			{
				T* k = keys + lo;
				int hi = n - 1;
				// Compute median-of-three. But also partition them, since we've done the comparison.
				int middle = hi >> 1;
				// Sort lo, mid and hi appropriately, then pick mid as the pivot.
				if (C(k[0], k[middle]) > 0) Swap(k, 0, middle);
				if (C(k[0], k[hi]) > 0) Swap(k, 0, hi);
				if (C(k[middle], k[hi]) > 0) Swap(k, middle, hi);

				T pivot = k[middle];
				Swap(k, middle, hi - 1);
				int left = 0, right = hi - 1; // We already partitioned lo and hi and put the pivot in hi - 1.
				while (left < right)
				{
					while (left < (hi - 1) && C(k[++left], pivot) < 0) {}
					while (right > 0 && C(pivot, k[--right]) < 0) {}
					if (left >= right) break;
					Swap(k, left, right);
				}
				// Put pivot in the right location.
				if (left != hi - 1) Swap(k, left, hi - 1);
				return left;
			}

			void IntroSort(int lo, int partitionSize, int depthLimit)
			{
				while (partitionSize > 1)
				{
					if (partitionSize <= IntrosortSizeThreshold)
					{
						if (partitionSize == 2)
						{
							SwapIfGreater(lo, lo + 1);
							return;
						}
						if (partitionSize == 3)
						{
							SwapIfGreater(lo, lo + 1);
							SwapIfGreater(lo, lo + 2);
							SwapIfGreater(lo + 1, lo + 2);
							return;
						}
						InsertionSort(lo, partitionSize);
						return;
					}
					if (depthLimit == 0)
					{
						HeapSort(lo, partitionSize);
						return;
					}
					depthLimit--;
					int p = PickPivotAndPartition(lo, partitionSize);
					// Note we've already partitioned around the pivot and do not have to move the pivot again.
					IntroSort(lo + p + 1, partitionSize - (p + 1), depthLimit);
					partitionSize = p;
				}
			}

			void Run(int n)
			{
				if (n > 1) IntroSort(0, n, 2 * (Log2(static_cast<uint32_t>(n)) + 1));
			}
		};

		template <class T, class Cmp> void IntrospectiveSort(std::vector<T>& v, Cmp& cmp)
		{
			Introsort<T, Cmp> s{ v.data(), cmp };
			s.Run(static_cast<int>(v.size()));
		}
	}

	template <class T> template <class Cmp> void List<T>::Sort(Cmp compare)
	{
		SortDetail::IntrospectiveSort(items, compare);
	}

	// List<T>.Sort() with Comparer<T>.Default: .NET's GenericArraySortHelper<T>. It runs the same introsort with
	// `<` comparisons (identical decisions to the Comparison form with Compare), after first moving every NaN to
	// the front for float/double (SortUtils.MoveNansToFront).
	template <class T> void List<T>::Sort()
	{
		size_t start = 0;
		if constexpr (std::is_floating_point_v<T>)
		{
			for (size_t i = 0; i < items.size(); i++)
			{
				if (std::isnan(items[i]))
				{
					std::swap(items[start], items[i]);
					start++;
				}
			}
		}
		auto cmp = [](const T& a, const T& b) { return Compare(a, b); };
		SortDetail::Introsort<T, decltype(cmp)> s{ items.data() + start, cmp };
		s.Run(static_cast<int>(items.size() - start));
	}
}

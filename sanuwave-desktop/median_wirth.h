#ifndef MEDIAN_WIRTH_
#define MEDIAN_WIRTH_

#include <vector>
#include <stdexcept>

// Median via Wirth's selection algorithm.
// Ref: N. Wirth, "Algorithms + Data Structures = Programs", 1976, p.84
//
// The public interface is non-destructive — input data is never modified.
// The internal partitioning operates on a local copy.

namespace Sanuwave {

template <typename T>
class MedianWirth {
public:

    // Compute median of n elements at pointer a.
    // Input array is not modified.
    static T median(const T* a, int n) {
        if (a == nullptr)
            throw std::invalid_argument("MedianWirth::median: null input pointer");
        if (n <= 0)
            throw std::invalid_argument("MedianWirth::median: n must be > 0");

        std::vector<T> copy(a, a + n);
        int k = (n & 1) ? (n / 2) : (n / 2 - 1);
        return kth_smallest(copy.data(), n, k);
    }

    // Convenience overload for contiguous containers (std::vector, std::array).
    template <typename Container>
    static T median(const Container& c) {
        return median(c.data(), static_cast<int>(c.size()));
    }

private:

    // Swaps two elements.
    static void elemSwap(T& a, T& b) {
        T temp = a;
        a = b;
        b = temp;
    }

    // Partitions a[] in place — intentionally destructive.
    // Always called via the public median() which passes a copy.
    // Returns the k-th smallest element (0-indexed).
    static T kth_smallest(T* a, int n, int k) {
        int l = 0;
        int m = n - 1;
        while (l < m) {
            T x = a[k];
            int i = l;
            int j = m;
            do {
                while (a[i] < x) i++;
                while (x < a[j]) j--;
                if (i <= j) {
                    elemSwap(a[i], a[j]);
                    i++;
                    j--;
                }
            } while (i <= j);
            if (j < k) l = i;
            if (k < i) m = j;
        }
        return a[k];
    }
};

} // namespace Sanuwave
#endif

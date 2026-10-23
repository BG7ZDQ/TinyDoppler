#pragma once

#include <cstdint>

namespace tiny_doppler
{
// POSIX V1 shared-memory record. The caller owns the exclusive writer lock.
inline void publishControlRecord(std::uint8_t *base, std::int64_t target,
				 std::int64_t correction,
				 std::int64_t timestamp, bool valid)
{
	auto *sequence = reinterpret_cast<std::uint32_t *>(base + 36);
	const auto previous = __atomic_load_n(sequence, __ATOMIC_RELAXED);
	__atomic_store_n(sequence, (previous & ~1U) + 1U, __ATOMIC_RELAXED);
	// Every payload store releases the preceding odd marker. The reader
	// acquires every field, so observing any new payload also orders its
	// final sequence check after this odd marker (including on ARM).
	__atomic_store_n(reinterpret_cast<std::uint32_t *>(base), 0x504f4441U,
			 __ATOMIC_RELEASE);
	__atomic_store_n(reinterpret_cast<std::uint32_t *>(base + 4), 1U,
			 __ATOMIC_RELEASE);
	__atomic_store_n(reinterpret_cast<std::int64_t *>(base + 8), target,
			 __ATOMIC_RELEASE);
	__atomic_store_n(reinterpret_cast<std::int64_t *>(base + 16),
			 correction, __ATOMIC_RELEASE);
	__atomic_store_n(reinterpret_cast<std::int64_t *>(base + 24), timestamp,
			 __ATOMIC_RELEASE);
	__atomic_store_n(reinterpret_cast<std::int32_t *>(base + 32),
			 valid ? 1 : 0, __ATOMIC_RELEASE);
	__atomic_store_n(sequence, (previous & ~1U) + 2U, __ATOMIC_RELEASE);
}
} // namespace tiny_doppler

#pragma once

#include <cstdint>

namespace kvcache {

// 64-bit handle layout:
// [ type:8 | generation:24 | position:32 ]
class FdToken final {
public:
    using raw_type = std::uint64_t;

    static constexpr std::uint32_t kPositionBits = 32;
    static constexpr std::uint32_t kGenerationBits = 24;
    static constexpr std::uint32_t kTypeBits = 8;

    static constexpr raw_type kPositionMask = (raw_type{1} << kPositionBits) - 1;
    static constexpr raw_type kGenerationMask =
        ((raw_type{1} << kGenerationBits) - 1) << kPositionBits;
    static constexpr raw_type kTypeMask =
        ((raw_type{1} << kTypeBits) - 1) << (kPositionBits + kGenerationBits);

    static constexpr raw_type kNull = 0;

    static constexpr raw_type Make(std::uint8_t type,
                                   std::uint32_t generation,
                                   std::uint32_t position) noexcept {
        const raw_type t = (static_cast<raw_type>(type) <<
                            (kPositionBits + kGenerationBits)) &
                           kTypeMask;
        const raw_type g =
            (static_cast<raw_type>(generation) << kPositionBits) & kGenerationMask;
        const raw_type p = static_cast<raw_type>(position) & kPositionMask;
        return t | g | p;
    }

    static constexpr std::uint8_t Type(raw_type token) noexcept {
        return static_cast<std::uint8_t>((token & kTypeMask) >>
                                         (kPositionBits + kGenerationBits));
    }

    static constexpr std::uint32_t Generation(raw_type token) noexcept {
        return static_cast<std::uint32_t>((token & kGenerationMask) >> kPositionBits);
    }

    static constexpr std::uint32_t Position(raw_type token) noexcept {
        return static_cast<std::uint32_t>(token & kPositionMask);
    }

    static constexpr bool IsNull(raw_type token) noexcept { return token == kNull; }
};

}  // namespace kvcache


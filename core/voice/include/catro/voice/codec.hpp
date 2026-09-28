#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace catro::voice {

inline constexpr std::uint32_t kSampleRate = 48'000;
inline constexpr std::uint32_t kFrameSamples = 960; // 20 ms at 48 kHz.
inline constexpr std::size_t kMaxOpusPacketBytes = 1275;

enum class CodecErrorCode {
    invalid_argument,
    allocation_failed,
    codec_failure,
    output_too_small,
};

struct CodecError {
    CodecErrorCode code = CodecErrorCode::codec_failure;
    int native_code = 0;

    friend bool operator==(const CodecError&, const CodecError&) = default;
};

[[nodiscard]] constexpr std::string_view name(CodecErrorCode code) noexcept {
    switch (code) {
    case CodecErrorCode::invalid_argument:
        return "invalid argument";
    case CodecErrorCode::allocation_failed:
        return "allocation failed";
    case CodecErrorCode::output_too_small:
        return "output too small";
    case CodecErrorCode::codec_failure:
        break;
    }
    return "codec failure";
}

enum class CodecApplication : std::uint8_t {
    voice,
    audio,
};

struct EncoderConfig {
    std::int32_t bitrate = 48'000;
    std::uint32_t channels = 1;
    CodecApplication application = CodecApplication::voice;
    int complexity = 10;
    int expected_packet_loss_percent = 5;
    bool inband_fec = true;
    bool vbr = true;
};

class Encoder {
public:
    using CreateResult = std::variant<std::unique_ptr<Encoder>, CodecError>;

    [[nodiscard]] static CreateResult create(const EncoderConfig& config = {}) noexcept;
    ~Encoder();

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;

    [[nodiscard]] std::variant<std::size_t, CodecError> encode(std::span<const float> pcm,
                                                               std::span<std::byte> output) noexcept;
    [[nodiscard]] std::optional<CodecError> reset() noexcept;

private:
    struct Impl;
    explicit Encoder(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

class Decoder {
public:
    using CreateResult = std::variant<std::unique_ptr<Decoder>, CodecError>;

    [[nodiscard]] static CreateResult create(
        std::uint32_t channels = 1) noexcept;
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // A normal packet decodes with decode_fec=false. When recovering the frame immediately before
    // a received packet, pass decode_fec=true. PLC uses conceal() with no packet bytes.
    [[nodiscard]] std::variant<std::size_t, CodecError> decode(std::span<const std::byte> packet,
                                                               std::span<float> pcm,
                                                               bool decode_fec = false) noexcept;
    [[nodiscard]] std::variant<std::size_t, CodecError> conceal(std::span<float> pcm) noexcept;
    [[nodiscard]] std::optional<CodecError> reset() noexcept;

private:
    struct Impl;
    explicit Decoder(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace catro::voice

#include <catro/voice/codec.hpp>

#include <opus.h>

#include <algorithm>
#include <new>
#include <utility>

namespace catro::voice {
namespace {

CodecError opus_error(int code) noexcept {
    return {CodecErrorCode::codec_failure, code};
}

} // namespace

struct Encoder::Impl {
    explicit Impl(OpusEncoder* value) noexcept : handle(value) {}
    ~Impl() {
        if (handle != nullptr) {
            opus_encoder_destroy(handle);
        }
    }

    OpusEncoder* handle = nullptr;
};

Encoder::Encoder(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Encoder::~Encoder() = default;

Encoder::CreateResult Encoder::create(const EncoderConfig& config) noexcept {
    if (config.bitrate <= 0 || config.complexity < 0 || config.complexity > 10 ||
        config.expected_packet_loss_percent < 0 || config.expected_packet_loss_percent > 100) {
        return CodecError{CodecErrorCode::invalid_argument};
    }

    int error = OPUS_OK;
    auto* handle = opus_encoder_create(static_cast<opus_int32>(kSampleRate), 1, OPUS_APPLICATION_VOIP, &error);
    if (handle == nullptr || error != OPUS_OK) {
        return error == OPUS_ALLOC_FAIL ? CodecError{CodecErrorCode::allocation_failed, error} : opus_error(error);
    }

    const auto destroy_on_error = [&](int result) -> CreateResult {
        opus_encoder_destroy(handle);
        return opus_error(result);
    };
    if (const auto result = opus_encoder_ctl(handle, OPUS_SET_BITRATE(config.bitrate)); result != OPUS_OK) {
        return destroy_on_error(result);
    }
    if (const auto result = opus_encoder_ctl(handle, OPUS_SET_COMPLEXITY(config.complexity)); result != OPUS_OK) {
        return destroy_on_error(result);
    }
    if (const auto result = opus_encoder_ctl(handle, OPUS_SET_VBR(config.vbr ? 1 : 0)); result != OPUS_OK) {
        return destroy_on_error(result);
    }
    if (const auto result = opus_encoder_ctl(handle, OPUS_SET_INBAND_FEC(config.inband_fec ? 1 : 0)); result != OPUS_OK) {
        return destroy_on_error(result);
    }
    if (const auto result = opus_encoder_ctl(handle, OPUS_SET_PACKET_LOSS_PERC(config.expected_packet_loss_percent));
        result != OPUS_OK) {
        return destroy_on_error(result);
    }

    auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl(handle));
    if (!impl) {
        opus_encoder_destroy(handle);
        return CodecError{CodecErrorCode::allocation_failed, OPUS_ALLOC_FAIL};
    }
    auto encoder = std::unique_ptr<Encoder>(new (std::nothrow) Encoder(std::move(impl)));
    if (!encoder) {
        return CodecError{CodecErrorCode::allocation_failed, OPUS_ALLOC_FAIL};
    }
    return encoder;
}

std::variant<std::size_t, CodecError> Encoder::encode(std::span<const float> pcm,
                                                       std::span<std::byte> output) noexcept {
    if (pcm.size() != kFrameSamples) {
        return CodecError{CodecErrorCode::invalid_argument};
    }
    if (output.empty()) {
        return CodecError{CodecErrorCode::output_too_small};
    }
    const auto capacity = std::min<std::size_t>(output.size(), kMaxOpusPacketBytes);
    const auto encoded = opus_encode_float(impl_->handle, pcm.data(), static_cast<int>(kFrameSamples),
                                           reinterpret_cast<unsigned char*>(output.data()),
                                           static_cast<opus_int32>(capacity));
    if (encoded < 0) {
        return encoded == OPUS_BUFFER_TOO_SMALL ? CodecError{CodecErrorCode::output_too_small, encoded}
                                                : opus_error(encoded);
    }
    return static_cast<std::size_t>(encoded);
}

struct Decoder::Impl {
    explicit Impl(OpusDecoder* value) noexcept : handle(value) {}
    ~Impl() {
        if (handle != nullptr) {
            opus_decoder_destroy(handle);
        }
    }

    OpusDecoder* handle = nullptr;
};

Decoder::Decoder(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Decoder::~Decoder() = default;

Decoder::CreateResult Decoder::create() noexcept {
    int error = OPUS_OK;
    auto* handle = opus_decoder_create(static_cast<opus_int32>(kSampleRate), 1, &error);
    if (handle == nullptr || error != OPUS_OK) {
        return error == OPUS_ALLOC_FAIL ? CodecError{CodecErrorCode::allocation_failed, error} : opus_error(error);
    }
    auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl(handle));
    if (!impl) {
        opus_decoder_destroy(handle);
        return CodecError{CodecErrorCode::allocation_failed, OPUS_ALLOC_FAIL};
    }
    auto decoder = std::unique_ptr<Decoder>(new (std::nothrow) Decoder(std::move(impl)));
    if (!decoder) {
        return CodecError{CodecErrorCode::allocation_failed, OPUS_ALLOC_FAIL};
    }
    return decoder;
}

std::variant<std::size_t, CodecError> Decoder::decode(std::span<const std::byte> packet,
                                                      std::span<float> pcm, bool decode_fec) noexcept {
    if (packet.empty() || packet.size() > kMaxOpusPacketBytes) {
        return CodecError{CodecErrorCode::invalid_argument};
    }
    if (pcm.size() < kFrameSamples) {
        return CodecError{CodecErrorCode::output_too_small};
    }
    const auto decoded = opus_decode_float(impl_->handle,
                                           reinterpret_cast<const unsigned char*>(packet.data()),
                                           static_cast<opus_int32>(packet.size()), pcm.data(),
                                           static_cast<int>(kFrameSamples), decode_fec ? 1 : 0);
    if (decoded < 0) {
        return opus_error(decoded);
    }
    return static_cast<std::size_t>(decoded);
}

std::variant<std::size_t, CodecError> Decoder::conceal(std::span<float> pcm) noexcept {
    if (pcm.size() < kFrameSamples) {
        return CodecError{CodecErrorCode::output_too_small};
    }
    const auto decoded =
        opus_decode_float(impl_->handle, nullptr, 0, pcm.data(), static_cast<int>(kFrameSamples), 0);
    if (decoded < 0) {
        return opus_error(decoded);
    }
    return static_cast<std::size_t>(decoded);
}

} // namespace catro::voice

#include <catro/voice/pipeline.hpp>

#include <new>
#include <utility>

namespace catro::voice {

VoicePipeline::VoicePipeline(const VoicePipelineConfig& config, std::unique_ptr<Encoder> encoder,
                             std::unique_ptr<Decoder> decoder)
    : capture_(config.capture_queue_frames), render_(config.render_queue_frames),
      encoder_(std::move(encoder)), decoder_(std::move(decoder)),
      jitter_(config.jitter_target_packets), stream_id_(config.local_stream_id),
      next_sequence_(config.initial_sequence), next_timestamp_(config.initial_timestamp) {}

VoicePipeline::CreateResult VoicePipeline::create(const VoicePipelineConfig& config) noexcept {
    if (config.local_stream_id == 0) {
        return CodecError{CodecErrorCode::invalid_argument};
    }

    auto encoder_result = Encoder::create(config.encoder);
    if (const auto* error = std::get_if<CodecError>(&encoder_result)) {
        return *error;
    }
    auto decoder_result = Decoder::create();
    if (const auto* error = std::get_if<CodecError>(&decoder_result)) {
        return *error;
    }

    try {
        return std::unique_ptr<VoicePipeline>(new VoicePipeline(
            config, std::move(std::get<std::unique_ptr<Encoder>>(encoder_result)),
            std::move(std::get<std::unique_ptr<Decoder>>(decoder_result))));
    } catch (const std::bad_alloc&) {
        return CodecError{CodecErrorCode::allocation_failed};
    }
}

std::variant<EncodeStep, CodecError> VoicePipeline::encode_next(OutboundDatagram& datagram) noexcept {
    datagram.size = 0;
    if (!capture_.try_pop(capture_frame_)) {
        return EncodeStep::no_frame;
    }

    auto output = std::span<std::byte>(datagram.bytes);
    auto payload = output.subspan(kVoiceHeaderBytes);
    const auto encoded = encoder_->encode(capture_frame_, payload);
    if (const auto* error = std::get_if<CodecError>(&encoded)) {
        encode_errors_.fetch_add(1, std::memory_order_relaxed);
        return *error;
    }

    const auto payload_size = std::get<std::size_t>(encoded);
    const auto header = write_packet_header(stream_id_, next_sequence_, next_timestamp_, payload_size, output);
    if (const auto* error = std::get_if<PacketError>(&header)) {
        encode_errors_.fetch_add(1, std::memory_order_relaxed);
        return CodecError{CodecErrorCode::codec_failure, -static_cast<int>(*error) - 1};
    }

    datagram.size = std::get<std::size_t>(header);
    encoded_frames_.fetch_add(1, std::memory_order_relaxed);
    outbound_bytes_.fetch_add(datagram.size, std::memory_order_relaxed);
    ++next_sequence_;
    next_timestamp_ += kFrameSamples;
    return EncodeStep::packet_ready;
}

ReceiveResult VoicePipeline::receive(std::span<const std::byte> datagram) noexcept {
    received_datagrams_.fetch_add(1, std::memory_order_relaxed);
    const auto parsed = parse_packet(datagram);
    if (const auto* error = std::get_if<PacketError>(&parsed)) {
        malformed_datagrams_.fetch_add(1, std::memory_order_relaxed);
        return *error;
    }
    return jitter_.push(std::get<VoicePacketView>(parsed));
}

std::variant<DecodeStep, CodecError> VoicePipeline::decode_next() noexcept {
    PlayoutFrame frame;
    const auto kind = jitter_.pull(frame);
    if (kind == PlayoutKind::waiting) {
        return DecodeStep::waiting;
    }

    std::variant<std::size_t, CodecError> decoded = CodecError{CodecErrorCode::codec_failure};
    DecodeStep step = DecodeStep::queued_packet;
    switch (kind) {
    case PlayoutKind::packet:
        decoded = decoder_->decode(frame.payload_view(), decoded_frame_);
        step = DecodeStep::queued_packet;
        break;
    case PlayoutKind::fec:
        decoded = decoder_->decode(frame.payload_view(), decoded_frame_, true);
        step = DecodeStep::queued_fec;
        break;
    case PlayoutKind::plc:
        decoded = decoder_->conceal(decoded_frame_);
        step = DecodeStep::queued_plc;
        break;
    case PlayoutKind::waiting:
        break;
    }

    if (const auto* error = std::get_if<CodecError>(&decoded)) {
        decode_errors_.fetch_add(1, std::memory_order_relaxed);
        return *error;
    }
    if (std::get<std::size_t>(decoded) != kFrameSamples) {
        decode_errors_.fetch_add(1, std::memory_order_relaxed);
        return CodecError{CodecErrorCode::codec_failure};
    }

    decoded_frames_.fetch_add(1, std::memory_order_relaxed);
    if (!render_.try_push(decoded_frame_)) {
        render_queue_full_.fetch_add(1, std::memory_order_relaxed);
        return DecodeStep::render_queue_full;
    }
    return step;
}

VoicePipelineStatistics VoicePipeline::statistics() const noexcept {
    return {
        .encoded_frames = encoded_frames_.load(std::memory_order_relaxed),
        .encode_errors = encode_errors_.load(std::memory_order_relaxed),
        .outbound_bytes = outbound_bytes_.load(std::memory_order_relaxed),
        .received_datagrams = received_datagrams_.load(std::memory_order_relaxed),
        .malformed_datagrams = malformed_datagrams_.load(std::memory_order_relaxed),
        .decoded_frames = decoded_frames_.load(std::memory_order_relaxed),
        .decode_errors = decode_errors_.load(std::memory_order_relaxed),
        .render_queue_full = render_queue_full_.load(std::memory_order_relaxed),
        .capture = capture_.statistics(),
        .render = render_.statistics(),
        .jitter = jitter_.statistics(),
    };
}

} // namespace catro::voice

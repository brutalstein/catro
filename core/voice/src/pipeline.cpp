#include <catro/voice/pipeline.hpp>

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace catro::voice {
namespace {

constexpr std::uint64_t kRemoteIdlePlayoutTicks = 100; // 2 s at 20 ms.
constexpr float kLimiterCeiling = 0.98F;
constexpr float kLimiterReleasePerFrame = 0.02F;
// A frame louder than -45 dBFS RMS is speech; the indicator then holds for 300 ms of quiet so it
// does not flicker between words.
constexpr float kSpeakingMeanSquare = 3.2e-5F;
constexpr std::uint16_t kSpeakingHangoverFrames = 15;

[[nodiscard]] float mean_square(const PcmFrame& frame) noexcept {
    float energy = 0.0F;
    for (const auto sample : frame) {
        energy += sample * sample;
    }
    return energy / static_cast<float>(frame.size());
}

// Returns true while the stream counts as speaking.
[[nodiscard]] bool track_speaking(std::uint16_t& hangover, float level, float threshold) noexcept {
    if (level > threshold) {
        hangover = kSpeakingHangoverFrames;
    } else if (hangover > 0) {
        --hangover;
    }
    return hangover > 0;
}

void accumulate_jitter(
    JitterStatistics& total,
    const JitterStatistics& value) noexcept {
    total.accepted += value.accepted;
    total.duplicates += value.duplicates;
    total.late += value.late;
    total.reordered += value.reordered;
    total.outside_window += value.outside_window;
    total.wrong_stream += value.wrong_stream;
    total.timestamp_mismatches += value.timestamp_mismatches;
    total.played += value.played;
    total.fec += value.fec;
    total.plc += value.plc;
    total.resyncs += value.resyncs;
    total.resync_discarded_packets +=
        value.resync_discarded_packets;
    total.buffered += value.buffered;
    // For a room this is a conservative aggregate bound rather than a statement that every remote
    // reached its individual peak in the same 20 ms interval. Single-peer semantics stay exact.
    total.peak_buffered += value.peak_buffered;
}

} // namespace

struct VoicePipeline::RemoteStream {
    RemoteStream(
        std::uint32_t next_stream_id,
        std::uint16_t jitter_target,
        std::unique_ptr<Decoder> next_decoder)
        : stream_id(next_stream_id),
          jitter(jitter_target),
          decoder(std::move(next_decoder)) {}

    std::uint32_t stream_id = 0;
    JitterBuffer jitter;
    std::unique_ptr<Decoder> decoder;
    PcmFrame decoded{};
    std::uint64_t last_packet_tick = 0;
    std::uint16_t speaking_hangover = 0;
};

VoicePipeline::VoicePipeline(
    const VoicePipelineConfig& config,
    std::unique_ptr<Encoder> encoder,
    std::unique_ptr<VoiceProcessor> processor)
    : capture_(config.capture_queue_frames),
      render_(config.render_queue_frames),
      encoder_(std::move(encoder)),
      processor_(std::move(processor)),
      controls_(config.controls),
      jitter_target_packets_(config.jitter_target_packets),
      stream_id_(config.local_stream_id),
      next_sequence_(config.initial_sequence),
      next_timestamp_(config.initial_timestamp) {}

VoicePipeline::~VoicePipeline() {
    if (controls_ == nullptr) {
        return;
    }
    controls_->set_local_speaking(false);
    for (const auto& remote : remotes_) {
        if (remote) {
            controls_->set_speaking(remote->stream_id, false);
        }
    }
}

VoicePipeline::CreateResult VoicePipeline::create(
    const VoicePipelineConfig& config) noexcept {
    if (config.local_stream_id == 0) {
        return CodecError{CodecErrorCode::invalid_argument};
    }

    auto encoder_result = Encoder::create(config.encoder);
    if (const auto* error =
            std::get_if<CodecError>(&encoder_result)) {
        return *error;
    }

    // A missing processor is not fatal: voice keeps flowing, just unprocessed.
    auto processor = config.processing
        ? VoiceProcessor::create(*config.processing)
        : nullptr;

    try {
        return std::unique_ptr<VoicePipeline>(
            new VoicePipeline(
                config,
                std::move(
                    std::get<std::unique_ptr<Encoder>>(
                        encoder_result)),
                std::move(processor)));
    } catch (const std::bad_alloc&) {
        return CodecError{CodecErrorCode::allocation_failed};
    }
}

void VoicePipeline::set_input_threshold(std::optional<float> dbfs) noexcept {
    gate_mean_square_ = dbfs ? std::pow(10.0F, std::clamp(*dbfs, kSilenceDbfs, 0.0F) / 10.0F)
                             : 0.0F;
}

void VoicePipeline::set_processing(
    const VoiceProcessingConfig& config) noexcept {
    if (processor_) {
        processor_->set_config(config);
    }
}

VoicePipeline::RemoteStream* VoicePipeline::find_remote(
    std::uint32_t stream_id) noexcept {
    for (auto& remote : remotes_) {
        if (remote && remote->stream_id == stream_id) {
            return remote.get();
        }
    }
    return nullptr;
}

std::variant<EncodeStep, CodecError>
VoicePipeline::encode_next(
    OutboundDatagram& datagram) noexcept {
    datagram.size = 0;

    // Keep at most 40 ms of complete microphone frames after scheduler/network stalls. When
    // media is intentionally discarded, advance both packet clocks so remote peers perform
    // FEC/PLC for the real elapsed gap instead of hearing time-compressed speech.
    const auto skipped_frames =
        capture_.trim_backlog(2);
    if (skipped_frames > 0) {
        if (const auto error = encoder_->reset()) {
            encode_errors_.fetch_add(
                1, std::memory_order_relaxed);
            return *error;
        }
        next_sequence_ =
            static_cast<std::uint16_t>(
                static_cast<std::uint64_t>(
                    next_sequence_) +
                skipped_frames);
        next_timestamp_ =
            static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(
                    next_timestamp_) +
                skipped_frames *
                    static_cast<std::uint64_t>(
                        kFrameSamples));
    }

    if (!capture_.try_pop(capture_frame_)) {
        return EncodeStep::no_frame;
    }

    if (muted_.load(std::memory_order_acquire)) {
        // Preserve media clock/cadence while guaranteeing microphone samples are not encoded.
        std::fill(
            capture_frame_.begin(),
            capture_frame_.end(),
            0.0F);
        muted_frames_.fetch_add(
            1, std::memory_order_relaxed);
    } else if (processor_) {
        processor_->process_capture(capture_frame_);
    }
    const auto level = mean_square(capture_frame_);
    const auto threshold = gate_mean_square_ > 0.0F ? gate_mean_square_ : kSpeakingMeanSquare;
    const bool speaking = track_speaking(local_speaking_hangover_, level, threshold);
    if (gate_mean_square_ > 0.0F && !speaking) {
        // Clean silence between words: no fan or keyboard noise, and Opus spends almost no bits.
        std::fill(capture_frame_.begin(), capture_frame_.end(), 0.0F);
        gated_frames_.fetch_add(1, std::memory_order_relaxed);
    }
    if (controls_ != nullptr) {
        controls_->set_local_speaking(speaking);
        controls_->set_local_level(level > 1e-10F ? 10.0F * std::log10(level) : kSilenceDbfs);
    }

    auto output =
        std::span<std::byte>(datagram.bytes);
    auto payload =
        output.subspan(kVoiceHeaderBytes);
    const auto encoded =
        encoder_->encode(capture_frame_, payload);
    if (const auto* error =
            std::get_if<CodecError>(&encoded)) {
        encode_errors_.fetch_add(
            1, std::memory_order_relaxed);
        return *error;
    }

    const auto payload_size =
        std::get<std::size_t>(encoded);
    const auto header =
        write_packet_header(
            stream_id_,
            next_sequence_,
            next_timestamp_,
            payload_size,
            output);
    if (const auto* error =
            std::get_if<PacketError>(&header)) {
        encode_errors_.fetch_add(
            1, std::memory_order_relaxed);
        return CodecError{
            CodecErrorCode::codec_failure,
            -static_cast<int>(*error) - 1};
    }

    datagram.size =
        std::get<std::size_t>(header);
    encoded_frames_.fetch_add(
        1, std::memory_order_relaxed);
    outbound_bytes_.fetch_add(
        datagram.size, std::memory_order_relaxed);
    ++next_sequence_;
    next_timestamp_ += kFrameSamples;
    return EncodeStep::packet_ready;
}

ReceiveResult VoicePipeline::receive(
    std::span<const std::byte> datagram) noexcept {
    received_datagrams_.fetch_add(
        1, std::memory_order_relaxed);

    const auto parsed = parse_packet(datagram);
    if (const auto* error =
            std::get_if<PacketError>(&parsed)) {
        malformed_datagrams_.fetch_add(
            1, std::memory_order_relaxed);
        return *error;
    }

    const auto packet =
        std::get<VoicePacketView>(parsed);
    if (packet.stream_id == stream_id_) {
        // An SFU may be configured to echo every room track. Never render our own microphone.
        return JitterPushResult::self_stream;
    }

    auto* remote =
        find_remote(packet.stream_id);
    if (remote == nullptr) {
        auto empty =
            std::find_if(
                remotes_.begin(),
                remotes_.end(),
                [](const auto& candidate) {
                    return !candidate;
                });
        if (empty == remotes_.end()) {
            remote_stream_capacity_drops_.fetch_add(
                1, std::memory_order_relaxed);
            return JitterPushResult::remote_capacity;
        }

        auto decoder_result = Decoder::create();
        if (const auto* error =
                std::get_if<CodecError>(
                    &decoder_result)) {
            return *error;
        }

        try {
            *empty = std::make_unique<RemoteStream>(
                packet.stream_id,
                jitter_target_packets_,
                std::move(
                    std::get<
                        std::unique_ptr<Decoder>>(
                        decoder_result)));
        } catch (const std::bad_alloc&) {
            return CodecError{
                CodecErrorCode::allocation_failed};
        }
        remote = empty->get();
    }

    remote->last_packet_tick = playout_tick_;
    return remote->jitter.push(packet);
}

PlayoutKind VoicePipeline::next_playout_kind()
    const noexcept {
    bool fec = false;
    bool plc = false;
    for (const auto& remote : remotes_) {
        if (!remote) {
            continue;
        }
        switch (remote->jitter.peek()) {
        case PlayoutKind::packet:
            return PlayoutKind::packet;
        case PlayoutKind::fec:
            fec = true;
            break;
        case PlayoutKind::plc:
            plc = true;
            break;
        case PlayoutKind::waiting:
            break;
        }
    }
    if (fec) {
        return PlayoutKind::fec;
    }
    if (plc) {
        return PlayoutKind::plc;
    }
    return PlayoutKind::waiting;
}

std::optional<CodecError>
VoicePipeline::resynchronize_receiver() noexcept {
    limiter_gain_ = 1.0F;
    for (auto& remote : remotes_) {
        if (!remote) {
            continue;
        }
        remote->jitter.resynchronize();
        remote->last_packet_tick = playout_tick_;
        std::fill(
            remote->decoded.begin(),
            remote->decoded.end(),
            0.0F);
        if (const auto failure =
                remote->decoder->reset()) {
            return failure;
        }
    }
    return std::nullopt;
}

std::variant<DecodeStep, CodecError>
VoicePipeline::decode_next() noexcept {
    // Remove tracks that have not delivered media for two seconds. Without this, a departed
    // speaker's jitter buffer would manufacture PLC forever and permanently occupy a room slot.
    for (auto& remote : remotes_) {
        if (!remote) {
            continue;
        }
        if (playout_tick_ >
                remote->last_packet_tick +
                    kRemoteIdlePlayoutTicks) {
            if (controls_ != nullptr) {
                controls_->set_speaking(remote->stream_id, false);
            }
            remote.reset();
        }
    }

    if (next_playout_kind() ==
        PlayoutKind::waiting) {
        return DecodeStep::waiting;
    }

    std::fill(
        mix_frame_.begin(),
        mix_frame_.end(),
        0.0F);

    bool decoded_any = false;
    bool saw_packet = false;
    bool saw_fec = false;
    bool saw_plc = false;

    for (auto& remote : remotes_) {
        if (!remote) {
            continue;
        }

        PlayoutFrame frame;
        const auto kind =
            remote->jitter.pull(frame);
        if (kind == PlayoutKind::waiting) {
            continue;
        }

        std::variant<std::size_t, CodecError>
            decoded{
                CodecError{
                    CodecErrorCode::codec_failure}};

        switch (kind) {
        case PlayoutKind::packet:
            decoded =
                remote->decoder->decode(
                    frame.payload_view(),
                    remote->decoded);
            saw_packet = true;
            break;
        case PlayoutKind::fec:
            decoded =
                remote->decoder->decode(
                    frame.payload_view(),
                    remote->decoded,
                    true);
            saw_fec = true;
            break;
        case PlayoutKind::plc:
            decoded =
                remote->decoder->conceal(
                    remote->decoded);
            saw_plc = true;
            break;
        case PlayoutKind::waiting:
            break;
        }

        const auto recover_with_plc =
            [&]() -> std::optional<CodecError> {
            const auto concealed =
                remote->decoder->conceal(
                    remote->decoded);
            if (const auto* error =
                    std::get_if<CodecError>(
                        &concealed)) {
                return *error;
            }
            if (std::get<std::size_t>(
                    concealed) != kFrameSamples) {
                return CodecError{
                    CodecErrorCode::codec_failure};
            }
            saw_plc = true;
            return std::nullopt;
        };

        if (const auto* error =
                std::get_if<CodecError>(&decoded)) {
            decode_errors_.fetch_add(
                1, std::memory_order_relaxed);
            if (kind == PlayoutKind::plc) {
                return *error;
            }
            if (const auto conceal_error =
                    recover_with_plc()) {
                return *conceal_error;
            }
        } else if (
            std::get<std::size_t>(decoded) !=
            kFrameSamples) {
            decode_errors_.fetch_add(
                1, std::memory_order_relaxed);
            if (kind == PlayoutKind::plc) {
                return CodecError{
                    CodecErrorCode::codec_failure};
            }
            if (const auto conceal_error =
                    recover_with_plc()) {
                return *conceal_error;
            }
        }

        decoded_frames_.fetch_add(
            1, std::memory_order_relaxed);
        decoded_any = true;

        auto volume = 1.0F;
        if (controls_ != nullptr) {
            // Speaking follows the sender's level, not the local volume, like Discord.
            controls_->set_speaking(
                remote->stream_id,
                track_speaking(remote->speaking_hangover, mean_square(remote->decoded), kSpeakingMeanSquare));
            volume = controls_->volume(remote->stream_id);
        }
        for (std::size_t sample = 0;
             sample < mix_frame_.size();
             ++sample) {
            const auto value =
                remote->decoded[sample];
            if (std::isfinite(value)) {
                mix_frame_[sample] += value * volume;
            }
        }
    }

    if (!decoded_any) {
        return DecodeStep::waiting;
    }

    float peak = 0.0F;
    for (const auto sample : mix_frame_) {
        peak = std::max(
            peak, std::abs(sample));
    }

    const auto target_gain =
        peak > kLimiterCeiling
            ? kLimiterCeiling / peak
            : 1.0F;
    if (target_gain < limiter_gain_) {
        // Instant attack avoids clipping.
        limiter_gain_ = target_gain;
    } else {
        // Slow bounded release avoids pumping between overlapping speakers.
        limiter_gain_ = std::min(
            1.0F,
            limiter_gain_ +
                kLimiterReleasePerFrame);
    }

    if (limiter_gain_ < 0.999F) {
        limiter_frames_.fetch_add(
            1, std::memory_order_relaxed);
    }
    for (auto& sample : mix_frame_) {
        sample = std::clamp(
            sample * limiter_gain_,
            -1.0F,
            1.0F);
    }

    mixed_frames_.fetch_add(
        1, std::memory_order_relaxed);
    ++playout_tick_;

    if (!render_.try_push(mix_frame_)) {
        render_queue_full_.fetch_add(
            1, std::memory_order_relaxed);
        // Preserve live conversational latency: the render callback owns the consumer index and
        // flushes stale backlog on its next native audio period.
        render_.request_resync();
        return DecodeStep::render_queue_full;
    }
    if (processor_ && !render_.deafened()) {
        // The echo canceller's reference is exactly what the speakers will play: the voice mix
        // plus any watched or shared stream audio.
        // ponytail: stream audio only reaches the reference while a voice peer keeps playout
        // ticking (peers send silence frames when quiet or muted, so in a call it always does).
        reference_frame_ = mix_frame_;
        if (controls_ != nullptr) {
            controls_->mix_echo_reference(reference_frame_);
        }
        processor_->analyze_render(reference_frame_);
    }

    if (saw_packet) {
        return DecodeStep::queued_packet;
    }
    if (saw_fec) {
        return DecodeStep::queued_fec;
    }
    if (saw_plc) {
        return DecodeStep::queued_plc;
    }
    return DecodeStep::waiting;
}

JitterStatistics
VoicePipeline::aggregate_jitter_statistics()
    const noexcept {
    JitterStatistics total;
    for (const auto& remote : remotes_) {
        if (remote) {
            accumulate_jitter(
                total,
                remote->jitter.statistics());
        }
    }
    return total;
}

VoicePipelineStatistics
VoicePipeline::statistics() const noexcept {
    std::size_t active = 0;
    for (const auto& remote : remotes_) {
        active += remote ? 1U : 0U;
    }

    return {
        .encoded_frames =
            encoded_frames_.load(
                std::memory_order_relaxed),
        .encode_errors =
            encode_errors_.load(
                std::memory_order_relaxed),
        .outbound_bytes =
            outbound_bytes_.load(
                std::memory_order_relaxed),
        .muted_frames =
            muted_frames_.load(
                std::memory_order_relaxed),
        .gated_frames =
            gated_frames_.load(
                std::memory_order_relaxed),
        .received_datagrams =
            received_datagrams_.load(
                std::memory_order_relaxed),
        .malformed_datagrams =
            malformed_datagrams_.load(
                std::memory_order_relaxed),
        .decoded_frames =
            decoded_frames_.load(
                std::memory_order_relaxed),
        .decode_errors =
            decode_errors_.load(
                std::memory_order_relaxed),
        .render_queue_full =
            render_queue_full_.load(
                std::memory_order_relaxed),
        .remote_stream_capacity_drops =
            remote_stream_capacity_drops_.load(
                std::memory_order_relaxed),
        .mixed_frames =
            mixed_frames_.load(
                std::memory_order_relaxed),
        .limiter_frames =
            limiter_frames_.load(
                std::memory_order_relaxed),
        .remote_streams_active = active,
        .capture = capture_.statistics(),
        .render = render_.statistics(),
        .jitter =
            aggregate_jitter_statistics(),
    };
}

} // namespace catro::voice

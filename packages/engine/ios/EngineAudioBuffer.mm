#import "EngineAudioBuffer.h"

#include "AudioInputBuffer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

@interface EngineAudioBuffer ()
{
	std::unique_ptr<engine::AudioInputBuffer> _buffer;
	std::vector<float> _scratch;
	std::atomic<uint64_t> _unsupportedBuffers;
	AVAudioCommonFormat _format;
	AVAudioChannelCount _channels;
	BOOL _interleaved;
	NSString *_formatSummary;
}
@end

@implementation EngineAudioBuffer

- (instancetype)initWithCapacity:(NSUInteger)capacity targetSampleRate:(double)targetSampleRate {
	self = [super init];
	if (self) {
		_buffer = std::make_unique<engine::AudioInputBuffer>(capacity, targetSampleRate);
		_scratch.resize(16384, 0.0f);
		_unsupportedBuffers.store(0, std::memory_order_release);
		_format = AVAudioPCMFormatFloat32;
		_channels = 1;
		_interleaved = NO;
		_formatSummary = @"unconfigured";
	}
	return self;
}

- (void)configureWithFormat:(AVAudioFormat *)format {
	_format = format.commonFormat;
	_channels = std::max<AVAudioChannelCount>(format.channelCount, 1);
	_interleaved = format.isInterleaved;
	_buffer->configure(format.sampleRate);
	_unsupportedBuffers.store(0, std::memory_order_release);

	const size_t size = std::max<size_t>(16384, static_cast<size_t>(std::ceil(format.sampleRate * 0.25)));
	_scratch.assign(size, 0.0f);

	NSString *kind = @"other";
	if (_format == AVAudioPCMFormatFloat32) {
		kind = @"float32";
	} else if (_format == AVAudioPCMFormatFloat64) {
		kind = @"float64";
	} else if (_format == AVAudioPCMFormatInt16) {
		kind = @"int16";
	} else if (_format == AVAudioPCMFormatInt32) {
		kind = @"int32";
	}

	_formatSummary = [NSString stringWithFormat:@"%@ %@ %.0fHz %uch",
		kind,
		_interleaved ? @"interleaved" : @"planar",
		format.sampleRate,
		_channels
	];
}

- (void)reset {
	_buffer->reset();
	_unsupportedBuffers.store(0, std::memory_order_release);
}

- (NSUInteger)writeAudioBufferList:(const AudioBufferList *)data frameCount:(AVAudioFrameCount)frames {
	if (data == nullptr || frames == 0) {
		return 0;
	}

	const size_t count = std::min<size_t>(static_cast<size_t>(frames), _scratch.size());
	if (count == 0) {
		return 0;
	}

	NSUInteger copied = 0;
	switch (_format) {
	case AVAudioPCMFormatFloat32:
		copied = [self copyFloat32:data frames:count];
		break;
	case AVAudioPCMFormatFloat64:
		copied = [self copyFloat64:data frames:count];
		break;
	case AVAudioPCMFormatInt16:
		copied = [self copyInt16:data frames:count];
		break;
	case AVAudioPCMFormatInt32:
		copied = [self copyInt32:data frames:count];
		break;
	default:
		break;
	}

	if (copied == 0) {
		_unsupportedBuffers.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}

	return static_cast<NSUInteger>(_buffer->writeMono(_scratch.data(), static_cast<int>(copied)));
}

- (NSUInteger)readSamples:(float *)samples capacity:(NSUInteger)capacity {
	return static_cast<NSUInteger>(_buffer->read(samples, static_cast<int>(capacity)));
}

- (NSUInteger)copyFloat32:(const AudioBufferList *)data frames:(size_t)frames {
	if (_interleaved) {
		if (data->mNumberBuffers == 0 || data->mBuffers[0].mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const float *>(data->mBuffers[0].mData);
		for (size_t frame = 0; frame < frames; frame++) {
			float sample = 0.0f;
			for (AVAudioChannelCount channel = 0; channel < _channels; channel++) {
				sample += input[frame * _channels + channel];
			}
			_scratch[frame] = sample / static_cast<float>(_channels);
		}
		return frames;
	}

	const UInt32 count = std::min<UInt32>(data->mNumberBuffers, _channels);
	if (count == 0) {
		return 0;
	}

	std::fill(_scratch.begin(), _scratch.begin() + frames, 0.0f);
	for (UInt32 channel = 0; channel < count; channel++) {
		const AudioBuffer *buffer = &data->mBuffers[channel];
		if (buffer->mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const float *>(buffer->mData);
		for (size_t frame = 0; frame < frames; frame++) {
			_scratch[frame] += input[frame] / static_cast<float>(count);
		}
	}
	return frames;
}

- (NSUInteger)copyFloat64:(const AudioBufferList *)data frames:(size_t)frames {
	if (_interleaved) {
		if (data->mNumberBuffers == 0 || data->mBuffers[0].mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const double *>(data->mBuffers[0].mData);
		for (size_t frame = 0; frame < frames; frame++) {
			double sample = 0.0;
			for (AVAudioChannelCount channel = 0; channel < _channels; channel++) {
				sample += input[frame * _channels + channel];
			}
			_scratch[frame] = static_cast<float>(sample / static_cast<double>(_channels));
		}
		return frames;
	}

	const UInt32 count = std::min<UInt32>(data->mNumberBuffers, _channels);
	if (count == 0) {
		return 0;
	}

	std::fill(_scratch.begin(), _scratch.begin() + frames, 0.0f);
	for (UInt32 channel = 0; channel < count; channel++) {
		const AudioBuffer *buffer = &data->mBuffers[channel];
		if (buffer->mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const double *>(buffer->mData);
		for (size_t frame = 0; frame < frames; frame++) {
			_scratch[frame] += static_cast<float>(input[frame] / static_cast<double>(count));
		}
	}
	return frames;
}

- (NSUInteger)copyInt16:(const AudioBufferList *)data frames:(size_t)frames {
	if (_interleaved) {
		if (data->mNumberBuffers == 0 || data->mBuffers[0].mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const int16_t *>(data->mBuffers[0].mData);
		for (size_t frame = 0; frame < frames; frame++) {
			float sample = 0.0f;
			for (AVAudioChannelCount channel = 0; channel < _channels; channel++) {
				sample += static_cast<float>(input[frame * _channels + channel]) / static_cast<float>(INT16_MAX);
			}
			_scratch[frame] = sample / static_cast<float>(_channels);
		}
		return frames;
	}

	const UInt32 count = std::min<UInt32>(data->mNumberBuffers, _channels);
	if (count == 0) {
		return 0;
	}

	std::fill(_scratch.begin(), _scratch.begin() + frames, 0.0f);
	for (UInt32 channel = 0; channel < count; channel++) {
		const AudioBuffer *buffer = &data->mBuffers[channel];
		if (buffer->mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const int16_t *>(buffer->mData);
		for (size_t frame = 0; frame < frames; frame++) {
			_scratch[frame] += static_cast<float>(input[frame]) / static_cast<float>(INT16_MAX) / static_cast<float>(count);
		}
	}
	return frames;
}

- (NSUInteger)copyInt32:(const AudioBufferList *)data frames:(size_t)frames {
	if (_interleaved) {
		if (data->mNumberBuffers == 0 || data->mBuffers[0].mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const int32_t *>(data->mBuffers[0].mData);
		for (size_t frame = 0; frame < frames; frame++) {
			float sample = 0.0f;
			for (AVAudioChannelCount channel = 0; channel < _channels; channel++) {
				sample += static_cast<float>(input[frame * _channels + channel]) / static_cast<float>(INT32_MAX);
			}
			_scratch[frame] = sample / static_cast<float>(_channels);
		}
		return frames;
	}

	const UInt32 count = std::min<UInt32>(data->mNumberBuffers, _channels);
	if (count == 0) {
		return 0;
	}

	std::fill(_scratch.begin(), _scratch.begin() + frames, 0.0f);
	for (UInt32 channel = 0; channel < count; channel++) {
		const AudioBuffer *buffer = &data->mBuffers[channel];
		if (buffer->mData == nullptr) {
			return 0;
		}
		const auto *input = static_cast<const int32_t *>(buffer->mData);
		for (size_t frame = 0; frame < frames; frame++) {
			_scratch[frame] += static_cast<float>(input[frame]) / static_cast<float>(INT32_MAX) / static_cast<float>(count);
		}
	}
	return frames;
}

- (engine::AudioInputStats)stats {
	return _buffer->stats();
}

- (NSUInteger)writes {
	return static_cast<NSUInteger>(self.stats.writes);
}

- (NSUInteger)reads {
	return static_cast<NSUInteger>(self.stats.reads);
}

- (NSUInteger)writtenFrames {
	return static_cast<NSUInteger>(self.stats.writtenFrames);
}

- (NSUInteger)outputFrames {
	return static_cast<NSUInteger>(self.stats.outputFrames);
}

- (NSUInteger)droppedFrames {
	return static_cast<NSUInteger>(self.stats.droppedFrames);
}

- (NSUInteger)unsupportedBuffers {
	return static_cast<NSUInteger>(_unsupportedBuffers.load(std::memory_order_acquire));
}

- (NSUInteger)resampleReads {
	return static_cast<NSUInteger>(self.stats.resampleReads);
}

- (NSUInteger)queuedFrames {
	return static_cast<NSUInteger>(self.stats.queuedFrames);
}

- (NSUInteger)lastInputFrames {
	return static_cast<NSUInteger>(self.stats.lastInputFrames);
}

- (double)sourceSampleRate {
	return self.stats.sourceRate;
}

- (double)targetSampleRate {
	return self.stats.targetRate;
}

- (float)peak {
	return self.stats.peak;
}

- (float)rms {
	return self.stats.rms;
}

- (NSString *)formatSummary {
	return _formatSummary;
}

@end

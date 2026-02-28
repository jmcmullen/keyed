#import "EngineBridge.h"
#include "Engine.hpp"
#include <vector>
#include <stdexcept>

@implementation EngineFrameResult
@end

@implementation EngineKeyResult
@end

@interface EngineBridge ()
{
	engine::Engine* _engine;
	std::vector<engine::Engine::FrameResult> _resultBuffer;
	BOOL _engineInitialized;
	BOOL _engineInitFailed;
}
@end

@implementation EngineBridge

+ (instancetype)shared {
	static EngineBridge *instance = nil;
	static dispatch_once_t onceToken;
	dispatch_once(&onceToken, ^{
		instance = [[EngineBridge alloc] init];
	});
	return instance;
}

- (instancetype)init {
	self = [super init];
	if (self) {
		_engine = nullptr;
		_engineInitialized = NO;
		_engineInitFailed = NO;
	}
	return self;
}

/// Lazily initialize the engine - returns YES if engine is ready
- (BOOL)ensureEngineInitialized {
	if (_engineInitFailed) {
		return NO;
	}
	if (_engineInitialized && _engine != nullptr) {
		return YES;
	}

	// Use C++ try/catch for C++ exceptions
	try {
		_engine = new engine::Engine();
		_resultBuffer.resize(200);
		_engineInitialized = YES;
		NSLog(@"[EngineBridge] Engine initialized successfully");
		return YES;
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] Engine initialization failed with C++ exception: %s", e.what());
		_engineInitFailed = YES;
		_engine = nullptr;
		return NO;
	} catch (...) {
		NSLog(@"[EngineBridge] Engine initialization failed with unknown C++ exception");
		_engineInitFailed = YES;
		_engine = nullptr;
		return NO;
	}
}

- (void)dealloc {
	if (_engine) {
		delete _engine;
		_engine = nullptr;
	}
}

- (void)reset {
	try {
		if (_engine) {
			_engine->reset();
		}
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] reset failed with C++ exception: %s", e.what());
	} catch (...) {
		NSLog(@"[EngineBridge] reset failed with unknown exception");
	}
}

// =============================================================================
// BPM Detection (BeatNet)
// =============================================================================

- (BOOL)loadModel:(NSString *)modelPath {
		try {
			if (![self ensureEngineInitialized]) {
				NSLog(@"[EngineBridge] loadModel: engine not initialized");
				return NO;
			}
			if (modelPath == nil) {
				NSLog(@"[EngineBridge] loadModel: modelPath is nil");
				return NO;
			}
			NSLog(@"[EngineBridge] loadModel: loading from %@", modelPath);
			BOOL result = _engine->loadModel(std::string([modelPath UTF8String]));
		NSLog(@"[EngineBridge] loadModel: result = %@", result ? @"YES" : @"NO");
		return result;
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] loadModel failed with C++ exception: %s", e.what());
		return NO;
	} catch (...) {
		NSLog(@"[EngineBridge] loadModel failed with unknown exception");
		return NO;
	}
}

- (BOOL)isReady {
	try {
		return _engine && _engine->isReady();
	} catch (...) {
		return NO;
	}
}

- (BOOL)warmUp {
	try {
		return _engine && _engine->warmUp();
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] warmUp failed with C++ exception: %s", e.what());
		return NO;
	} catch (...) {
		NSLog(@"[EngineBridge] warmUp failed with unknown exception");
		return NO;
	}
}

- (float)getBpm {
	try {
		return _engine ? _engine->getBpm() : 0.0f;
	} catch (...) {
		return 0.0f;
	}
}

- (NSUInteger)getFrameCount {
	try {
		return _engine ? static_cast<NSUInteger>(_engine->getFrameCount()) : 0;
	} catch (...) {
		return 0;
	}
}

// =============================================================================
// Key Detection (MusicalKeyCNN)
// =============================================================================

- (BOOL)loadKeyModel:(NSString *)modelPath {
		try {
			if (![self ensureEngineInitialized]) {
				NSLog(@"[EngineBridge] loadKeyModel: engine not initialized");
				return NO;
			}
			if (modelPath == nil) {
				NSLog(@"[EngineBridge] loadKeyModel: modelPath is nil");
				return NO;
			}
			NSLog(@"[EngineBridge] loadKeyModel: loading from %@", modelPath);
			BOOL result = _engine->loadKeyModel(std::string([modelPath UTF8String]));
		NSLog(@"[EngineBridge] loadKeyModel: result = %@", result ? @"YES" : @"NO");
		return result;
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] loadKeyModel failed with C++ exception: %s", e.what());
		return NO;
	} catch (...) {
		NSLog(@"[EngineBridge] loadKeyModel failed with unknown exception");
		return NO;
	}
}

- (BOOL)isKeyReady {
	try {
		return _engine && _engine->isKeyReady();
	} catch (...) {
		return NO;
	}
}

- (BOOL)warmUpKey {
	try {
		return _engine && _engine->warmUpKey();
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] warmUpKey failed with C++ exception: %s", e.what());
		return NO;
	} catch (...) {
		NSLog(@"[EngineBridge] warmUpKey failed with unknown exception");
		return NO;
	}
}

- (EngineKeyResult *)getKey {
	EngineKeyResult *result = [[EngineKeyResult alloc] init];
	result.camelot = @"";
	result.notation = @"";
	result.confidence = 0.0f;
	result.valid = NO;

	try {
		if (_engine) {
			engine::Engine::KeyResult cppResult = _engine->getKey();
			result.camelot = [NSString stringWithUTF8String:cppResult.camelot.c_str()];
			result.notation = [NSString stringWithUTF8String:cppResult.notation.c_str()];
			result.confidence = cppResult.confidence;
			result.valid = cppResult.valid;
		}
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] getKey failed with C++ exception: %s", e.what());
	} catch (...) {
		NSLog(@"[EngineBridge] getKey failed with unknown exception");
	}

	return result;
}

- (NSUInteger)getKeyFrameCount {
	try {
		return _engine ? static_cast<NSUInteger>(_engine->getKeyFrameCount()) : 0;
	} catch (...) {
		return 0;
	}
}

// =============================================================================
// Audio Processing
// =============================================================================

- (nullable NSArray<EngineFrameResult *> *)processAudio:(NSArray<NSNumber *> *)samples {
		try {
			if (!_engine || samples.count == 0) {
				return @[];
			}

		// Convert NSArray to float array
		std::vector<float> floatSamples(samples.count);
		for (NSUInteger i = 0; i < samples.count; i++) {
			floatSamples[i] = [samples[i] floatValue];
		}

		// Process audio at 44100 Hz (handles both BPM and key)
		int maxResults = static_cast<int>(_resultBuffer.size());
		int numResults = _engine->processAudio(floatSamples.data(),
		                                       static_cast<int>(floatSamples.size()),
		                                       _resultBuffer.data(),
		                                       maxResults);

			if (numResults == 0) {
				return @[];
			}

		// Convert to NSArray of EngineFrameResult
		NSMutableArray<EngineFrameResult *> *results = [NSMutableArray arrayWithCapacity:numResults];
		for (int i = 0; i < numResults; i++) {
			EngineFrameResult *result = [[EngineFrameResult alloc] init];
			result.beatActivation = _resultBuffer[i].beatActivation;
			result.downbeatActivation = _resultBuffer[i].downbeatActivation;
			[results addObject:result];
		}

		return results;
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] processAudio failed with C++ exception: %s", e.what());
		return nil;
	} catch (...) {
		NSLog(@"[EngineBridge] processAudio failed with unknown exception");
		return nil;
	}
}

- (nullable NSArray<EngineFrameResult *> *)processAudioForBpm:(NSArray<NSNumber *> *)samples {
		try {
			if (!_engine || !_engine->isReady() || samples.count == 0) {
				return @[];
			}

		// Convert NSArray to float array
		std::vector<float> floatSamples(samples.count);
		for (NSUInteger i = 0; i < samples.count; i++) {
			floatSamples[i] = [samples[i] floatValue];
		}

		// Process audio at 22050 Hz (BPM only, no key detection)
		int maxResults = static_cast<int>(_resultBuffer.size());
		int numResults = _engine->processAudioForBpm(floatSamples.data(),
		                                             static_cast<int>(floatSamples.size()),
		                                             _resultBuffer.data(),
		                                             maxResults);

			if (numResults == 0) {
				return @[];
			}

		// Convert to NSArray of EngineFrameResult
		NSMutableArray<EngineFrameResult *> *results = [NSMutableArray arrayWithCapacity:numResults];
		for (int i = 0; i < numResults; i++) {
			EngineFrameResult *result = [[EngineFrameResult alloc] init];
			result.beatActivation = _resultBuffer[i].beatActivation;
			result.downbeatActivation = _resultBuffer[i].downbeatActivation;
			[results addObject:result];
		}

		return results;
	} catch (const std::exception& e) {
		NSLog(@"[EngineBridge] processAudioForBpm failed with C++ exception: %s", e.what());
		return nil;
	} catch (...) {
		NSLog(@"[EngineBridge] processAudioForBpm failed with unknown exception");
		return nil;
	}
}

// =============================================================================
// Class Properties (Constants)
// Note: These are hardcoded to avoid C++ static initialization issues in release builds
// =============================================================================

+ (int)sampleRate {
	return 44100;
}

+ (int)bpmSampleRate {
	return 22050;
}

+ (int)keySampleRate {
	return 44100;
}

@end

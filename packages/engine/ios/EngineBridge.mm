#import "EngineBridge.h"
#include "Engine.hpp"
#include <vector>

@implementation EngineFrameResult
@end

@interface EngineBridge ()
{
    engine::Engine* _engine;
    std::vector<engine::Engine::FrameResult> _resultBuffer;
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
        _engine = new engine::Engine();
        _resultBuffer.resize(100);
    }
    return self;
}

- (void)dealloc {
    delete _engine;
}

- (void)reset {
    if (_engine) {
        _engine->reset();
    }
}

- (BOOL)loadModel:(NSString *)modelPath {
    if (!_engine) {
        return NO;
    }
    return _engine->loadModel(std::string([modelPath UTF8String]));
}

- (BOOL)isReady {
    return _engine && _engine->isReady();
}

- (BOOL)warmUp {
    return _engine && _engine->warmUp();
}

- (nullable NSArray<EngineFrameResult *> *)processAudio:(NSArray<NSNumber *> *)samples {
    if (!_engine || !_engine->isReady() || samples.count == 0) {
        return nil;
    }

    // Convert NSArray to float array
    std::vector<float> floatSamples(samples.count);
    for (NSUInteger i = 0; i < samples.count; i++) {
        floatSamples[i] = [samples[i] floatValue];
    }

    // Process audio
    int maxResults = 100;
    int numResults = _engine->processAudio(floatSamples.data(),
                                           static_cast<int>(floatSamples.size()),
                                           _resultBuffer.data(),
                                           maxResults);

    if (numResults == 0) {
        return nil;
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
}

- (float)getBpm {
    return _engine ? _engine->getBpm() : 0.0f;
}

- (NSUInteger)getFrameCount {
    return _engine ? static_cast<NSUInteger>(_engine->getFrameCount()) : 0;
}

@end

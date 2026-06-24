#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface EngineAudioBuffer : NSObject

- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithCapacity:(NSUInteger)capacity targetSampleRate:(double)targetSampleRate NS_DESIGNATED_INITIALIZER;
- (void)configureWithFormat:(AVAudioFormat *)format;
- (void)reset;
- (NSUInteger)writeAudioBufferList:(const AudioBufferList *)data frameCount:(AVAudioFrameCount)frames;
- (NSUInteger)readSamples:(float *)samples capacity:(NSUInteger)capacity;

@property (nonatomic, readonly) NSUInteger writes;
@property (nonatomic, readonly) NSUInteger reads;
@property (nonatomic, readonly) NSUInteger writtenFrames;
@property (nonatomic, readonly) NSUInteger outputFrames;
@property (nonatomic, readonly) NSUInteger droppedFrames;
@property (nonatomic, readonly) NSUInteger unsupportedBuffers;
@property (nonatomic, readonly) NSUInteger resampleReads;
@property (nonatomic, readonly) NSUInteger queuedFrames;
@property (nonatomic, readonly) NSUInteger lastInputFrames;
@property (nonatomic, readonly) double sourceSampleRate;
@property (nonatomic, readonly) double targetSampleRate;
@property (nonatomic, readonly) float peak;
@property (nonatomic, readonly) float rms;
@property (nonatomic, readonly, copy) NSString *formatSummary;

@end

NS_ASSUME_NONNULL_END

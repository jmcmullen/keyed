#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Result from processing one audio frame
 */
@interface EngineFrameResult : NSObject
@property (nonatomic, assign) float beatActivation;
@property (nonatomic, assign) float downbeatActivation;
@end

/**
 * Objective-C bridge to the C++ audio processing engine
 */
@interface EngineBridge : NSObject

+ (instancetype)shared;

- (void)reset;

/**
 * Load ONNX model
 * @param modelPath Path to .onnx model file
 * @return YES if loaded successfully
 */
- (BOOL)loadModel:(NSString *)modelPath;

/**
 * Check if model is loaded and ready
 */
- (BOOL)isReady;

/**
 * Warm-up inference to pre-compile CoreML model
 */
- (BOOL)warmUp;

/**
 * Process audio samples
 * @param samples Audio samples at 22050Hz
 * @return Array of EngineFrameResult, or nil if no results
 */
- (nullable NSArray<EngineFrameResult *> *)processAudio:(NSArray<NSNumber *> *)samples;

/**
 * Get detected BPM (0 if not enough data yet)
 */
- (float)getBpm;

/**
 * Get number of frames processed
 */
- (NSUInteger)getFrameCount;

@end

NS_ASSUME_NONNULL_END

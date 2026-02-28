#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Result from processing one audio frame (BPM detection)
 */
@interface EngineFrameResult : NSObject
@property (nonatomic, assign) float beatActivation;
@property (nonatomic, assign) float downbeatActivation;
@end

/**
 * Result from key detection
 */
@interface EngineKeyResult : NSObject
@property (nonatomic, copy) NSString *camelot;     // Camelot notation: "1A" - "12B"
@property (nonatomic, copy) NSString *notation;    // Musical notation: "Am", "C", etc.
@property (nonatomic, assign) float confidence;    // 0-1, softmax probability
@property (nonatomic, assign) BOOL valid;          // YES if key has been detected
@end

/**
 * Objective-C bridge to the C++ audio processing engine
 *
 * Supports both BPM detection (BeatNet) and key detection (MusicalKeyCNN).
 * Audio is processed at 44100 Hz (native sample rate).
 */
@interface EngineBridge : NSObject

+ (instancetype)shared;

- (void)reset;

// =========================================================================
// BPM Detection (BeatNet)
// =========================================================================

/**
 * Load BeatNet ONNX model
 * @param modelPath Path to beatnet.onnx model file
 * @return YES if loaded successfully
 */
- (BOOL)loadModel:(NSString *)modelPath;

/**
 * Check if BeatNet model is loaded and ready
 */
- (BOOL)isReady;

/**
 * Warm-up BeatNet inference to pre-compile CoreML model
 */
- (BOOL)warmUp;

/**
 * Get detected BPM (0 if not enough data yet)
 */
- (float)getBpm;

/**
 * Get number of BPM frames processed
 */
- (NSUInteger)getFrameCount;

// =========================================================================
// Key Detection (MusicalKeyCNN)
// =========================================================================

/**
 * Load MusicalKeyCNN ONNX model
 * @param modelPath Path to keynet.onnx model file
 * @return YES if loaded successfully
 */
- (BOOL)loadKeyModel:(NSString *)modelPath;

/**
 * Check if MusicalKeyCNN model is loaded and ready
 */
- (BOOL)isKeyReady;

/**
 * Warm-up MusicalKeyCNN inference
 */
- (BOOL)warmUpKey;

/**
 * Get detected key (invalid if not enough data yet)
 */
- (EngineKeyResult *)getKey;

/**
 * Get number of CQT frames accumulated
 */
- (NSUInteger)getKeyFrameCount;

// =========================================================================
// Audio Processing
// =========================================================================

/**
 * Process audio samples at 44100 Hz (native sample rate)
 * Handles both BPM detection and key detection
 * @param samples Audio samples at 44100Hz
 * @return Array of EngineFrameResult (BPM), or nil if no results
 */
- (nullable NSArray<EngineFrameResult *> *)processAudio:(NSArray<NSNumber *> *)samples;

/**
 * Process audio samples at 22050 Hz for BPM only (legacy compatibility)
 * Does NOT process key detection
 * @param samples Audio samples at 22050Hz
 * @return Array of EngineFrameResult, or nil if no results
 */
- (nullable NSArray<EngineFrameResult *> *)processAudioForBpm:(NSArray<NSNumber *> *)samples;

// Constants
@property (class, nonatomic, readonly) int sampleRate;       // 44100 Hz
@property (class, nonatomic, readonly) int bpmSampleRate;    // 22050 Hz
@property (class, nonatomic, readonly) int keySampleRate;    // 44100 Hz

@end

NS_ASSUME_NONNULL_END

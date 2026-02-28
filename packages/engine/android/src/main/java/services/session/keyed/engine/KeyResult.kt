package services.session.keyed.engine

/**
 * Key detection result from MusicalKeyCNN model
 */
data class KeyResult(
	val camelot: String,     // Camelot notation: "1A" - "12B"
	val notation: String,    // Musical notation: "Am", "C", etc.
	val confidence: Float,   // 0-1, softmax probability
	val valid: Boolean       // true if key has been detected
)

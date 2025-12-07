const { getDefaultConfig } = require("expo/metro-config");

const config = getDefaultConfig(__dirname);

config.resolver.sourceExts.push("sql");

// Add ONNX model support for BeatNet
config.resolver.assetExts.push("onnx");

module.exports = config;

const path = require("node:path");
const { getDefaultConfig } = require("expo/metro-config");
const createReporter = require("./metro-reporter");

const config = getDefaultConfig(__dirname);
const root = path.resolve(__dirname, "../..");

config.resolver.sourceExts.push("sql");

// Add ONNX model support for BeatNet
config.resolver.assetExts.push("onnx");

config.resolver.disableHierarchicalLookup = true;
config.resolver.nodeModulesPaths = [path.resolve(root, "node_modules")];

config.reporter = createReporter(config.reporter);

module.exports = config;

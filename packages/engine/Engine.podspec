require 'json'

package = JSON.parse(File.read(File.join(__dir__, 'package.json')))

Pod::Spec.new do |s|
  s.name           = 'Engine'
  s.version        = package['version']
  s.summary        = package['description']
  s.description    = package['description']
  s.license        = package['license']
  s.author         = package['author']
  s.homepage       = package['homepage']
  s.platforms      = {
    :ios => '15.1',
    :tvos => '15.1'
  }
  s.swift_version  = '5.9'
  s.source         = { git: 'https://github.com/jmcmullen/keyed' }
  s.static_framework = true

  s.dependency 'ExpoModulesCore'
  s.dependency 'onnxruntime-c', '~> 1.21.0'

  # Source files - iOS native files and shared C++ sources
  s.source_files = [
    'ios/**/*.{h,m,mm,swift}',
    'cpp/**/*.{h,hpp,cpp}',
  ]

  s.public_header_files = 'ios/EngineBridge.h'

  # Bundle ONNX models in a named resource bundle for reliable access in frameworks
  s.resource_bundles = {
    'EngineResources' => ['models/*.onnx']
  }

  s.pod_target_xcconfig = {
    'DEFINES_MODULE' => 'YES',
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'GCC_ENABLE_CPP_EXCEPTIONS' => 'YES',
    'GCC_ENABLE_OBJC_EXCEPTIONS' => 'YES',
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/cpp" "$(PODS_ROOT)/onnxruntime-c/onnxruntime.xcframework/ios-arm64/onnxruntime.framework/Headers" "$(PODS_ROOT)/onnxruntime-c/onnxruntime.xcframework/ios-arm64_x86_64-simulator/onnxruntime.framework/Headers"',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) ONNX_ENABLED=1 ONNX_ENABLE_COREML=1',
  }

  # CoreML framework required for Neural Engine acceleration
  s.frameworks = ['CoreML']
end

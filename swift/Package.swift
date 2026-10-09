// swift-tools-version: 6.4

import PackageDescription

let strict: [SwiftSetting] = [
  .treatAllWarnings(as: .error),
  .enableUpcomingFeature("ExistentialAny"),
  .enableUpcomingFeature("InternalImportsByDefault"),
  .enableUpcomingFeature("MemberImportVisibility"),
]

let package = Package(
  name: "Pxl",
  platforms: [.macOS(.v27)],
  products: [
    .library(name: "Pxl", targets: ["Pxl"])
  ],
  targets: [
    .systemLibrary(
      name: "CSDL3",
      pkgConfig: "sdl3",
      providers: [.brew(["sdl3"]), .apt(["libsdl3-dev"])]
    ),
    .target(name: "Pxl", dependencies: ["CSDL3"], swiftSettings: strict),
    .executableTarget(name: "hello", dependencies: ["Pxl", "CSDL3"], swiftSettings: strict),
    .executableTarget(name: "pxl-bench", dependencies: ["Pxl", "CSDL3"], swiftSettings: strict),
    .testTarget(name: "PxlTests", dependencies: ["Pxl", "CSDL3"], swiftSettings: strict),
  ]
)

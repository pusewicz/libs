// The GPU tests draw into an offscreen canvas and read the pixels back. They
// need a GPU device; without one they are skipped. Set PXL_TEST_REQUIRE_GPU to
// make them fail instead. SDL needs the main thread on macOS, so the tests run
// on the main actor.

import CSDL3
import Testing

@testable import Pxl

let canvasSize = 16
let red = Color(r: 255, g: 0, b: 0)
let green = Color(r: 0, g: 255, b: 0)
let blue = Color(r: 0, g: 0, b: 255)

/// The GPU state of the tests: a context with a canvas of `canvasSize`.
@MainActor
final class GPU {
  /// The GPU of the tests, or nil if there is none.
  static let shared: GPU? = GPU()

  /// Why there is no GPU.
  static var failure = ""

  let device: Device
  let pxl: Context

  private init?() {
    guard SDL_Init(SDL_INIT_VIDEO) else {
      Self.failure = "SDL_Init: \(String(cString: SDL_GetError()))"
      return nil
    }
    do {
      device = try Device(debug: true)
      pxl = try Context(device: device, resolution: (canvasSize, canvasSize))
    } catch {
      Self.failure = "\(error)"
      return nil
    }
    print("GPU driver: \(device.driver)")
  }
}

@MainActor @Test func gpuIsAvailable() {
  guard GPU.shared == nil else { return }
  if SDL_getenv("PXL_TEST_REQUIRE_GPU") != nil {
    Issue.record("\(GPU.failure): PXL_TEST_REQUIRE_GPU is set")
  } else {
    print("\(GPU.failure): GPU tests skipped")
  }
}

/// The pixels of a target, rows top to bottom.
struct Pixels {
  let colors: [Color]
  let width: Int

  subscript(x: Int, y: Int) -> Color {
    colors[y * width + x]
  }

  /// Draws the first rows as text: "#" for `color`, "." for `other`, "?" for
  /// all else. Compare it with a mask.
  func mask(_ color: Color, _ other: Color, rows: Int) -> [String] {
    (0..<rows).map { y in
      String((0..<width).map { x in self[x, y] == color ? "#" : self[x, y] == other ? "." : "?" })
    }
  }
}

func near(_ a: Color, _ b: Color, _ tolerance: Int) -> Bool {
  abs(Int(a.r) - Int(b.r)) <= tolerance && abs(Int(a.g) - Int(b.g)) <= tolerance
    && abs(Int(a.b) - Int(b.b)) <= tolerance && abs(Int(a.a) - Int(b.a)) <= tolerance
}

@MainActor
@Suite(.serialized, .enabled { await MainActor.run { GPU.shared != nil } })
struct GPUTests {
  let device = GPU.shared!.device
  let pxl = GPU.shared!.pxl

  /// Ends the frame and reads the canvas.
  func finish() throws -> Pixels {
    try pxl.endFrame()
    return Pixels(colors: try pxl.canvas.read(), width: canvasSize)
  }

  func quadTexture() throws -> Texture {
    try Texture(device: device, width: 2, height: 2, pixels: [red, green, blue, .white])
  }

  @Test func clear() throws {
    pxl.beginFrame()
    pxl.clear(red)
    let pixels = try finish()
    #expect(pixels[0, 0] == red)
    #expect(pixels[canvasSize - 1, canvasSize - 1] == red)
  }

  @Test func coordinates() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawPixel(at: [0, 0], color: .white)
    pxl.drawRect(Rect(x: 2, y: 1, width: 3, height: 2), color: .white)
    pxl.drawPixel(at: [15, 15], color: .white)
    let mask = [
      "#...............",
      "..###...........",
      "..###...........",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "...............#",
    ]
    #expect(try finish().mask(.white, .black, rows: mask.count) == mask)
  }

  @Test func spriteOrientation() throws {
    let texture = try quadTexture()
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawSprite(texture, at: [4, 4])
    pxl.drawSprite(texture, at: [4, 8], flipX: true)
    pxl.drawSprite(texture, at: [10, 2], scale: [2, 2])
    pxl.drawSprite(texture, at: [12, 12], rotation: .pi / 2)
    let pixels = try finish()
    #expect(pixels[4, 4] == red)
    #expect(pixels[5, 4] == green)
    #expect(pixels[4, 5] == blue)
    #expect(pixels[5, 5] == .white)
    #expect(pixels[3, 8] == red)
    #expect(pixels[2, 8] == green)
    #expect(pixels[4, 8] == .black)
    #expect(pixels[10, 2] == red)
    #expect(pixels[11, 3] == red)
    #expect(pixels[12, 2] == green)
    #expect(pixels[13, 5] == .white)
    #expect(pixels[11, 12] == red)
    #expect(pixels[11, 13] == green)
    #expect(pixels[10, 12] == blue)
  }

  @Test func spriteOptions() throws {
    let texture = try quadTexture()
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawSprite(texture, at: [1, 1], source: Rect(x: 1, y: 0, width: 1, height: 2))
    pxl.drawSprite(texture, at: [6, 6], origin: [1, 1])
    pxl.drawSprite(texture, at: [10, 10], color: red)
    pxl.drawSprite(texture, at: [13, 1], overlay: blue)
    let pixels = try finish()
    #expect(pixels[1, 1] == green)
    #expect(pixels[1, 2] == .white)
    #expect(pixels[2, 1] == .black)
    #expect(pixels[5, 5] == red)
    #expect(pixels[6, 6] == .white)
    #expect(pixels[11, 10] == .black)
    #expect(pixels[11, 11] == red)
    #expect(pixels[13, 1] == blue)
    #expect(pixels[14, 2] == blue)
  }

  @Test func snap() throws {
    let texture = try quadTexture()
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawSprite(texture, at: [2.4, 2.6])
    pxl.translate(x: 0.5, y: 0.5)
    pxl.drawRect(Rect(x: 8, y: 8, width: 2, height: 2), color: .white)
    let pixels = try finish()
    #expect(pixels[2, 3] == red)
    #expect(pixels[3, 4] == .white)
    #expect(pixels[8, 8] == .black)
    #expect(pixels[9, 9] == .white)
    #expect(pixels[10, 10] == .white)
  }

  @Test func lines() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawLine(from: [0, 0], to: [7, 3], color: .white)
    pxl.drawLine(from: [15, 5], to: [15, 9], color: .white)
    pxl.drawLine(from: [3, 12], to: [0, 15], color: .white)
    pxl.drawLine(from: [-100, 6], to: [1000, 6], color: .white)
    let mask = [
      "##..............",
      "..##............",
      "....##..........",
      "......##........",
      "................",
      "...............#",
      "################",
      "...............#",
      "...............#",
      "...............#",
      "................",
      "................",
      "...#............",
      "..#.............",
      ".#..............",
      "#...............",
    ]
    #expect(try finish().mask(.white, .black, rows: mask.count) == mask)
  }

  @Test func rectLines() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawRectLines(Rect(x: 1, y: 1, width: 5, height: 4), color: .white)
    pxl.drawRectLines(Rect(x: 8, y: 1, width: 2, height: 2), color: .white)
    let mask = [
      "................",
      ".#####..##......",
      ".#...#..##......",
      ".#...#..........",
      ".#####..........",
      "................",
    ]
    #expect(try finish().mask(.white, .black, rows: mask.count) == mask)
  }

  @Test func circles() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawCircle(at: [3, 3], radius: 2, color: .white)
    pxl.drawCircleLines(at: [10, 3], radius: 2, color: .white)
    pxl.drawCircle(at: [3, 10], radius: 0, color: .white)
    pxl.drawCircleLines(at: [10, 11], radius: 3, color: .white)
    pxl.drawCircle(at: [1, 14], radius: -1, color: .white)
    let mask = [
      "................",
      "..###....###....",
      ".#####..#...#...",
      ".#####..#...#...",
      ".#####..#...#...",
      "..###....###....",
      "................",
      "................",
      ".........###....",
      "........#...#...",
      "...#...#.....#..",
      ".......#.....#..",
      ".......#.....#..",
      "........#...#...",
      ".........###....",
      "................",
    ]
    #expect(try finish().mask(.white, .black, rows: mask.count) == mask)
  }

  @Test func triangles() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawTriangle([0, 0], [4, 0], [0, 4], color: .white)
    pxl.drawTriangles([
      Vertex(position: [8, 8], color: red),
      Vertex(position: [16, 8], color: red),
      Vertex(position: [8, 16], color: red),
    ])
    let pixels = try finish()
    #expect(pixels[0, 0] == .white)
    #expect(pixels[1, 1] == .white)
    #expect(pixels[3, 3] == .black)
    #expect(pixels[8, 8] == red)
    #expect(pixels[15, 15] == .black)
  }

  @Test func blend() throws {
    pxl.beginFrame()
    pxl.clear(blue)
    pxl.drawRect(Rect(x: 0, y: 0, width: 4, height: 4), color: Color(r: 255, g: 0, b: 0, a: 128))
    pxl.blend = .add
    pxl.drawRect(Rect(x: 4, y: 0, width: 4, height: 4), color: Color(r: 50, g: 60, b: 0))
    pxl.blend = .multiply
    pxl.drawRect(Rect(x: 8, y: 0, width: 4, height: 4), color: Color(r: 128, g: 0, b: 0))
    pxl.blend = .replace
    pxl.drawRect(Rect(x: 12, y: 0, width: 4, height: 4), color: Color(r: 255, g: 0, b: 0, a: 128))
    let pixels = try finish()
    #expect(near(pixels[0, 0], Color(r: 128, g: 0, b: 127), 1))
    #expect(near(pixels[4, 0], Color(r: 50, g: 60, b: 255), 1))
    #expect(near(pixels[8, 0], Color(r: 0, g: 0, b: 0), 1))
    #expect(near(pixels[12, 0], Color(r: 128, g: 0, b: 0, a: 128), 1))
  }

  @Test func pushPop() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.push()
    pxl.translate(x: 4, y: 4)
    pxl.scale(x: 2, y: 2)
    pxl.drawRect(Rect(x: 0, y: 0, width: 1, height: 1), color: .white)
    pxl.pop()
    pxl.drawPixel(at: [0, 0], color: red)
    let pixels = try finish()
    #expect(pixels[4, 4] == .white)
    #expect(pixels[5, 5] == .white)
    #expect(pixels[6, 6] == .black)
    #expect(pixels[0, 0] == red)
  }

  @Test func clip() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.clip = PixelRect(x: 2, y: 2, width: 3, height: 3)
    pxl.drawRect(Rect(x: 0, y: 0, width: 16, height: 16), color: .white)
    pxl.clip = PixelRect(x: 14, y: 14, width: 10, height: 10)
    pxl.drawRect(Rect(x: 0, y: 0, width: 16, height: 16), color: red)
    pxl.clip = PixelRect(x: 0, y: 0, width: 0, height: 0)
    pxl.drawRect(Rect(x: 0, y: 0, width: 16, height: 16), color: red)
    pxl.clip = nil
    let pixels = try finish()
    #expect(pixels[1, 1] == .black)
    #expect(pixels[2, 2] == .white)
    #expect(pixels[4, 4] == .white)
    #expect(pixels[5, 5] == .black)
    #expect(pixels[14, 14] == red)
    #expect(pixels[13, 13] == .black)
  }

  @Test func renderTarget() throws {
    let target = try Texture(device: device, width: 4, height: 4, renderTarget: true)
    pxl.beginFrame()
    pxl.target = target
    #expect(pxl.width == 4)
    pxl.clear(green)
    pxl.drawPixel(at: [1, 1], color: red)
    pxl.target = nil
    #expect(pxl.width == canvasSize)
    pxl.clear(.black)
    pxl.drawSprite(target, at: [2, 2])
    let pixels = try finish()
    #expect(pixels[1, 1] == .black)
    #expect(pixels[2, 2] == green)
    #expect(pixels[3, 3] == red)
    #expect(pixels[5, 5] == green)
  }

  @Test func nineSlice() throws {
    let pixels = (0..<9).map { $0 == 4 ? blue : red }
    let texture = try Texture(device: device, width: 3, height: 3, pixels: pixels)
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawNineSlice(
      texture, in: Rect(x: 2, y: 2, width: 8, height: 6), left: 1, top: 1, right: 1, bottom: 1)
    let canvas = try finish()
    #expect(canvas[2, 2] == red)
    #expect(canvas[9, 7] == red)
    #expect(canvas[5, 2] == red)
    #expect(canvas[3, 3] == blue)
    #expect(canvas[8, 6] == blue)
    #expect(canvas[10, 8] == .black)
  }

  @Test func updateTexture() throws {
    let texture = try quadTexture()
    try texture.update(x: 1, y: 1, width: 1, height: 1, pixels: [blue])
    #expect(throws: PxlError.self) {
      try texture.update(x: 1, y: 1, width: 2, height: 1, pixels: [blue, blue])
    }
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawSprite(texture, at: [0, 0])
    let pixels = try finish()
    #expect(pixels[0, 0] == red)
    #expect(pixels[1, 1] == blue)
  }

  @Test func batching() throws {
    let texture = try quadTexture()
    let other = try quadTexture()
    pxl.beginFrame()
    pxl.clear(.black)
    for i in 0..<100 {
      pxl.drawSprite(texture, at: [Float(i % 8), Float(i % 5)])
      pxl.drawRect(Rect(x: 0, y: 0, width: 1, height: 1), color: red)
      pxl.drawCircle(at: [8, 8], radius: 2, color: red)
    }
    try pxl.endFrame()
    #expect(pxl.stats.drawCalls == 1)
    #expect(pxl.stats.passes == 1)

    pxl.beginFrame()
    pxl.drawSprite(texture, at: [0, 0])
    pxl.drawSprite(other, at: [0, 0])
    pxl.blend = .add
    pxl.drawSprite(other, at: [0, 0])
    try pxl.endFrame()
    #expect(pxl.stats.drawCalls == 3)
  }

  /// A texture that goes during a frame stays until the frame ends.
  @Test func releaseDuringFrame() throws {
    var texture: Texture? = try quadTexture()
    weak let released = texture
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawSprite(texture!, at: [0, 0])
    texture = nil
    #expect(released != nil)
    let pixels = try finish()
    #expect(released == nil)
    #expect(pixels[0, 0] == red)
  }

  @Test func measureText() {
    #expect(pxl.measureText("Hi") == [8, 10])
    #expect(pxl.measureText("a\nbc") == [11, 20])
    #expect(pxl.measureText("") == [0, 0])
    #expect(pxl.measureText("\(42)").x == 12)
    #expect(pxl.measureText(String(repeating: " ", count: 299) + "a").x == 299 * 4 + 6)
  }

  @Test func drawText() throws {
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.drawText("!", at: [1, 1], color: .white)
    pxl.drawText("\u{E9}", at: [3, 1], color: .white)
    pxl.drawText("T\nT", at: [9, 1], color: .white)
    let mask = [
      "................",
      ".#..###..#####..",
      ".#.#...#...#....",
      ".#.....#...#....",
      ".#....#....#....",
      ".#...#.....#....",
      "...........#....",
      ".#...#.....#....",
      "................",
      "................",
      "................",
      ".........#####..",
      "...........#....",
      "...........#....",
      "...........#....",
      "...........#....",
    ]
    #expect(try finish().mask(.white, .black, rows: mask.count) == mask)
  }

  @Test func customFont() throws {
    let texture = try Texture(
      device: device, width: 4, height: 2,
      pixels: [red, .transparent, green, green, red, .transparent, green, green])
    let font = try Font(
      texture: texture, glyphWidth: 2, glyphHeight: 2, first: "A", advances: [1, 3])
    #expect(throws: PxlError.self) { try Font(texture: texture, glyphWidth: 0, glyphHeight: 0) }
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.font = font
    pxl.drawText("ABAz", at: [0, 0], color: .white)
    #expect(pxl.measureText("AB").x == 4)
    pxl.font = nil
    #expect(pxl.measureText("AB").x == 12)
    let pixels = try finish()
    #expect(pixels[0, 0] == red)
    #expect(pixels[1, 1] == green)
    #expect(pixels[2, 0] == green)
    #expect(pixels[3, 0] == .black)
    #expect(pixels[4, 0] == red)
    #expect(pixels[5, 0] == .black)
    #expect(pixels[6, 0] == .black)
  }

  @Test func customShader() throws {
    #expect(throws: PxlError.self) { try Shader(device: device, ShaderDescriptor()) }
    let shader = try Shader(device: device, slotsFrag)
    let extra = try Texture(device: device, width: 1, height: 1, pixels: [green])
    pxl.beginFrame()
    pxl.clear(.black)
    pxl.shader = shader
    pxl.setShaderTexture(extra, slot: 1)
    pxl.setUniforms(SIMD4<Float>(1, 0, 0, 0))
    pxl.drawRect(Rect(x: 0, y: 0, width: 4, height: 4), color: .white)
    pxl.setUniforms(SIMD4<Float>(0, 0, 1, 0))
    pxl.drawRect(Rect(x: 4, y: 0, width: 4, height: 4), color: .white)
    pxl.withSavedState {
      pxl.shader = nil
      pxl.drawRect(Rect(x: 8, y: 0, width: 4, height: 4), color: red)
    }
    pxl.drawRect(Rect(x: 12, y: 0, width: 4, height: 4), color: .white)
    let pixels = try finish()
    #expect(pixels[0, 0] == Color(r: 255, g: 255, b: 0))
    #expect(pixels[4, 0] == Color(r: 0, g: 255, b: 255))
    #expect(pixels[8, 0] == red)
    #expect(pixels[12, 0] == Color(r: 0, g: 255, b: 255))
    #expect(pxl.stats.drawCalls == 4)
  }

  /// Ends the frame into a screen texture and reads the screen.
  func present(_ screen: Texture) throws -> Pixels {
    let cmd = try #require(SDL_AcquireGPUCommandBuffer(device.handle))
    var ended: PxlError?
    do {
      try pxl.endFrame(
        into: cmd, target: screen.handle, format: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        width: screen.width, height: screen.height)
    } catch {
      ended = error
    }
    #expect(SDL_SubmitGPUCommandBuffer(cmd))
    if let ended {
      throw ended
    }
    return Pixels(colors: try screen.read(), width: screen.width)
  }

  @Test func presentCanvas() throws {
    let screen = try Texture(device: device, width: 40, height: 24, renderTarget: true)

    // Scale 1, in the middle.
    pxl.beginFrame()
    pxl.clear(red)
    pxl.drawPixel(at: [0, 0], color: .white)
    var pixels = try present(screen)
    #expect(pxl.viewport == Rect(x: 12, y: 4, width: 16, height: 16))
    #expect(pixels[12, 4] == .white)
    #expect(pixels[13, 4] == red)
    #expect(pixels[11, 4] == .black)
    #expect(pixels[27, 19] == red)
    #expect(pixels[28, 20] == .black)
    #expect(near(pxl.windowToCanvas([12.5, 4.5]), [0.5, 0.5]))

    // Scale 1.5: sharp filtering blends only the texel edges.
    pxl.scaleMode = .fit
    defer { pxl.scaleMode = .integer }
    pxl.beginFrame()
    pxl.clear(red)
    pxl.drawPixel(at: [0, 0], color: .white)
    pixels = try present(screen)
    #expect(pxl.viewport == Rect(x: 8, y: 0, width: 24, height: 24))
    #expect(pixels[8, 0] == .white)
    #expect(near(pixels[9, 0], Color(r: 255, g: 128, b: 128), 3))
    #expect(pixels[10, 0] == red)
    #expect(pixels[7, 0] == .black)
    #expect(near(pxl.windowToCanvas([20, 12]), [8, 8]))
  }

  @Test func loadTexture() throws {
    #expect(throws: PxlError.self) { try Texture(device: device, path: "no such file.png") }
    let surface = try #require(SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_ARGB8888))
    defer { SDL_DestroySurface(surface) }
    #expect(SDL_WriteSurfacePixel(surface, 0, 0, 255, 0, 0, 255))
    #expect(SDL_WriteSurfacePixel(surface, 1, 0, 0, 0, 255, 128))

    let base = String(cString: SDL_GetBasePath())
    let bmp = base + "pxl_load_test.bmp"
    let png = base + "pxl_load_test.png"
    defer {
      SDL_RemovePath(bmp)
      SDL_RemovePath(png)
    }
    #expect(SDL_SaveBMP(surface, bmp))
    #expect(SDL_SavePNG(surface, png))
    let textures = [
      try Texture(device: device, path: bmp),
      try Texture(device: device, path: png),
      try Texture(device: device, surface: surface),
    ]
    pxl.beginFrame()
    pxl.clear(.black)
    for (i, texture) in textures.enumerated() {
      pxl.drawSprite(texture, at: [0, Float(i * 2)])
    }
    let pixels = try finish()
    for i in textures.indices {
      #expect(textures[i].width == 2)
      #expect(pixels[0, i * 2] == red)
      #expect(near(pixels[1, i * 2], Color(r: 0, g: 0, b: 128), 1))
    }
  }

  @Test func createErrors() {
    #expect(throws: PxlError.self) { try Context(device: device) }
    #expect(throws: PxlError.self) { try Context(device: device, resolution: (8, 0)) }
  }

  @Test func resourceErrors() throws {
    #expect(throws: PxlError.self) { try Texture(device: device, width: 0, height: 0) }
    #expect(throws: PxlError.self) { try Texture(device: device, width: 4, height: -1) }
    let texture = try quadTexture()
    #expect(throws: PxlError.self) {
      try texture.update(x: -1, y: 0, width: 1, height: 1, pixels: [red])
    }
    #expect(throws: PxlError.self) {
      try texture.update(x: 0, y: 0, width: 0, height: 1, pixels: [])
    }
    #expect(throws: PxlError.self) { try Font(texture: texture, glyphWidth: 4, glyphHeight: 4) }
    #expect(throws: PxlError.self) {
      try Font(texture: texture, glyphWidth: 1, glyphHeight: 1, count: 5)
    }
    var tooMany = slotsFrag
    tooMany.textureCount = Shader.maxTextures + 1
    #expect(throws: PxlError.self) { try Shader(device: device, tooMany) }
  }
}

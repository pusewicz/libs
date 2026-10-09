import Testing

@testable import Pxl

@Suite struct CPUTests {
  @Test func colors() {
    #expect(Color(rgb: 0x123456) == Color(r: 0x12, g: 0x34, b: 0x56, a: 255))
    #expect(Color(rgba: 0x1234_5678) == Color(r: 0x12, g: 0x34, b: 0x56, a: 0x78))
    #expect(premultiply(255, 128) == 128)
    #expect(premultiply(100, 255) == 100)
    #expect(premultiply(100, 0) == 0)
  }

  @Test func transforms() {
    let m = Transform(a: 2, b: 1, c: -1, d: 3, tx: 5, ty: -7)
    let p = m * Vec2(1, 2)
    #expect(near(p, Vec2(5, 0)))
    #expect(near(m.inverse * p, Vec2(1, 2)))
    let zero = Transform(a: 0, d: 0)
    #expect(zero.inverse == .identity)
  }

  @Test func fitCanvas() {
    var (r, exact) = fit(
      canvasWidth: 320, canvasHeight: 180, windowWidth: 1280, windowHeight: 720, mode: .integer)
    #expect(r == Rect(x: 0, y: 0, width: 1280, height: 720))
    #expect(exact)

    (r, exact) = fit(
      canvasWidth: 320, canvasHeight: 180, windowWidth: 1000, windowHeight: 700, mode: .integer)
    #expect(r == Rect(x: 20, y: 80, width: 960, height: 540))
    #expect(exact)

    (r, exact) = fit(
      canvasWidth: 320, canvasHeight: 180, windowWidth: 1000, windowHeight: 700, mode: .fit)
    #expect(r == Rect(x: 0, y: 68, width: 1000, height: 563))
    #expect(!exact)

    (r, exact) = fit(
      canvasWidth: 320, canvasHeight: 180, windowWidth: 1000, windowHeight: 700, mode: .stretch)
    #expect(r == Rect(x: 0, y: 0, width: 1000, height: 700))
    #expect(!exact)

    (r, exact) = fit(
      canvasWidth: 320, canvasHeight: 180, windowWidth: 200, windowHeight: 100, mode: .integer)
    #expect(r.width < 200.5 && r.height <= 100)
    #expect(!exact)
  }

  @Test func circleSpans() {
    #expect(circleSpan(radius: 0, dy: 0) == 0)
    #expect(circleSpan(radius: 0, dy: 1) == -1)
    #expect(circleSpan(radius: 2, dy: 0) == 2)
    #expect(circleSpan(radius: 2, dy: 1) == 2)
    #expect(circleSpan(radius: 2, dy: -2) == 1)
    #expect(circleSpan(radius: 2, dy: 3) == -1)
    for r in 0..<200 {
      for dy in -r...r {
        let half = circleSpan(radius: r, dy: dy)
        #expect(half * half + dy * dy <= r * (r + 1))
        #expect((half + 1) * (half + 1) + dy * dy > r * (r + 1))
      }
    }
  }

  @Test func coordinatesSurviveNaN() {
    #expect(pixel(.nan) == -16_777_216)
    #expect(pixel(.infinity) == 16_777_216)
    #expect(pixel(-0.0005) == 0)
  }

  @Test func growableBuffer() {
    var buffer = GrowableBuffer<UInt32>(capacity: 2)
    for chunk in 0..<10 {
      let values = buffer.append(chunk + 1)
      for i in 0...chunk {
        (values + i).initialize(to: UInt32(buffer.count - chunk - 1 + i))
      }
    }
    #expect(buffer.count == 55)
    let values = buffer.withUnsafeBytes { Array($0.bindMemory(to: UInt32.self)) }
    #expect(values == (0..<55).map(UInt32.init))
    buffer.removeAll()
    #expect(buffer.count == 0)
  }

  /// The vertex attributes and the pixel uploads depend on these layouts.
  @Test func layouts() {
    #expect(MemoryLayout<BatchVertex>.stride == 28)
    #expect(MemoryLayout<Color>.stride == 4)
    #expect(MemoryLayout<Color>.offset(of: \.a) == 3)
  }
}

func near(_ a: Vec2, _ b: Vec2) -> Bool {
  abs(a.x - b.x) < 1e-4 && abs(a.y - b.y) < 1e-4
}

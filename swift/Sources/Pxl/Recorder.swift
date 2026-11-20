import CSDL3

/// The draw state and the draws of the current frame.
///
/// The draw functions of `Context` forward to the mutating methods of this
/// struct. In them, Swift checks exclusive access at compile time. On the
/// stored properties of a class, it checks at each access at run time, and it
/// copies values that it cannot prove stay unchanged.
///
/// A non-mutating method gets a copy of the whole struct, hundreds of bytes.
/// So the helpers of the draws are methods of the small values that they use:
/// `Quad`, `Transform`, `Circle` and `Font`.
struct Recorder: ~Copyable {
  /// The texture of plain color and of empty shader slots.
  let white: Texture
  let samplers: Samplers
  let defaultShader: Shader
  let defaultFont: Font
  /// The default target.
  var canvas: Texture
  var state = DrawState()
  var savedStates: [DrawState] = []
  var batch = Batch()
  var inFrame = false
  /// The first error of the frame.
  var frameError: PxlError?

  /// The current target: the target, or the canvas.
  var currentTarget: Texture {
    state.target ?? canvas
  }

  /// The size of the current target. Unlike `??`, `if let` reads the target
  /// without a retain.
  var targetSize: (width: Int, height: Int) {
    if let target = state.target {
      (target.width, target.height)
    } else {
      (canvas.width, canvas.height)
    }
  }

  /// Starts a frame with a new draw state.
  mutating func begin() {
    assert(!inFrame, "beginFrame() during a frame")
    inFrame = true
    frameError = nil
    state = DrawState()
    savedStates.removeAll(keepingCapacity: true)
  }

  /// Drops the draws of the frame. Textures and shaders that only the frame
  /// used go now.
  mutating func reset() {
    inFrame = false
    batch.removeAll()
  }

  /// Marks the frame as failed.
  mutating func fail(_ error: PxlError) {
    if frameError == nil {
      frameError = error
    }
  }
}

// MARK: - Draw state

extension Recorder {
  mutating func push() {
    savedStates.append(state)
  }

  mutating func pop() {
    assert(!savedStates.isEmpty, "pop() without push()")
    if let saved = savedStates.popLast() {
      state = saved
    }
  }

  mutating func setUniforms<T: BitwiseCopyable>(_ value: T) {
    assert(inFrame, "set uniforms during a frame")
    assert(MemoryLayout<T>.size % 16 == 0, "the size must be a multiple of 16")
    let padding = -batch.uniforms.count & 15
    batch.uniforms.append(contentsOf: repeatElement(0, count: padding))
    let offset = batch.uniforms.count
    withUnsafeBytes(of: value) { batch.uniforms.append(contentsOf: $0) }
    state.uniforms = offset..<batch.uniforms.count
  }
}

// MARK: - Drawing

extension Recorder {
  mutating func clear(_ color: Color) {
    assert(inFrame, "draw during a frame")
    batch.start(Command(kind: .clear(color), target: currentTarget))
  }

  mutating func drawSprite(
    _ texture: Texture, at position: Vec2, source: Rect?, origin: Vec2, scale: Vec2,
    rotation: Float, flipX: Bool, flipY: Bool, color: Color, overlay: Color
  ) {
    let src = source ?? Rect(x: 0, y: 0, width: Float(texture.width), height: Float(texture.height))
    let sx = flipX ? -scale.x : scale.x
    let sy = flipY ? -scale.y : scale.y
    let c = rotation == 0 ? 1 : SDL_cosf(rotation)
    let s = rotation == 0 ? 0 : SDL_sinf(rotation)
    let local = Transform(
      a: c * sx, b: s * sx, c: -s * sy, d: c * sy,
      tx: position.x - c * sx * origin.x + s * sy * origin.y,
      ty: position.y - s * sx * origin.x - c * sy * origin.y)
    let t = state.transform * local
    let quad = Quad(
      topLeft: t * Vec2(0, 0), topRight: t * Vec2(src.width, 0),
      bottomRight: t * Vec2(src.width, src.height), bottomLeft: t * Vec2(0, src.height)
    ).snapped(if: state.snap)
    addQuad(texture, quad, uv: texture.uv(of: src), color: color, overlay: overlay)
  }

  mutating func drawNineSlice(
    _ texture: Texture, in rect: Rect, left: Float, top: Float, right: Float, bottom: Float,
    source: Rect?, color: Color
  ) {
    let src = source ?? Rect(x: 0, y: 0, width: Float(texture.width), height: Float(texture.height))
    let sx: [4 of Float] = [src.x, src.x + left, src.x + src.width - right, src.x + src.width]
    let sy: [4 of Float] = [src.y, src.y + top, src.y + src.height - bottom, src.y + src.height]
    let dx: [4 of Float] = [
      rect.x, rect.x + left, rect.x + rect.width - right, rect.x + rect.width,
    ]
    let dy: [4 of Float] = [
      rect.y, rect.y + top, rect.y + rect.height - bottom, rect.y + rect.height,
    ]
    let size = Vec2(Float(texture.width), Float(texture.height))
    let t = state.transform
    let snap = state.snap
    for j in 0..<3 {
      for i in 0..<3 where dx[i + 1] > dx[i] && dy[j + 1] > dy[j] {
        let quad = Quad(
          topLeft: t.vertex(dx[i], dy[j], snapped: snap),
          topRight: t.vertex(dx[i + 1], dy[j], snapped: snap),
          bottomRight: t.vertex(dx[i + 1], dy[j + 1], snapped: snap),
          bottomLeft: t.vertex(dx[i], dy[j + 1], snapped: snap))
        let uv = (Vec2(sx[i], sy[j]) / size, Vec2(sx[i + 1], sy[j + 1]) / size)
        addQuad(texture, quad, uv: uv, color: color, overlay: .transparent)
      }
    }
  }

  mutating func drawPixel(at point: Vec2, color: Color) {
    let p = state.transform * point
    fill(x: pixel(p.x), y: pixel(p.y), width: 1, height: 1, color: color)
  }

  mutating func drawLine(from start: Vec2, to end: Vec2, color: Color) {
    let a = state.transform * start
    let b = state.transform * end
    line(from: (pixel(a.x), pixel(a.y)), to: (pixel(b.x), pixel(b.y)), last: true, color: color)
  }

  mutating func drawRect(_ rect: Rect, color: Color) {
    let x1 = rect.x + rect.width
    let y1 = rect.y + rect.height
    let t = state.transform
    let snap = state.snap
    let quad = Quad(
      topLeft: t.vertex(rect.x, rect.y, snapped: snap),
      topRight: t.vertex(x1, rect.y, snapped: snap),
      bottomRight: t.vertex(x1, y1, snapped: snap), bottomLeft: t.vertex(rect.x, y1, snapped: snap))
    addPlainQuad(quad, color: color)
  }

  mutating func drawRectLines(_ rect: Rect, color: Color) {
    let t = state.transform
    let x1 = rect.x + rect.width
    let y1 = rect.y + rect.height
    if t.b == 0 && t.c == 0 {
      let a = t * Vec2(rect.x, rect.y)
      let b = t * Vec2(x1, y1)
      let left = toInt(roundHalfUp(Float.minimum(a.x, b.x)))
      let top = toInt(roundHalfUp(Float.minimum(a.y, b.y)))
      let right = toInt(roundHalfUp(Float.maximum(a.x, b.x)))
      let bottom = toInt(roundHalfUp(Float.maximum(a.y, b.y)))
      let w = right - left
      let h = bottom - top
      if w <= 2 || h <= 2 {
        fill(x: left, y: top, width: w, height: h, color: color)
        return
      }
      fill(x: left, y: top, width: w, height: 1, color: color)
      fill(x: left, y: bottom - 1, width: w, height: 1, color: color)
      fill(x: left, y: top + 1, width: 1, height: h - 2, color: color)
      fill(x: right - 1, y: top + 1, width: 1, height: h - 2, color: color)
      return
    }
    // A turned rectangle: join the centers of its corner pixels.
    let corners: [4 of Vec2] = [
      t * Vec2(rect.x + 0.5, rect.y + 0.5), t * Vec2(x1 - 0.5, rect.y + 0.5),
      t * Vec2(x1 - 0.5, y1 - 0.5), t * Vec2(rect.x + 0.5, y1 - 0.5),
    ]
    for i in 0..<4 {
      let a = corners[i]
      let b = corners[(i + 1) % 4]
      line(from: (pixel(a.x), pixel(a.y)), to: (pixel(b.x), pixel(b.y)), last: false, color: color)
    }
  }

  mutating func drawCircle(at center: Vec2, radius: Float, color: Color) {
    guard
      let circle = Circle(
        center: center, radius: radius, transform: state.transform,
        targetHeight: targetSize.height)
    else { return }
    // Rows with the same span become one quad.
    var top = circle.rows.lowerBound
    var half = circleSpan(radius: circle.radius, dy: top)
    for dy in circle.rows.lowerBound + 1..<circle.rows.upperBound + 1 {
      let next = dy < circle.rows.upperBound ? circleSpan(radius: circle.radius, dy: dy) : -1
      if next != half {
        fill(
          x: circle.x - half, y: circle.y + top, width: 2 * half + 1, height: dy - top,
          color: color)
        top = dy
        half = next
      }
    }
  }

  mutating func drawCircleLines(at center: Vec2, radius: Float, color: Color) {
    guard
      let circle = Circle(
        center: center, radius: radius, transform: state.transform,
        targetHeight: targetSize.height)
    else { return }
    // A pixel is on the edge if one of its four neighbors is outside.
    for dy in circle.rows {
      let half = circleSpan(radius: circle.radius, dy: dy)
      let above = circleSpan(radius: circle.radius, dy: dy - 1)
      let below = circleSpan(radius: circle.radius, dy: dy + 1)
      let inner = min(min(above, below) + 1, half)
      let y = circle.y + dy
      if inner <= 0 {
        fill(x: circle.x - half, y: y, width: 2 * half + 1, height: 1, color: color)
      } else {
        let width = half - inner + 1
        fill(x: circle.x - half, y: y, width: width, height: 1, color: color)
        fill(x: circle.x + inner, y: y, width: width, height: 1, color: color)
      }
    }
  }

  mutating func drawTriangle(_ a: Vec2, _ b: Vec2, _ c: Vec2, color: Color) {
    guard beginPlainDraw(vertexCount: 3, indexCount: 3) else { return }
    let params = VertexParams.plain
    let t = state.transform
    let pa = t.vertex(a.x, a.y, snapped: state.snap)
    let pb = t.vertex(b.x, b.y, snapped: state.snap)
    let pc = t.vertex(c.x, c.y, snapped: state.snap)
    batch.addVertex(BatchVertex(pa, .zero, color: color, overlay: .transparent, params: params))
    batch.addVertex(BatchVertex(pb, .zero, color: color, overlay: .transparent, params: params))
    batch.addVertex(BatchVertex(pc, .zero, color: color, overlay: .transparent, params: params))
  }

  mutating func drawTriangles(_ vertices: [Vertex], texture: Texture?) {
    assert(vertices.count % 3 == 0, "the vertex count must be a multiple of 3")
    let count = vertices.count - vertices.count % 3
    guard count > 0 else { return }
    let params: VertexParams
    if let texture {
      guard let textured = beginDraw(texture, vertexCount: count, indexCount: count) else { return }
      params = textured
    } else {
      guard beginPlainDraw(vertexCount: count, indexCount: count) else { return }
      params = .plain
    }
    let t = state.transform
    let snap = state.snap
    for vertex in vertices[..<count] {
      batch.addVertex(
        BatchVertex(
          t.vertex(vertex.position.x, vertex.position.y, snapped: snap), vertex.uv,
          color: vertex.color,
          overlay: .transparent, params: params))
    }
  }

  mutating func drawText(_ text: String, at position: Vec2, color: Color) {
    let font = state.font ?? defaultFont
    var pen = position
    for scalar in text.unicodeScalars {
      if scalar == "\n" {
        pen = Vec2(position.x, pen.y + Float(font.lineHeight))
        continue
      }
      guard let glyph = font.glyph(for: scalar) else { continue }
      if scalar != " " {
        drawRegion(font.texture, font.cell(glyph), at: pen, color: color)
      }
      pen.x += font.advance(glyph)
    }
  }

  /// Draws an area of a texture with its top-left corner at `point`.
  private mutating func drawRegion(
    _ texture: Texture, _ area: Rect, at point: Vec2, color: Color
  ) {
    let t = state.transform
    let x1 = point.x + area.width
    let y1 = point.y + area.height
    let quad = Quad(
      topLeft: t * point, topRight: t * Vec2(x1, point.y), bottomRight: t * Vec2(x1, y1),
      bottomLeft: t * Vec2(point.x, y1)
    ).snapped(if: state.snap)
    addQuad(texture, quad, uv: texture.uv(of: area), color: color, overlay: .transparent)
  }
}

// MARK: - Batching

extension Recorder {
  /// Checks a draw of a texture, and adds its indices to the command of the
  /// current state.
  ///
  /// - Parameters:
  ///   - texture: The texture.
  ///   - vertexCount: The number of vertices that the draw adds.
  ///   - indexCount: The number of indices that the draw adds.
  /// - Returns: The params of the vertices, or nil if the draw cannot happen.
  mutating func beginDraw(_ texture: Texture, vertexCount: Int, indexCount: Int)
    -> VertexParams?
  {
    guard isRecording() else { return nil }
    let key = drawKey
    let drawsIntoItself = ObjectIdentifier(texture) == key.target
    assert(!drawsIntoItself, "a texture cannot be drawn into itself")
    guard !drawsIntoItself else {
      fail(PxlError("a texture cannot be drawn into itself"))
      return nil
    }
    guard hasRoom(vertexCount: vertexCount, indexCount: indexCount) else { return nil }
    let sampling = texture.sampling
    addToCommand(
      key, texture: texture, sampler: samplers[sampling], indexCount: UInt32(indexCount))
    return VertexParams(sampling)
  }

  /// Checks a draw of plain color, and adds its indices to the command of the
  /// current state. Shapes make one for each run of pixels, so it is inlined.
  ///
  /// - Returns: false if the draw cannot happen.
  @inline(always)
  mutating func beginPlainDraw(vertexCount: Int, indexCount: Int) -> Bool {
    guard isRecording(), hasRoom(vertexCount: vertexCount, indexCount: indexCount) else {
      return false
    }
    addToCommand(drawKey, texture: nil, sampler: nil, indexCount: UInt32(indexCount))
    return true
  }

  /// Returns true during a frame. Else it marks the frame as failed.
  @inline(always)
  private mutating func isRecording() -> Bool {
    assert(inFrame, "draw during a frame")
    guard inFrame else {
      fail(PxlError("draw outside of a frame"))
      return false
    }
    return true
  }

  /// Returns true if a draw can add vertices and indices: the clip area is not
  /// empty, and the indices fit in 32 bits.
  @inline(always)
  private mutating func hasRoom(vertexCount: Int, indexCount: Int) -> Bool {
    if let clip = state.clip, clip.width <= 0 || clip.height <= 0 {
      return false
    }
    guard batch.hasRoom(vertexCount: vertexCount, indexCount: indexCount) else {
      fail(PxlError("too many vertices"))
      return false
    }
    return true
  }

  /// The key of a draw with the current state.
  /// Unlike `??`, `if let` reads the target and the shader without a retain.
  private var drawKey: DrawKey {
    let target =
      if let target = state.target { ObjectIdentifier(target) } else { ObjectIdentifier(canvas) }
    let shader =
      if let shader = state.shader { ObjectIdentifier(shader) } else {
        ObjectIdentifier(defaultShader)
      }
    return DrawKey(
      target: target, shader: shader, blend: state.blend, clip: state.clip,
      uniforms: state.uniforms)
  }

  /// Adds indices to the last command if the draw can join it, or to a new
  /// command.
  ///
  /// - Parameters:
  ///   - key: The key of the draw.
  ///   - texture: The texture, or nil for plain color. Plain color can use any
  ///     texture, so it keeps the batch of the last draw.
  ///   - sampler: The sampler of `texture`.
  ///   - indexCount: The number of indices of the draw.
  private mutating func addToCommand(
    _ key: DrawKey, texture: Texture?, sampler: OpaquePointer?, indexCount: UInt32
  ) {
    let joined = batch.lastCommand?.join(
      key, texture: texture, sampler: sampler, state: state, samplers: samplers, white: white,
      indexCount: indexCount)
    if joined == true {
      return
    }
    let shader = state.shader ?? defaultShader
    var command = Command(
      kind: .draw(key), target: currentTarget, shader: shader, bindingCount: shader.textureCount,
      firstIndex: UInt32(batch.indices.count), indexCount: indexCount)
    command.bindings[0] = Binding(
      texture: texture ?? white, sampler: sampler ?? samplers[white.sampling])
    for slot in 1..<shader.textureCount {
      let bound = state.shaderTextures[slot] ?? white
      command.bindings[slot] = Binding(texture: bound, sampler: samplers[bound.sampling])
    }
    batch.start(command)
  }

  /// Appends a quad of a texture.
  ///
  /// - Parameters:
  ///   - texture: The texture.
  ///   - quad: The corners in target pixels.
  ///   - uv: The top-left and bottom-right of the texture area.
  mutating func addQuad(
    _ texture: Texture, _ quad: Quad, uv: (Vec2, Vec2), color: Color, overlay: Color
  ) {
    guard let params = beginDraw(texture, vertexCount: 4, indexCount: 6) else { return }
    batch.addQuad(quad, uv: uv, color: color, overlay: overlay, params: params)
  }

  /// Appends a quad of plain color.
  ///
  /// - Parameter quad: The corners in target pixels.
  mutating func addPlainQuad(_ quad: Quad, color: Color) {
    guard beginPlainDraw(vertexCount: 4, indexCount: 6) else { return }
    batch.addQuad(quad, uv: (.zero, .one), color: color, overlay: .transparent, params: .plain)
  }

  /// Fills a rectangle of target pixels. The transform does not apply.
  mutating func fill(x: Int, y: Int, width: Int, height: Int, color: Color) {
    let target = targetSize
    guard width > 0, height > 0, x < target.width, y < target.height, x &+ width > 0,
      y &+ height > 0
    else {
      return
    }
    let x0 = Float(x)
    let y0 = Float(y)
    let x1 = Float(x) + Float(width)
    let y1 = Float(y) + Float(height)
    let quad = Quad(
      topLeft: Vec2(x0, y0), topRight: Vec2(x1, y0), bottomRight: Vec2(x1, y1),
      bottomLeft: Vec2(x0, y1))
    addPlainQuad(quad, color: color)
  }

  /// Draws a Bresenham line between two pixels. Fills each straight run of
  /// pixels with one quad. Skips the pixels before and after the target on the
  /// major axis, so long lines stay fast.
  ///
  /// - Parameter last: Include the end pixel. Lines that join leave it out.
  mutating func line(
    from p0: (x: Int, y: Int), to p1: (x: Int, y: Int), last: Bool, color: Color
  ) {
    let target = targetSize
    let dx = abs(p1.x - p0.x)
    let dy = abs(p1.y - p0.y)
    let stepX = p0.x < p1.x ? 1 : -1
    let stepY = p0.y < p1.y ? 1 : -1
    let steep = dy > dx
    let major = steep ? dy : dx
    let minor = steep ? dx : dy
    let count = last ? major + 1 : major
    if major == 0 {
      if last {
        fill(x: p0.x, y: p0.y, width: 1, height: 1, color: color)
      }
      return
    }

    // Pixel i is at major coordinate start + i * step.
    let start = steep ? p0.y : p0.x
    let step = steep ? stepY : stepX
    let extent = steep ? target.height : target.width
    let first = max(step > 0 ? -start : start - (extent - 1), 0)
    let end = min(step > 0 ? extent - start : start + 1, count)
    guard first < end else { return }

    // Before pixel i, the minor coordinate moved k = ceil((2 m i - M) / 2 M).
    var k = 2 * minor * first - major
    k = k > 0 ? (k + 2 * major - 1) / (2 * major) : 0
    var error = 2 * minor - major + 2 * minor * first - 2 * major * k
    var x = p0.x + (steep ? k * stepX : first * stepX)
    var y = p0.y + (steep ? first * stepY : k * stepY)
    var runX = x
    var runY = y
    // The coordinates are in the float range, so no step can overflow.
    let twoMajor = 2 &* major
    let twoMinor = 2 &* minor
    for i in first..<end {
      let turn = error > 0
      let done = turn || i == end &- 1
      if done {
        fill(
          x: min(runX, x), y: min(runY, y), width: abs(x &- runX) &+ 1,
          height: abs(y &- runY) &+ 1, color: color)
      }
      if turn {
        if steep { x &+= stepX } else { y &+= stepY }
        error &-= twoMajor
      }
      error &+= twoMinor
      if steep { y &+= stepY } else { x &+= stepX }
      if done {
        runX = x
        runY = y
      }
    }
  }

}

/// A circle in target pixels.
struct Circle {
  var x, y, radius: Int
  /// The rows of the circle that are in the target.
  var rows: Range<Int>

  /// The largest radius in pixels.
  static let maxRadius = 1 << 14

  /// Transforms a circle to target pixels. Returns nil if it is not seen.
  init?(center: Vec2, radius: Float, transform t: Transform, targetHeight: Int) {
    let c = t * center
    let r = roundHalfUp(radius * abs(t.a * t.d - t.b * t.c).squareRoot())
    guard r >= 0 else { return nil }
    x = pixel(c.x)
    y = pixel(c.y)
    self.radius = r < Float(Self.maxRadius) ? Int(r) : Self.maxRadius
    let firstRow = max(-self.radius, -y)
    let lastRow = min(self.radius, targetHeight - 1 - y)
    guard firstRow <= lastRow else { return nil }
    rows = firstRow..<lastRow + 1
  }
}

/// Gets the half width of a row of a circle. The row holds the pixels with
/// dx * dx + dy * dy <= r * (r + 1). Returns -1 for rows outside.
func circleSpan(radius: Int, dy: Int) -> Int {
  guard (-radius...radius).contains(dy) else { return -1 }
  // The radius is at most Circle.maxRadius, so nothing can overflow.
  let area = radius &* (radius &+ 1) &- dy &* dy
  var half = Int(Float(area).squareRoot())
  while (half &+ 1) &* (half &+ 1) <= area {
    half &+= 1
  }
  while half &* half > area {
    half &-= 1
  }
  return half
}

/// Floats hold all whole numbers up to this.
private let coordinateLimit: Float = 16_777_216

/// Converts a whole float to an int. Clamps it to the exact float range. NaN
/// gives the lower limit.
@inline(always)
func toInt(_ value: Float) -> Int {
  Int(Float.minimum(Float.maximum(value, -coordinateLimit), coordinateLimit))
}

/// Returns the pixel that holds a coordinate. Allows a small float error.
@inline(always)
func pixel(_ value: Float) -> Int {
  toInt((value + 1e-3).rounded(.down))
}

extension Texture {
  /// Gets the texture coordinates of an area in texels.
  @inline(always)
  func uv(of area: Rect) -> (Vec2, Vec2) {
    let size = Vec2(Float(width), Float(height))
    return (Vec2(area.x, area.y) / size, Vec2(area.x + area.width, area.y + area.height) / size)
  }
}

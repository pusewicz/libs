import CSDL3

/// The draw state and the draws of the current frame.
///
/// The draw functions of `Context` forward to the mutating methods of this
/// struct. In them, Swift checks exclusive access at compile time. On the
/// stored properties of a class, it checks at each access at run time, and it
/// copies values that it cannot prove stay unchanged.
struct Recorder {
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

  /// The size of the current target. It reads the target without a retain.
  var targetSize: (width: Int, height: Int) {
    let target = state.target ?? canvas
    return (target.width, target.height)
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
    batch.commands.append(Command(kind: .clear(color), target: currentTarget))
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
    let corners = snapped([
      t * Vec2(0, 0), t * Vec2(src.width, 0), t * Vec2(src.width, src.height),
      t * Vec2(0, src.height),
    ])
    addQuad(texture, corners, uv: texture.uv(of: src), color: color, overlay: overlay)
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
    for j in 0..<3 {
      for i in 0..<3 where dx[i + 1] > dx[i] && dy[j + 1] > dy[j] {
        let corners: [4 of Vec2] = [
          vertexPosition(dx[i], dy[j]), vertexPosition(dx[i + 1], dy[j]),
          vertexPosition(dx[i + 1], dy[j + 1]), vertexPosition(dx[i], dy[j + 1]),
        ]
        let uv = (Vec2(sx[i], sy[j]) / size, Vec2(sx[i + 1], sy[j + 1]) / size)
        addQuad(texture, corners, uv: uv, color: color, overlay: .transparent)
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
    let corners: [4 of Vec2] = [
      vertexPosition(rect.x, rect.y), vertexPosition(x1, rect.y), vertexPosition(x1, y1),
      vertexPosition(rect.x, y1),
    ]
    addQuad(nil, corners, uv: (.zero, .one), color: color, overlay: .transparent)
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
    guard let circle = circleInTarget(center, radius) else { return }
    // Rows with the same span become one quad.
    var top = circle.rows.lowerBound
    var half = circleSpan(radius: circle.radius, dy: top)
    for dy in circle.rows.lowerBound + 1...circle.rows.upperBound + 1 {
      let next = dy <= circle.rows.upperBound ? circleSpan(radius: circle.radius, dy: dy) : -1
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
    guard let circle = circleInTarget(center, radius) else { return }
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
    guard let params = beginDraw(nil, vertexCount: 3, indexCount: 3) else { return }
    for p in [a, b, c] {
      batch.addVertex(
        BatchVertex(
          vertexPosition(p.x, p.y), .zero, color: color, overlay: .transparent, params: params))
    }
  }

  mutating func drawTriangles(_ vertices: [Vertex], texture: Texture?) {
    assert(vertices.count % 3 == 0, "the vertex count must be a multiple of 3")
    let count = vertices.count - vertices.count % 3
    guard count > 0, let params = beginDraw(texture, vertexCount: count, indexCount: count) else {
      return
    }
    for vertex in vertices[..<count] {
      batch.addVertex(
        BatchVertex(
          vertexPosition(vertex.position.x, vertex.position.y), vertex.uv, color: vertex.color,
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

  func measureText(_ text: String) -> Vec2 {
    let font = state.font ?? defaultFont
    var width: Float = 0
    var line: Float = 0
    var lines = text.isEmpty ? 0 : 1
    for scalar in text.unicodeScalars {
      if scalar == "\n" {
        line = 0
        lines += 1
      } else if let glyph = font.glyph(for: scalar) {
        line += font.advance(glyph)
        width = Float.maximum(width, line)
      }
    }
    return Vec2(width, Float(lines) * Float(font.lineHeight))
  }

  /// Draws an area of a texture with its top-left corner at `point`.
  private mutating func drawRegion(
    _ texture: Texture, _ area: Rect, at point: Vec2, color: Color
  ) {
    let t = state.transform
    let x1 = point.x + area.width
    let y1 = point.y + area.height
    let corners = snapped([
      t * point, t * Vec2(x1, point.y), t * Vec2(x1, y1), t * Vec2(point.x, y1),
    ])
    addQuad(texture, corners, uv: texture.uv(of: area), color: color, overlay: .transparent)
  }
}

// MARK: - Batching

extension Recorder {
  /// Checks a draw, and adds its indices to the command of the current state.
  ///
  /// - Parameters:
  ///   - texture: The texture, or nil for plain color.
  ///   - vertexCount: The number of vertices that the draw adds.
  ///   - indexCount: The number of indices that the draw adds.
  /// - Returns: The params of the vertices, or nil if the draw cannot happen.
  mutating func beginDraw(_ texture: Texture?, vertexCount: Int, indexCount: Int)
    -> SIMD4<UInt8>?
  {
    assert(inFrame, "draw during a frame")
    guard inFrame else {
      fail(PxlError("draw outside of a frame"))
      return nil
    }
    let key = drawKey
    let drawsIntoItself = texture.map { ObjectIdentifier($0) == key.target } ?? false
    assert(!drawsIntoItself, "a texture cannot be drawn into itself")
    guard !drawsIntoItself else {
      fail(PxlError("a texture cannot be drawn into itself"))
      return nil
    }
    if let clip = state.clip, clip.width <= 0 || clip.height <= 0 {
      return nil
    }
    guard batch.hasRoom(vertexCount: vertexCount, indexCount: indexCount) else {
      fail(PxlError("too many vertices"))
      return nil
    }
    let sampling = texture?.sampling
    addToCommand(key, texture: texture, sampling: sampling, indexCount: UInt32(indexCount))
    return BatchVertex.params(sampling)
  }

  /// The key of a draw with the current state.
  private var drawKey: DrawKey {
    DrawKey(
      target: ObjectIdentifier(state.target ?? canvas),
      shader: ObjectIdentifier(state.shader ?? defaultShader), blend: state.blend,
      clip: state.clip, uniforms: state.uniforms)
  }

  /// Adds indices to the last command if the draw can join it, or to a new
  /// command.
  ///
  /// - Parameters:
  ///   - key: The key of the draw.
  ///   - texture: The texture, or nil for plain color. Plain color can use any
  ///     texture, so it keeps the batch of the last draw.
  ///   - sampling: The settings of `texture`.
  ///   - indexCount: The number of indices of the draw.
  private mutating func addToCommand(
    _ key: DrawKey, texture: Texture?, sampling: Sampling?, indexCount: UInt32
  ) {
    let last = batch.commands.count - 1
    if last >= 0
      && batch.commands[last].join(
        key, texture: texture, sampling: sampling, shaderTextures: state.shaderTextures,
        samplers: samplers, white: white, indexCount: indexCount)
    {
      return
    }
    let shader = state.shader ?? defaultShader
    var command = Command(
      kind: .draw(key), target: currentTarget, shader: shader, bindingCount: shader.textureCount,
      firstIndex: UInt32(batch.indices.count), indexCount: indexCount)
    command.bindings[0] = Binding(
      texture: texture ?? white, sampler: samplers[sampling ?? white.sampling])
    for slot in 1..<shader.textureCount {
      let bound = state.shaderTextures[slot] ?? white
      command.bindings[slot] = Binding(texture: bound, sampler: samplers[bound.sampling])
    }
    batch.commands.append(command)
  }

  /// Appends a quad.
  ///
  /// - Parameters:
  ///   - texture: The texture, or nil for plain color.
  ///   - corners: The corners in target pixels: top-left, top-right,
  ///     bottom-right, bottom-left of the texture area.
  ///   - uv: The top-left and bottom-right of the texture area.
  mutating func addQuad(
    _ texture: Texture?, _ corners: [4 of Vec2], uv: (Vec2, Vec2), color: Color, overlay: Color
  ) {
    guard let params = beginDraw(texture, vertexCount: 4, indexCount: 6) else { return }
    batch.addQuad(corners, uv: uv, color: color, overlay: overlay, params: params)
  }

  /// Fills a rectangle of target pixels. The transform does not apply.
  mutating func fill(x: Int, y: Int, width: Int, height: Int, color: Color) {
    let target = targetSize
    guard width > 0, height > 0, x < target.width, y < target.height, x + width > 0,
      y + height > 0
    else {
      return
    }
    let x0 = Float(x)
    let y0 = Float(y)
    let x1 = Float(x) + Float(width)
    let y1 = Float(y) + Float(height)
    addQuad(
      nil, [Vec2(x0, y0), Vec2(x1, y0), Vec2(x1, y1), Vec2(x0, y1)], uv: (.zero, .one),
      color: color, overlay: .transparent)
  }

  /// Moves the corners so that the first is on a whole pixel, if snap is on.
  func snapped(_ corners: [4 of Vec2]) -> [4 of Vec2] {
    guard state.snap else { return corners }
    let offset = (corners[0] + 0.5).rounded(.down) - corners[0]
    var moved = corners
    for i in 0..<4 {
      moved[i] += offset
    }
    return moved
  }

  /// Transforms a point, and rounds it if snap is on.
  func vertexPosition(_ x: Float, _ y: Float) -> Vec2 {
    let p = state.transform * Vec2(x, y)
    return state.snap ? (p + 0.5).rounded(.down) : p
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
    for i in first..<end {
      let turn = error > 0
      let done = turn || i == end - 1
      if done {
        fill(
          x: min(runX, x), y: min(runY, y), width: abs(x - runX) + 1, height: abs(y - runY) + 1,
          color: color)
      }
      if turn {
        if steep { x += stepX } else { y += stepY }
        error -= 2 * major
      }
      error += 2 * minor
      if steep { y += stepY } else { x += stepX }
      if done {
        runX = x
        runY = y
      }
    }
  }

  /// A circle in target pixels.
  struct Circle {
    var x, y, radius: Int
    /// The rows of the circle that are in the target.
    var rows: ClosedRange<Int>
  }

  /// Transforms a circle to target pixels. Returns nil if it is not seen.
  func circleInTarget(_ center: Vec2, _ radius: Float) -> Circle? {
    let t = state.transform
    let c = t * center
    let r = roundHalfUp(radius * abs(t.a * t.d - t.b * t.c).squareRoot())
    guard r >= 0 else { return nil }
    let x = pixel(c.x)
    let y = pixel(c.y)
    let radius = r < Float(maxRadius) ? Int(r) : maxRadius
    let firstRow = max(-radius, -y)
    let lastRow = min(radius, targetSize.height - 1 - y)
    guard firstRow <= lastRow else { return nil }
    return Circle(x: x, y: y, radius: radius, rows: firstRow...lastRow)
  }
}

/// The largest radius of a circle in pixels.
private let maxRadius = 1 << 14

/// Gets the half width of a row of a circle. The row holds the pixels with
/// dx * dx + dy * dy <= r * (r + 1). Returns -1 for rows outside.
func circleSpan(radius: Int, dy: Int) -> Int {
  guard (-radius...radius).contains(dy) else { return -1 }
  let area = radius * (radius + 1) - dy * dy
  var half = Int(Float(area).squareRoot())
  while (half + 1) * (half + 1) <= area {
    half += 1
  }
  while half * half > area {
    half -= 1
  }
  return half
}

/// Floats hold all whole numbers up to this.
private let coordinateLimit: Float = 16_777_216

/// Converts a whole float to an int. Clamps it to the exact float range. NaN
/// gives the lower limit.
func toInt(_ value: Float) -> Int {
  Int(Float.minimum(Float.maximum(value, -coordinateLimit), coordinateLimit))
}

/// Returns the pixel that holds a coordinate. Allows a small float error.
func pixel(_ value: Float) -> Int {
  toInt((value + 1e-3).rounded(.down))
}

extension Texture {
  /// Gets the texture coordinates of an area in texels.
  func uv(of area: Rect) -> (Vec2, Vec2) {
    let size = Vec2(Float(width), Float(height))
    return (Vec2(area.x, area.y) / size, Vec2(area.x + area.width, area.y + area.height) / size)
  }
}

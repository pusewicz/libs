extension Context {
  /// Clears the whole current target. The clip area does not apply.
  public func clear(_ color: Color) {
    recorder.clear(color)
  }

  /// Draws an area of a texture.
  ///
  /// - Parameters:
  ///   - texture: The texture.
  ///   - position: The position of the origin.
  ///   - source: The area of the texture in texels. nil: the whole texture.
  ///   - origin: The point that `position` gives, in texels from the top-left
  ///     of `source`.
  ///   - scale: The scale around the origin.
  ///   - rotation: The angle around the origin.
  ///   - flipX: Mirror the sprite around the origin.
  ///   - flipY: Mirror the sprite around the origin.
  ///   - color: Multiplies the texels.
  ///   - overlay: Mixes its color into the texels by its alpha. Use it to flash
  ///     a sprite.
  public func drawSprite(
    _ texture: Texture, at position: Vec2, source: Rect? = nil, origin: Vec2 = .zero,
    scale: Vec2 = .one, rotation: Float = 0, flipX: Bool = false, flipY: Bool = false,
    color: Color = .white, overlay: Color = .transparent
  ) {
    recorder.drawSprite(
      texture, at: position, source: source, origin: origin, scale: scale, rotation: rotation,
      flipX: flipX, flipY: flipY, color: color, overlay: overlay)
  }

  /// Draws a texture area that stretches to fill an area. The corners keep
  /// their size, the edges stretch along one axis and the center along both.
  ///
  /// - Parameters:
  ///   - texture: The texture.
  ///   - rect: The area to fill.
  ///   - left: The left border in texels.
  ///   - top: The top border in texels.
  ///   - right: The right border in texels.
  ///   - bottom: The bottom border in texels.
  ///   - source: The area of the texture in texels. nil: the whole texture.
  ///   - color: Multiplies the texels.
  public func drawNineSlice(
    _ texture: Texture, in rect: Rect, left: Float, top: Float, right: Float, bottom: Float,
    source: Rect? = nil, color: Color = .white
  ) {
    recorder.drawNineSlice(
      texture, in: rect, left: left, top: top, right: right, bottom: bottom, source: source,
      color: color)
  }

  /// Sets one pixel: the pixel at the transformed point.
  public func drawPixel(at point: Vec2, color: Color) {
    recorder.drawPixel(at: point, color: color)
  }

  /// Draws a line one pixel wide. It includes both end pixels. The transform
  /// moves the ends, but the line stays one pixel wide.
  public func drawLine(from start: Vec2, to end: Vec2, color: Color) {
    recorder.drawLine(from: start, to: end, color: color)
  }

  /// Fills a rectangle.
  public func drawRect(_ rect: Rect, color: Color) {
    recorder.drawRect(rect, color: color)
  }

  /// Draws the edge of a rectangle, one pixel wide, inside the rectangle.
  public func drawRectLines(_ rect: Rect, color: Color) {
    recorder.drawRectLines(rect, color: color)
  }

  /// Fills a circle. The center is a pixel, so the diameter is 2 radius + 1.
  ///
  /// - Parameters:
  ///   - center: The center.
  ///   - radius: The radius in pixels. The transform scales it.
  ///   - color: The color.
  public func drawCircle(at center: Vec2, radius: Float, color: Color) {
    recorder.drawCircle(at: center, radius: radius, color: color)
  }

  /// Draws the edge of a circle, one pixel wide. See
  /// `drawCircle(at:radius:color:)`.
  public func drawCircleLines(at center: Vec2, radius: Float, color: Color) {
    recorder.drawCircleLines(at: center, radius: radius, color: color)
  }

  /// Fills a triangle.
  public func drawTriangle(_ a: Vec2, _ b: Vec2, _ c: Vec2, color: Color) {
    recorder.drawTriangle(a, b, c, color: color)
  }

  /// Draws triangles.
  ///
  /// - Parameters:
  ///   - vertices: Three vertices for each triangle.
  ///   - texture: The texture, or nil for plain color.
  public func drawTriangles(_ vertices: [Vertex], texture: Texture? = nil) {
    recorder.drawTriangles(vertices, texture: texture)
  }

  /// Draws text with the current font. "\n" starts a new line. pxl draws "?"
  /// for code points that the font does not have.
  ///
  /// - Parameters:
  ///   - text: The text.
  ///   - position: The top-left corner of the text.
  ///   - color: The color.
  public func drawText(_ text: String, at position: Vec2, color: Color) {
    recorder.drawText(text, at: position, color: color)
  }

  /// Measures text as `drawText(_:at:color:)` draws it.
  ///
  /// - Returns: The width of the widest line and the height of all lines.
  public func measureText(_ text: String) -> Vec2 {
    (recorder.state.font ?? recorder.defaultFont).measure(text)
  }
}

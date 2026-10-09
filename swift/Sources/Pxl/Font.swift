/// A bitmap font: a texture with one glyph in each cell of a grid.
public struct Font {
  /// The glyph sheet.
  public let texture: Texture

  /// The width of a cell in texels.
  public let glyphWidth: Int

  /// The height of a cell in texels.
  public let glyphHeight: Int

  /// The cells in each row.
  public let columns: Int

  /// The code point of the first cell.
  public let first: Unicode.Scalar

  /// The number of glyphs.
  public let count: Int

  /// The distance from one line to the next.
  public let lineHeight: Int

  /// The advance of each glyph in pixels, or nil for `glyphWidth`.
  private let advances: [UInt8]?

  /// Creates a font.
  ///
  /// - Parameters:
  ///   - texture: The glyph sheet. Use white glyphs.
  ///   - glyphWidth: The width of a cell in texels.
  ///   - glyphHeight: The height of a cell in texels.
  ///   - columns: The cells in each row. nil: the texture width / glyphWidth.
  ///   - first: The code point of the first cell.
  ///   - count: The number of glyphs. nil: all cells.
  ///   - advances: The advance of each glyph in pixels. nil: glyphWidth.
  ///   - lineHeight: The distance from one line to the next. nil: glyphHeight.
  public init(
    texture: Texture, glyphWidth: Int, glyphHeight: Int, columns: Int? = nil,
    first: Unicode.Scalar = " ", count: Int? = nil, advances: [UInt8]? = nil,
    lineHeight: Int? = nil
  ) throws(PxlError) {
    guard glyphWidth > 0, glyphHeight > 0 else {
      throw PxlError("a font needs a glyph size")
    }
    let columns = columns ?? texture.width / glyphWidth
    let cells = columns * (texture.height / glyphHeight)
    let count = count ?? cells
    guard columns > 0, count > 0, count <= cells else {
      throw PxlError("the font texture is too small")
    }
    guard advances.map({ $0.count >= count }) ?? true else {
      throw PxlError("a font needs an advance for each glyph")
    }
    self.texture = texture
    self.glyphWidth = glyphWidth
    self.glyphHeight = glyphHeight
    self.columns = columns
    self.first = first
    self.count = count
    self.advances = advances
    self.lineHeight = lineHeight ?? glyphHeight
  }

  /// Creates the built-in font: glyphs of 5 x 9 pixels in cells of 6 x 10
  /// texels.
  init(builtInFor device: Device) throws(PxlError) {
    let glyphWidth = 5
    let glyphHeight = 9
    let cellWidth = 6
    let cellHeight = 10
    let columns = 16
    let count = fontAdvances.count
    let width = columns * cellWidth
    let height = (count + columns - 1) / columns * cellHeight
    var pixels = [Color](repeating: .transparent, count: width * height)
    for glyph in 0..<count {
      let left = glyph % columns * cellWidth
      let top = glyph / columns * cellHeight
      for row in 0..<glyphHeight {
        let bits = fontRows[glyph * glyphHeight + row]
        for column in 0..<glyphWidth where bits & (1 << (glyphWidth - 1 - column)) != 0 {
          pixels[(top + row) * width + left + column] = .white
        }
      }
    }
    try self.init(
      texture: Texture(device: device, width: width, height: height, pixels: pixels),
      glyphWidth: cellWidth, glyphHeight: cellHeight, columns: columns, count: count,
      advances: fontAdvances)
  }

  /// Gets the glyph of a code point, the glyph of "?", or nil.
  func glyph(for scalar: Unicode.Scalar) -> Int? {
    let indices = 0..<count
    let index = Int(scalar.value) - Int(first.value)
    if indices.contains(index) {
      return index
    }
    let fallback = Int(("?" as Unicode.Scalar).value) - Int(first.value)
    return indices.contains(fallback) ? fallback : nil
  }

  /// Gets the advance of a glyph in pixels.
  func advance(_ glyph: Int) -> Float {
    advances.map { Float($0[glyph]) } ?? Float(glyphWidth)
  }

  /// Gets the area of a glyph in the texture.
  func cell(_ glyph: Int) -> Rect {
    Rect(
      x: Float(glyph % columns * glyphWidth), y: Float(glyph / columns * glyphHeight),
      width: Float(glyphWidth), height: Float(glyphHeight))
  }
}

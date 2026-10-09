# frozen_string_literal: true

# Converts tools/pxl/font.txt to the C arrays of the built-in font of pxl.h.
module PxlFont
  FIRST = 32
  COUNT = 95
  WIDTH = 5
  HEIGHT = 9
  # Digits have one width, so numbers do not move when they change.
  DIGIT_ADVANCE = 6
  SPACE_ADVANCE = 4

  module_function

  # Returns a hash from each character to its rows.
  def parse(path)
    glyphs = {}
    rows = nil
    File.readlines(path, chomp: true).each_with_index do |line, index|
      if (match = line.match(/\A\[(.)\]\z/))
        rows = glyphs[match[1]] = []
      elsif rows && !line.empty?
        abort("#{path}:#{index + 1}: bad glyph row") unless line.match?(/\A[#.]{1,#{WIDTH}}\z/o)
        rows << line
      end
    end
    glyphs
  end

  # Returns the advance of a glyph: its width and one column of space.
  def advance(char, rows)
    return DIGIT_ADVANCE if char.match?(/\d/)

    right = rows.filter_map { |row| row.rindex("#") }.max
    right ? right + 2 : SPACE_ADVANCE
  end

  # Returns a row as bits. The left column is bit WIDTH - 1.
  def row_bits(row)
    row.chars.each_with_index.sum { |pixel, column| pixel == "#" ? 1 << (WIDTH - 1 - column) : 0 }
  end

  # Returns the C code for the generated section of pxl.h.
  def generate(path)
    glyphs = parse(path)
    bitmaps = []
    advances = []
    COUNT.times do |index|
      char = (FIRST + index).chr
      rows = glyphs.fetch(char) { abort("#{path} has no glyph for #{char.inspect}") }
      abort("#{path}: #{char.inspect} has more than #{HEIGHT} rows") if rows.size > HEIGHT
      bits = Array.new(HEIGHT) { |row| row_bits(rows[row] || "") }
      bitmaps << format("  {%s}, // 0x%02x", bits.map { |b| format("0x%02x", b) }.join(", "), FIRST + index)
      advances << advance(char, rows)
    end
    <<~C
      // clang-format off
      static const uint8_t pxl__font_rows[#{COUNT}][#{HEIGHT}] = {
      #{bitmaps.join("\n")}
      };
      static const uint8_t pxl__font_advances[#{COUNT}] = {
      #{advances.each_slice(16).map { |line| "  #{line.join(', ')}," }.join("\n")}
      };
      // clang-format on
    C
  end
end

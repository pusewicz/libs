# frozen_string_literal: true

# Writes the Aseprite files that tests/pxl_anim/test_pxl_anim.c reads.
#
# Usage: ruby tests/pxl_anim/fixtures.rb OUTPUT_DIR

require_relative "../aseprite/fixtures"

# Builds the test files.
module AnimFixtures
  module_function

  extend Ase

  # A square of one color, with a transparent border of one pixel.
  def square(size, rgba) = Array.new(size * size) { |i| border?(i, size) ? [0, 0, 0, 0] : rgba }.flatten.pack("C*")

  def border?(index, size)
    x = index % size
    y = index / size
    x.zero? || y.zero? || x == size - 1 || y == size - 1
  end

  # A character like the Orc of the Tiny RPG Character Asset Pack: a shadow
  # that is the same in all frames, a body in a group with a hidden sketch,
  # effects in two frames and a hidden layer.
  def character
    layers = [
      layer("shadow"), layer("char", type: 1), layer("body", level: 1), layer("sketch", level: 1, flags: 2),
      layer("fx"), layer("hidden", flags: 2)
    ]
    tags = tags([
                  [0, 1, 0, 0, [0, 0, 0], "idle"], [1, 3, 1, 2, [0, 0, 0], "walk"],
                  [2, 4, 2, 3, [0, 0, 0], "attack"], [3, 5, 3, 0, [0, 0, 0], "back"],
                  [5, 5, 7, 1, [0, 0, 0], "future"]
                ])
    body = ->(frame) { square(6, [10 * frame, 200, 30, 255]) }
    durations = [100, 50, 0, 200, 100, 100]
    frames = durations.each_with_index.map do |duration, frame|
      chunks = frame.zero? ? layers + [tags, image_cel(0, 8, 3, square(8, [0, 0, 0, 90])[0, 96], x: 4, y: 13)] : []
      chunks << linked_cel(0, 0, x: 4, y: 13) unless frame.zero?
      # Frames 4 and 5 have the same body at other places.
      chunks << image_cel(2, 6, 6, body.call([frame, 4].min), x: 5 + frame, y: 7 - (frame % 2))
      chunks << image_cel(3, 16, 16, square(16, [255, 0, 255, 255]))
      chunks << image_cel(4, 4, 4, square(4, [255, 255, 0, 200]), x: 11, y: 2 + frame) if [3, 4].include?(frame)
      chunks << image_cel(5, 3, 3, square(3, [0, 0, 255, 255]), x: 1, y: 1)
      frame(chunks, duration: duration)
    end
    file(frames, width: 16, height: 16)
  end

  # A sprite without cels.
  def empty = file([frame([layer("layer"), tags([[0, 1, 0, 1, [0, 0, 0], "all"]])]), frame([])], width: 8, height: 8)

  # Two frames of 64 x 64 opaque pixels that are not the same.
  def big
    pixels = ->(seed) { Array.new(64 * 64) { |i| [(i * seed) % 256, seed, 7, 255] }.flatten.pack("C*") }
    file([frame([layer("layer"), image_cel(0, 64, 64, pixels.call(3))]), frame([image_cel(0, 64, 64, pixels.call(5))])],
         width: 64, height: 64)
  end

  def write(directory)
    FileUtils.mkdir_p(directory)
    { "character" => character, "empty" => empty, "big" => big }.each do |name, data|
      File.binwrite(File.join(directory, "#{name}.aseprite"), data)
    end
  end
end

abort "usage: ruby fixtures.rb OUTPUT_DIR" unless ARGV.size == 1
AnimFixtures.write(ARGV[0])

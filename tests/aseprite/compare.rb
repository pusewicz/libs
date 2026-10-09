# frozen_string_literal: true

# Compares aseprite_render_frame() with Aseprite. Aseprite exports the frames
# of each file as PNG files, and the render program writes its frames. The
# script reports the frames that are not the same.
#
# Usage: ruby tests/aseprite/compare.rb ASEPRITE RENDER WORK_DIR FILE...

require "fileutils"
require "open3"
require "zlib"

# Decodes the PNG files that Aseprite writes: 8 bits per channel, no
# interlace.
module Png
  module_function

  CHANNELS = { 0 => 1, 2 => 3, 3 => 1, 4 => 2, 6 => 4 }.freeze

  # Returns [width, height, RGBA bytes].
  def read(path)
    data = File.binread(path)
    raise "#{path}: not a PNG file" unless data.start_with?("\x89PNG\r\n\x1a\n".b)

    chunks = parse_chunks(data)
    width, height, depth, type, _, _, interlace = chunks.fetch("IHDR").first.unpack("NNCCCCC")
    raise "#{path}: unsupported PNG (depth #{depth}, interlace #{interlace})" unless depth == 8 && interlace.zero?

    channels = CHANNELS.fetch(type) { raise "#{path}: unsupported PNG color type #{type}" }
    raw = unfilter(Zlib::Inflate.inflate(chunks.fetch("IDAT").join), width * channels, height, channels)
    [width, height, to_rgba(raw, type, chunks)]
  end

  def parse_chunks(data)
    chunks = Hash.new { |hash, key| hash[key] = [] }
    offset = 8
    while offset < data.bytesize
      length, type = data.byteslice(offset, 8).unpack("Na4")
      chunks[type] << data.byteslice(offset + 8, length)
      offset += length + 12
    end
    chunks
  end

  def unfilter(data, stride, height, bpp)
    previous = "\0".b * stride
    rows = Array.new(height) do |y|
      filter = data.getbyte(y * (stride + 1))
      row = data.byteslice((y * (stride + 1)) + 1, stride).bytes
      row.each_index do |x|
        left = x >= bpp ? row[x - bpp] : 0
        up = previous.getbyte(x)
        corner = x >= bpp ? previous.getbyte(x - bpp) : 0
        row[x] = (row[x] + predict(filter, left, up, corner)) & 0xFF
      end
      previous = row.pack("C*")
    end
    rows.join
  end

  def predict(filter, left, up, corner)
    case filter
    when 0 then 0
    when 1 then left
    when 2 then up
    when 3 then (left + up) / 2
    when 4 then paeth(left, up, corner)
    else raise "unknown PNG filter #{filter}"
    end
  end

  def paeth(left, up, corner)
    estimate = left + up - corner
    distances = [(estimate - left).abs, (estimate - up).abs, (estimate - corner).abs]
    return left if distances[0] <= distances[1] && distances[0] <= distances[2]

    distances[1] <= distances[2] ? up : corner
  end

  def to_rgba(raw, type, chunks)
    bytes = raw.bytes
    case type
    when 6 then raw
    when 2 then bytes.each_slice(3).map { |r, g, b| [r, g, b, 255] }.flatten.pack("C*")
    when 4 then bytes.each_slice(2).map { |v, a| [v, v, v, a] }.flatten.pack("C*")
    when 0 then bytes.map { |v| [v, v, v, 255] }.flatten.pack("C*")
    when 3 then indexed(bytes, chunks)
    end
  end

  def indexed(bytes, chunks)
    palette = chunks.fetch("PLTE").first.bytes.each_slice(3).to_a
    alpha = chunks["tRNS"].first&.bytes || []
    bytes.map { |i| palette.fetch(i) + [alpha.fetch(i, 255)] }.flatten.pack("C*")
  end
end

# Exports the frames of a file with Aseprite and with the render program.
# Returns the count of frames that are not the same.
def compare(aseprite, render, work, file, index)
  name = "#{index}-#{File.basename(file, ".*")}"
  expected_dir = File.join(work, name, "aseprite")
  actual_dir = File.join(work, name, "render")
  [expected_dir, actual_dir].each do |dir|
    FileUtils.rm_rf(dir)
    FileUtils.mkdir_p(dir)
  end
  run(aseprite, "-b", file, "--save-as", File.join(expected_dir, "{frame}.png"))
  run(render, file, actual_dir)

  actual = Dir[File.join(actual_dir, "*.rgba")].sort_by { File.basename(_1, ".rgba").to_i }
  expected = Dir[File.join(expected_dir, "*.png")].sort_by { File.basename(_1, ".png").to_i }
  if actual.size != expected.size
    puts "#{file}: #{actual.size} frames, Aseprite has #{expected.size}"
    return 1
  end
  actual.zip(expected).each_with_index.count do |(ours, theirs), frame|
    !same_frame?(file, frame, File.binread(ours), *Png.read(theirs))
  end
end

def run(*command)
  output, status = Open3.capture2e(*command)
  abort "#{command.join(' ')} failed:\n#{output}" unless status.success?
end

# Compares the pixels of a frame. RGB values of transparent pixels do not
# count.
def same_frame?(file, frame, ours, width, height, theirs)
  if ours.bytesize != theirs.bytesize
    puts "#{file}: frame #{frame}: #{ours.bytesize} bytes, Aseprite has #{theirs.bytesize}"
    return false
  end
  differences = (0...(width * height)).filter_map do |i|
    a = ours.byteslice(i * 4, 4).bytes
    b = theirs.byteslice(i * 4, 4).bytes
    next if a == b || (a[3].zero? && b[3].zero?)

    [i % width, i / width, a, b]
  end
  return true if differences.empty?

  x, y, a, b = differences.first
  puts "#{file}: frame #{frame}: #{differences.size} pixels differ, first at #{x},#{y}: " \
       "render #{a.inspect}, Aseprite #{b.inspect}"
  false
end

abort "usage: ruby compare.rb ASEPRITE RENDER WORK_DIR FILE..." if ARGV.size < 4
aseprite, render, work, *files = ARGV
failed = files.each_with_index.sum { |file, index| compare(aseprite, render, work, file, index) }
puts "#{files.size} files, #{failed} frames differ"
exit(failed.zero? ? 0 : 1)

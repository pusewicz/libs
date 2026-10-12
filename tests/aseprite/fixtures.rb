# frozen_string_literal: true

# Writes the Aseprite test files that tests/aseprite/test_aseprite.c reads.
#
# Usage: ruby tests/aseprite/fixtures.rb OUTPUT_DIR

require "fileutils"
require "zlib"

# Encodes the parts of an Aseprite file.
module Ase
  module_function

  def byte(value) = [value].pack("C")
  def word(value) = [value].pack("v")
  def short(value) = [value].pack("s<")
  def dword(value) = [value].pack("V")
  def long(value) = [value].pack("l<")
  def qword(value) = [value].pack("Q<")
  def long64(value) = [value].pack("q<")
  def zeros(count) = "\0".b * count
  def string(text) = word(text.bytesize) + text.b

  # Compresses data as a zlib stream.
  def zlib(data, level: Zlib::BEST_COMPRESSION, strategy: Zlib::DEFAULT_STRATEGY, window: Zlib::MAX_WBITS)
    deflate = Zlib::Deflate.new(level, window, 9, strategy)
    compressed = deflate.deflate(data, Zlib::FINISH)
    deflate.close
    compressed
  end

  def chunk(type, data) = dword(data.bytesize + 6) + word(type) + data.b

  # Encodes a frame. The chunk counts can be set to test the header fields.
  def frame(chunks, duration: 100, old_count: nil, new_count: nil)
    body = chunks.join.b
    count = chunks.size
    dword(body.bytesize + 16) + word(0xF1FA) + word(old_count || [count, 0xFFFF].min) +
      word(duration) + zeros(2) + dword(new_count || count) + body
  end

  # Encodes a whole file from encoded frames.
  def file(frames, width: 4, height: 3, depth: 32, flags: 1, speed: 100, transparent: 0,
           colors: 0, pixel_width: 1, pixel_height: 1, grid: [0, 0, 16, 16], frame_count: nil)
    body = frames.join.b
    header = word(0xA5E0) + word(frame_count || frames.size) + word(width) + word(height) +
             word(depth) + dword(flags) + word(speed) + zeros(8) + byte(transparent) + zeros(3) +
             word(colors) + byte(pixel_width) + byte(pixel_height) + short(grid[0]) +
             short(grid[1]) + word(grid[2]) + word(grid[3]) + zeros(84)
    dword(4 + header.bytesize + body.bytesize) + header + body
  end

  def layer(name, type: 0, flags: 3, level: 0, blend: 0, opacity: 255, tileset: nil, uuid: nil)
    data = word(flags) + word(type) + word(level) + word(0) + word(0) + word(blend) +
           byte(opacity) + zeros(3) + string(name)
    data += dword(tileset) if tileset
    data += uuid.b if uuid
    chunk(0x2004, data)
  end

  def cel_header(layer, type, x: 0, y: 0, opacity: 255, z_index: 0)
    word(layer) + short(x) + short(y) + byte(opacity) + word(type) + short(z_index) + zeros(5)
  end

  def image_cel(layer, width, height, pixels, raw: false, zlib_options: {}, **header)
    data = word(width) + word(height) + (raw ? pixels.b : zlib(pixels, **zlib_options))
    chunk(0x2005, cel_header(layer, raw ? 0 : 2, **header) + data)
  end

  def linked_cel(layer, frame, **header)
    chunk(0x2005, cel_header(layer, 1, **header) + word(frame))
  end

  def tilemap_cel(layer, width, height, tiles, bits: 32, masks: [0x1fffffff, 0x20000000, 0x40000000, 0x80000000],
                  **header)
    format = { 8 => "C*", 16 => "v*", 32 => "V*" }.fetch(bits)
    data = word(width) + word(height) + word(bits) + masks.map { dword(_1) }.join + zeros(10) +
           zlib(tiles.pack(format))
    chunk(0x2005, cel_header(layer, 3, **header) + data)
  end

  def cel_extra(flags, x, y, width, height)
    chunk(0x2006, dword(flags) + long(x) + long(y) + long(width) + long(height) + zeros(16))
  end

  def color_profile(type, flags: 0, gamma: 0, icc: nil)
    data = word(type) + word(flags) + long(gamma) + zeros(8)
    data += dword(icc.bytesize) + icc.b if icc
    chunk(0x2007, data)
  end

  def external_files(entries)
    data = dword(entries.size) + zeros(8)
    entries.each { |id, type, name| data += dword(id) + byte(type) + zeros(7) + string(name) }
    chunk(0x2008, data)
  end

  def old_palette(packets, type: 0x0004)
    data = word(packets.size)
    packets.each do |skip, colors|
      data += byte(skip) + byte(colors.size == 256 ? 0 : colors.size)
      colors.each { |rgb| data += rgb.pack("C3") }
    end
    chunk(type, data)
  end

  # Encodes a palette chunk. Entries are [r, g, b, a] or [r, g, b, a, name].
  def palette(size, first, entries)
    data = dword(size) + dword(first) + dword(first + entries.size - 1) + zeros(8)
    entries.each do |r, g, b, a, name|
      data += word(name ? 1 : 0) + [r, g, b, a].pack("C4")
      data += string(name) if name
    end
    chunk(0x2019, data)
  end

  # Encodes tags. Each tag is [from, to, direction, repeat, rgb, name].
  def tags(list)
    data = word(list.size) + zeros(8)
    list.each do |from, to, direction, repeat, rgb, name|
      data += word(from) + word(to) + byte(direction) + word(repeat) + zeros(6) + rgb.pack("C3") +
              zeros(1) + string(name)
    end
    chunk(0x2018, data)
  end

  PROPERTY_TYPES = {
    bool: 1, int8: 2, uint8: 3, int16: 4, uint16: 5, int32: 6, uint32: 7, int64: 8, uint64: 9,
    fixed: 10, float: 11, double: 12, string: 13, point: 14, size: 15, rect: 16, vector: 17,
    map: 18, uuid: 19
  }.freeze

  # Encodes a property value. A value is [type, data].
  def value(type, data)
    case type
    when :bool then byte(data ? 1 : 0)
    when :int8 then [data].pack("c")
    when :uint8 then byte(data)
    when :int16 then short(data)
    when :uint16 then word(data)
    when :int32, :fixed then long(data)
    when :uint32 then dword(data)
    when :int64 then long64(data)
    when :uint64 then qword(data)
    when :float then [data].pack("e")
    when :double then [data].pack("E")
    when :string then string(data)
    when :point, :size, :rect then data.map { long(_1) }.join
    when :vector then vector(*data)
    when :map then properties(data)
    when :uuid then data.b
    when Integer then data.b
    end
  end

  def type_code(type) = type.is_a?(Integer) ? type : PROPERTY_TYPES.fetch(type)

  # Encodes a vector. A nil item type means mixed items of [type, data].
  def vector(item_type, items)
    data = dword(items.size) + word(item_type ? type_code(item_type) : 0)
    items.each do |item|
      data += item_type ? value(item_type, item) : word(type_code(item[0])) + value(*item)
    end
    data
  end

  # Encodes a map of name => [type, data].
  def properties(map)
    dword(map.size) + map.map { |name, (type, data)| string(name) + word(type_code(type)) + value(type, data) }.join
  end

  # Encodes a user data chunk. maps is a hash of key => properties.
  def user_data(text: nil, color: nil, maps: nil)
    flags = (text ? 1 : 0) | (color ? 2 : 0) | (maps ? 4 : 0)
    data = dword(flags)
    data += string(text) if text
    data += color.pack("C4") if color
    if maps
      body = dword(maps.size) + maps.map { |key, map| dword(key) + properties(map) }.join
      data += dword(body.bytesize + 4) + body
    end
    chunk(0x2020, data)
  end

  # Encodes a slice. Each key is [frame, x, y, w, h, center, pivot].
  def slice(name, flags, keys)
    data = dword(keys.size) + dword(flags) + dword(0) + string(name)
    keys.each do |frame, x, y, width, height, center, pivot|
      data += dword(frame) + long(x) + long(y) + dword(width) + dword(height)
      data += long(center[0]) + long(center[1]) + dword(center[2]) + dword(center[3]) if flags & 1 != 0
      data += long(pivot[0]) + long(pivot[1]) if flags & 2 != 0
    end
    chunk(0x2022, data)
  end

  def tileset(id, flags, count, width, height, name, pixels: nil, external: nil, base: 1)
    data = dword(id) + dword(flags) + dword(count) + word(width) + word(height) + short(base) +
           zeros(14) + string(name)
    data += dword(external[0]) + dword(external[1]) if external
    if pixels
      compressed = zlib(pixels)
      data += dword(compressed.bytesize) + compressed
    end
    chunk(0x2023, data)
  end
end

# Builds every test file.
module Fixtures
  module_function

  extend Ase

  def rgba_pixels(count, seed) = Array.new(count) { [(seed * 7 + _1 * 13) % 256, _1 * 3 % 256, seed, 255] }.flatten.pack("C*")

  # RGBA, groups, all image cel types, cel extra, sprite user data.
  def rgba
    frame0 = [
      color_profile(1),
      palette(3, 0, [[255, 0, 0, 255], [0, 255, 0, 128, "green"], [0, 0, 255, 255]]),
      user_data(text: "sprite", color: [1, 2, 3, 4]),
      layer("group", type: 1, flags: 1),
      layer("body", level: 1, blend: 1, opacity: 128),
      user_data(text: "body layer"),
      layer("background", flags: 11, blend: 2, opacity: 64),
      image_cel(1, 2, 2, rgba_pixels(4, 1), x: 1, y: -1, opacity: 200, z_index: -2),
      user_data(text: "body cel"),
      cel_extra(1, 0x18000, 0x8000, 0x20000, 0x20000),
      image_cel(2, 4, 3, rgba_pixels(12, 2), raw: true)
    ]
    frame1 = [
      linked_cel(1, 0, x: 3, y: 2),
      cel_extra(1, 0x30000, 0x20000, 0x10000, 0x10000),
      image_cel(2, 4, 3, rgba_pixels(12, 3), zlib_options: { level: 0 })
    ]
    frame2 = [
      image_cel(1, 3, 3, rgba_pixels(9, 4), zlib_options: { strategy: Zlib::FIXED }),
      image_cel(2, 4, 3, rgba_pixels(12, 5), zlib_options: { strategy: Zlib::HUFFMAN_ONLY })
    ]
    file([frame(frame0, duration: 50), frame(frame1, duration: 0), frame(frame2)],
         speed: 120, pixel_width: 2, pixel_height: 1, grid: [-3, 4, 8, 6])
  end

  def grayscale
    file([frame([layer("gray"), image_cel(0, 2, 1, [10, 255, 20, 128].pack("C*"))])], width: 2, height: 1,
                                                                                        depth: 16, colors: 4)
  end

  # Indexed palettes: old chunks, a 6 bit chunk, a new chunk and a change per frame.
  def indexed
    frame0 = [
      old_palette([[0, [[1, 2, 3], [4, 5, 6]]], [1, [[7, 8, 9]]]]),
      user_data(text: "indexed sprite"),
      layer("index"),
      image_cel(0, 2, 2, [0, 1, 3, 5].pack("C*"))
    ]
    frame1 = [old_palette([[0, [[63, 32, 0]]]], type: 0x000B)]
    frame3 = [
      palette(6, 5, [[10, 20, 30, 40, "last"]]),
      old_palette([[0, [[99, 99, 99]]]])
    ]
    file([frame(frame0), frame(frame1), frame([]), frame(frame3)], depth: 8, transparent: 5, colors: 2)
  end

  # A tileset with user data per tile and tilemap cels of 32, 16 and 8 bits.
  def tilemap
    tiles = rgba_pixels(2 * 2 * 3, 6)
    frame0 = [
      external_files([[7, 1, "tiles.aseprite"], [9, 2, "pub/ext"]]),
      tileset(4, 2 | 4, 3, 2, 2, "ground", pixels: tiles),
      user_data(text: "tileset"),
      user_data,
      user_data(text: "tile 1"),
      user_data(text: "tile 2"),
      user_data(text: "not a tile"),
      tileset(5, 1, 10, 8, 8, "far", external: [7, 2], base: 0),
      layer("map", type: 2, tileset: 4),
      tilemap_cel(0, 2, 1, [1, 0x20000002])
    ]
    frame1 = [tilemap_cel(0, 1, 2, [2, 0x4001], bits: 16, masks: [0x0fff, 0x1000, 0x2000, 0x4000])]
    frame2 = [tilemap_cel(0, 1, 1, [0x83], bits: 8, masks: [0x1f, 0x20, 0x40, 0x80])]
    file([frame(frame0), frame(frame1), frame(frame2), frame([linked_cel(0, 0)])])
  end

  def tagged
    frame0 = [
      tags([[0, 1, 0, 0, [1, 2, 3], "walk"], [1, 2, 7, 3, [4, 5, 6], "run"]]),
      user_data(color: [10, 20, 30, 255]),
      user_data(text: "run tag"),
      user_data(text: "no tag"),
      layer("layer"),
      tags([[2, 2, 1, 1, [0, 0, 0], "idle"]]),
      user_data(text: "idle tag")
    ]
    file([frame(frame0), frame([]), frame([])])
  end

  def sliced
    frame0 = [
      slice("plain", 0, [[0, -1, -2, 3, 4]]),
      user_data(text: "plain slice"),
      slice("patch", 3, [[0, 1, 2, 10, 20, [2, 3, 4, 5], [6, 7]], [1, 2, 3, 11, 21, [3, 4, 5, 6], [8, 9]]])
    ]
    file([frame(frame0), frame([])])
  end

  def all_properties
    {
      "bool" => [:bool, true], "int8" => [:int8, -8], "uint8" => [:uint8, 200], "int16" => [:int16, -1600],
      "uint16" => [:uint16, 60_000], "int32" => [:int32, -320_000], "uint32" => [:uint32, 4_000_000_000],
      "int64" => [:int64, -64_000_000_000], "uint64" => [:uint64, 18_000_000_000_000_000_000],
      "fixed" => [:fixed, 0x18000], "float" => [:float, 1.5], "double" => [:double, -2.25],
      "string" => [:string, "text"], "point" => [:point, [1, -2]], "size" => [:size, [3, 4]],
      "rect" => [:rect, [5, 6, 7, 8]], "ints" => [:vector, [:int32, [1, 2, 3]]],
      "mixed" => [:vector, [nil, [[:string, "a"], [:bool, false]]]],
      "nested" => [:map, { "inner" => [:uint8, 9] }], "uuid" => [:uuid, (1..16).to_a.pack("C*")]
    }
  end

  def nested_vector(depth) = depth.zero? ? [:int8, 1] : [:vector, [nil, [nested_vector(depth - 1)]]]

  def property_file
    frame0 = [
      external_files([[3, 2, "pub/ext"]]),
      user_data(maps: { 0 => all_properties, 3 => { "x" => [:int16, 1] } }),
      layer("unknown type"),
      user_data(text: "kept", maps: { 0 => { "bad" => [0x77, "\x01"] } }),
      layer("deep"),
      user_data(maps: { 0 => { "deep" => nested_vector(128) } }),
      layer("deep enough"),
      user_data(maps: { 0 => { "deep" => nested_vector(127) } })
    ]
    file([frame(frame0)])
  end

  def uuid_file
    uuid = (100..115).to_a.pack("C*")
    frame0 = [
      color_profile(2, flags: 1, gamma: 0x10000, icc: "ICCDATA"),
      layer("first", uuid: uuid),
      layer("second", type: 1, blend: 3, opacity: 77, uuid: uuid.reverse)
    ]
    file([frame(frame0)], flags: 7)
  end

  # Chunk counts in the frame header, chunks and cels that the reader skips.
  def skipped
    mask = chunk(0x2016, short(0) + short(0) + word(1) + word(1) + zeros(8) + string("mask") + byte(0x80))
    frame0 = [
      layer("layer"),
      chunk(0x7777, "unknown"),
      mask,
      image_cel(0, 0, 0, ""),
      user_data(text: "lost"),
      chunk(0x2005, cel_header(0, 9) + "future"),
      image_cel(0, 1, 1, [1, 2, 3, 4].pack("C*"), z_index: 5)
    ]
    frame1 = [image_cel(0, 1, 1, [5, 6, 7, 8].pack("C*"))]
    file([frame(frame0, old_count: 0xFFFF), frame(frame1, new_count: 0)])
  end

  # A cel of 160000 bytes. The loader puts it in a block of its own.
  def large
    file([frame([layer("layer"), image_cel(0, 200, 200, zeros(200 * 200 * 4))])], width: 200, height: 200)
  end

  def valid
    {
      "rgba" => rgba, "grayscale" => grayscale, "indexed" => indexed, "tilemap" => tilemap,
      "tags" => tagged, "slices" => sliced, "properties" => property_file, "uuid" => uuid_file, "skipped" => skipped,
      "large" => large
    }
  end

  def with_cel(chunks, frames: 1)
    file([frame([layer("layer")] + chunks)] + Array.new(frames - 1) { frame([]) })
  end

  def patch(data, offset, bytes)
    copy = data.dup
    copy[offset, bytes.bytesize] = bytes
    copy
  end

  def invalid
    base = with_cel([image_cel(0, 1, 1, [1, 2, 3, 4].pack("C*"))])
    bad_checksum = base.dup
    bad_checksum[-1] = (bad_checksum[-1].ord ^ 1).chr
    first_chunk = 128 + 16
    {
      "not_aseprite" => "This is not an Aseprite file at all, it is just some text.".b * 4,
      "short" => base[0, 5],
      "truncated_header" => base[0, 64],
      "truncated_frame" => base[0...-1],
      "bad_depth" => patch(base, 12, word(24)),
      "zero_width" => patch(base, 8, word(0)),
      "zero_frames" => patch(base, 6, word(0)),
      "many_frames" => patch(base, 6, word(500)),
      "bad_frame_magic" => patch(base, 132, word(0x1234)),
      "small_chunk" => patch(base, first_chunk, dword(4)),
      "big_chunk" => patch(base, first_chunk, dword(5000)),
      "bad_checksum" => bad_checksum,
      "truncated_zlib" => with_cel([chunk(0x2005, cel_header(0, 2) + word(1) + word(1) + Ase.zlib("abcd")[0, 6])]),
      "bad_layer" => with_cel([image_cel(5, 1, 1, "abcd")]),
      "link_forward" => with_cel([linked_cel(0, 0)]),
      "link_missing" => file([frame([layer("a"), layer("b"), image_cel(0, 1, 1, "abcd")]), frame([linked_cel(1, 0)])]),
      "tilemap_on_image" => with_cel([tilemap_cel(0, 1, 1, [1])]),
      "image_on_group" => file([frame([layer("group", type: 1), image_cel(0, 1, 1, "abcd")])]),
      "child_of_image" => file([frame([layer("image"), layer("child", level: 1)])]),
      "missing_tileset" => file([frame([layer("map", type: 2, tileset: 3)])]),
      "tag_range" => with_cel([tags([[0, 4, 0, 0, [0, 0, 0], "late"]])]),
      "big_palette" => with_cel([chunk(0x2019, dword(70_000) + dword(1) + dword(0) + zeros(8))]),
      "palette_range" => with_cel([palette(2, 1, [[1, 1, 1, 1], [2, 2, 2, 2]])]),
      "old_palette_range" => with_cel([old_palette([[200, Array.new(100) { [0, 0, 0] }]])]),
      "bomb" => with_cel([chunk(0x2005, cel_header(0, 2) + word(65_535) + word(65_535) + Ase.zlib("\0" * 1000))]),
      "many_tags" => with_cel([chunk(0x2018, word(1000) + zeros(8) + zeros(30))]),
      "many_keys" => with_cel([chunk(0x2022, dword(100_000) + dword(0) + dword(0) + string("s"))])
    }
  end

  def write(directory)
    FileUtils.mkdir_p(directory)
    valid.merge(invalid).each do |name, data|
      File.binwrite(File.join(directory, "#{name}.aseprite"), data)
    end
    write_inflate(File.join(directory, "inflate"))
  end

  # Raw data and zlib streams for the inflate tests.
  def write_inflate(directory)
    FileUtils.mkdir_p(directory)
    text = (Array.new(4000) { |i| "line #{i * 7 % 113} of the text #{i % 17}\n" }.join).b
    noise = Random.new(42).bytes(70_000)
    streams = {
      "empty" => ["".b, {}],
      "stored" => [noise, { level: 0 }],
      "fast" => [text, { level: 1 }],
      "best" => [text, { level: 9 }],
      "fixed" => [text, { strategy: Zlib::FIXED }],
      "huffman" => [noise + text, { strategy: Zlib::HUFFMAN_ONLY }],
      "rle" => [text, { strategy: Zlib::RLE }],
      "window" => [text, { window: 9 }],
      "mixed" => [noise[0, 5000] + text + noise[0, 3000] + text, {}]
    }
    streams.each do |name, (raw, options)|
      File.binwrite(File.join(directory, "#{name}.raw"), raw)
      File.binwrite(File.join(directory, "#{name}.z"), Ase.zlib(raw, **options))
    end
  end
end

abort "usage: ruby fixtures.rb OUTPUT_DIR" unless ARGV.size == 1
Fixtures.write(ARGV[0])

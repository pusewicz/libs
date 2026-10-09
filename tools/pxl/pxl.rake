# frozen_string_literal: true

# Tasks that generate parts of pxl.h, and the shaders of its tests and
# examples.

require_relative "font"
require_relative "shaders"

module Pxl
  HEADER = "pxl.h"
  TOOLS_DIR = "tools/pxl"
  WORK_DIR = File.join(BUILD, "pxl", "shaders")
  CACHE_DIR = File.join(BUILD, "tools")

  # Shader headers for the tests and the examples: output => sources.
  SHADER_HEADERS = {
    "tests/pxl/test_shaders.h" => FileList["tests/pxl/shaders/*.frag"],
    "examples/pxl/example_shaders.h" => FileList["examples/pxl/shaders/*.frag"],
  }.freeze

  module_function

  # Replaces the text between the BEGIN and END markers of a section in pxl.h.
  def splice(section, text)
    header = File.read(HEADER)
    pattern = %r{(// BEGIN GENERATED #{section}\n).*?(// END GENERATED #{section}\n)}m
    abort("#{HEADER} has no generated section '#{section}'") unless header.match?(pattern)
    File.write(HEADER, header.sub(pattern) { "#{Regexp.last_match(1)}#{text}#{Regexp.last_match(2)}" })
  end

  def dxc
    @dxc ||= PxlShaders.dxc_command(CACHE_DIR, WORK_DIR)
  end

  # Returns the DXC of "rake pxl:generate" for the banner. It does not
  # download DXC.
  def describe_dxc
    user = PxlShaders.user_dxc
    return describe(user.shelljoin) if user

    release = PxlShaders.release_dxc(CACHE_DIR)
    version = "release #{PxlShaders::DXC_VERSION}, #{File.exist?(release) ? release : 'downloads on first use'}"
    case PxlShaders.release_mode
    when :native then version
    when :docker then "#{version}, runs in Docker: #{describe('docker')}"
    else "not found"
    end
  end
end

%w[glslc spirv-cross spirv-val].each { |tool| BANNER_ROWS[tool] = -> { describe(tool) } }
BANNER_ROWS["DXC"] = -> { Pxl.describe_dxc }

namespace :pxl do
  desc "Compile the built-in shaders and embed them in pxl.h"
  task :shaders do
    sources = FileList[File.join(Pxl::TOOLS_DIR, "shaders", "*.{vert,frag}")].sort
    arrays = sources.map do |source|
      PxlShaders.c_arrays("pxl__#{PxlShaders.c_name(source)}", PxlShaders.compile(source, Pxl::WORK_DIR, Pxl.dxc))
    end
    Pxl.splice("SHADERS", "// clang-format off\n#{arrays.join}// clang-format on\n")
  end

  desc "Embed the built-in font in pxl.h"
  task :font do
    Pxl.splice("FONT", PxlFont.generate(File.join(Pxl::TOOLS_DIR, "font.txt")))
  end

  desc "Compile the shaders of the tests and the examples"
  task :custom_shaders do
    Pxl::SHADER_HEADERS.each do |output, sources|
      next if sources.empty?

      shaders = sources.sort.to_h do |source|
        [PxlShaders.c_name(source), PxlShaders.compile(source, Pxl::WORK_DIR, Pxl.dxc)]
      end
      File.write(output, PxlShaders.header(shaders))
    end
  end

  desc "Generate all parts of pxl.h and the shaders of its tests and examples"
  task generate: %i[shaders font custom_shaders]
end

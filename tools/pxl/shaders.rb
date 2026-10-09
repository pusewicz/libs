# frozen_string_literal: true

# Compiles GLSL fragment shaders for pxl.h to SPIR-V, MSL and DXIL, and
# writes them as C arrays.
#
#   ruby tools/pxl/shaders.rb -o my_shaders.h wave.frag palette.frag
#
# The header has one pxl_shader_desc for each shader, for example
# `wave_frag`. Include pxl.h before it:
#
#   pxl_shader* wave = pxl_create_shader(pxl, &wave_frag);
#
# It needs glslc, spirv-val and spirv-cross in PATH, and dxc in PATH or in
# $DXC. If there is no dxc, it downloads the DXC release for Linux x86_64.
# It runs the release directly on Linux x86_64, and in Docker on macOS,
# because Microsoft has no DXC for macOS.

require "digest"
require "fileutils"
require "json"
require "open3"
require "shellwords"
require "tmpdir"

module PxlShaders
  DXC_VERSION = "v1.9.2609"
  DXC_ARCHIVE = "linux_dxc_2026_09_28.x86_x64.tar.gz"
  DXC_SHA256 = "96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1"
  DXC_DOCKER_IMAGE = "ubuntu:24.04"
  MAX_TEXTURES = 4
  FRAGMENT_INPUTS = { 0 => "vec2", 1 => "vec4", 2 => "vec4", 3 => "vec4" }.freeze

  module_function

  # Runs a command. Stops the program if the command fails.
  def run(*command)
    output, status = Open3.capture2e(*command)
    abort("#{command.shelljoin}\n#{output}") unless status.success?
    output
  end

  # Returns the absolute path of a program in PATH, or nil.
  def which(name)
    program = "#{name}#{RbConfig::CONFIG['EXEEXT']}"
    path = ENV.fetch("PATH", "").split(File::PATH_SEPARATOR).map { |dir| File.join(dir, program) }
              .find { |candidate| File.file?(candidate) && File.executable?(candidate) }
    path && File.expand_path(path)
  end

  # Returns the command of the DXC that the user gives: $DXC, or dxc in
  # PATH. Returns nil if there is none.
  def user_dxc
    return ENV["DXC"].shellsplit if ENV["DXC"] && !ENV["DXC"].empty?

    dxc = which("dxc")
    [dxc] if dxc
  end

  # Returns how this system runs the DXC release for Linux x86_64: :native
  # on Linux x86_64, :docker on macOS, or nil.
  def release_mode
    if RUBY_PLATFORM.include?("x86_64-linux")
      :native
    elsif RUBY_PLATFORM.include?("darwin")
      :docker
    end
  end

  # Returns the path of the DXC release in `cache_dir`.
  def release_dxc(cache_dir)
    File.join(File.expand_path(cache_dir), "dxc", "bin", "dxc")
  end

  # Returns the command that runs DXC: the DXC of the user, else the DXC
  # release in `cache_dir`. It downloads the release once. On macOS, the
  # release runs in Docker, with `work_dir` mounted.
  def dxc_command(cache_dir, work_dir)
    user = user_dxc
    return user if user

    mode = release_mode
    abort("dxc not found. Put dxc in PATH or set DXC.") unless mode
    release = release_dxc(cache_dir)
    download_dxc(File.dirname(release, 2)) unless File.exist?(release)
    return [release] if mode == :native

    abort("dxc not found. On macOS, DXC runs in Docker: install Docker, or set DXC.") unless which("docker")
    work = File.expand_path(work_dir)
    FileUtils.mkdir_p(work)
    ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", "#{File.dirname(release, 2)}:/dxc:ro",
     "-v", "#{work}:#{work}", DXC_DOCKER_IMAGE, "/dxc/bin/dxc"]
  end

  # Downloads the DXC release into `dir`, and checks it.
  def download_dxc(dir)
    FileUtils.mkdir_p(dir)
    archive = File.join(dir, DXC_ARCHIVE)
    url = "https://github.com/microsoft/DirectXShaderCompiler/releases/download/#{DXC_VERSION}/#{DXC_ARCHIVE}"
    run("curl", "-fsSL", "-o", archive, url)
    abort("#{archive}: the SHA-256 is not #{DXC_SHA256}") unless Digest::SHA256.file(archive).hexdigest == DXC_SHA256
    run("tar", "-xzf", archive, "-C", dir)
  end

  # Checks that a shader binds its resources where SDL GPU wants them.
  def check_bindings(source, reflection, stage)
    texture_set = stage == "vs" ? 0 : 2
    uniform_set = stage == "vs" ? 1 : 3
    textures = reflection.fetch("textures", [])
    uniforms = reflection.fetch("ubos", [])
    textures.each do |texture|
      next if texture["set"] == texture_set && texture["binding"] < textures.size

      abort("#{source}: put the textures in set #{texture_set}, bindings 0 to #{textures.size - 1}")
    end
    uniforms.each do |ubo|
      next if ubo["set"] == uniform_set && ubo["binding"] < uniforms.size

      abort("#{source}: put the uniform buffers in set #{uniform_set}, bindings 0 to #{uniforms.size - 1}")
    end
    abort("#{source}: use at most #{MAX_TEXTURES} textures") if textures.size > MAX_TEXTURES
    abort("#{source}: use at most one uniform buffer") if stage == "ps" && uniforms.size > 1
    check_inputs(source, reflection) if stage == "ps"
    { textures: textures.size, uniforms: uniforms.size }
  end

  # Checks that a fragment shader declares the outputs of the pxl vertex
  # shader. Direct3D 12 links the stages by register, so all must be there.
  def check_inputs(source, reflection)
    inputs = reflection.fetch("inputs", []).to_h { |input| [input["location"], input["type"]] }
    return if inputs == FRAGMENT_INPUTS

    abort("#{source}: declare the inputs of tools/pxl/shaders/sprite.frag: " \
          "#{FRAGMENT_INPUTS.map { |location, type| "#{type} at location #{location}" }.join(', ')}")
  end

  # Compiles a .vert or .frag GLSL file. Returns the blobs and the counts of
  # textures and uniform buffers.
  def compile(source, work_dir, dxc)
    FileUtils.mkdir_p(work_dir)
    stage = File.extname(source) == ".vert" ? "vs" : "ps"
    base = File.expand_path(File.join(work_dir, File.basename(source)))
    spirv = "#{base}.spv"
    run("glslc", "--target-env=vulkan1.0", "-o", spirv, source)
    run("spirv-val", spirv)
    counts = check_bindings(source, JSON.parse(run("spirv-cross", spirv, "--reflect")), stage)
    run("spirv-cross", spirv, "--msl", "--msl-version", "20100", "--msl-decoration-binding",
        "--output", "#{base}.msl")
    run("spirv-cross", spirv, "--hlsl", "--shader-model", "60", "--output", "#{base}.hlsl")
    run(*dxc, "-T", "#{stage}_6_0", "-E", "main", "-Qstrip_debug", "-Qstrip_reflect",
        "-Fo", "#{base}.dxil", "#{base}.hlsl")
    {
      spirv: File.binread(spirv),
      msl: File.binread("#{base}.msl"),
      dxil: File.binread("#{base}.dxil"),
      **counts,
    }
  end

  # Returns the C name of a shader file: wave.frag -> wave_frag.
  def c_name(source)
    File.basename(source).gsub(/[^A-Za-z0-9]/, "_")
  end

  def c_array(name, bytes)
    lines = bytes.bytes.each_slice(12).map { |line| "  #{line.map { |b| format('0x%02x', b) }.join(', ')}," }
    "static const uint8_t #{name}[] = {\n#{lines.join("\n")}\n};\n"
  end

  # Returns the C arrays of the blobs of a shader.
  def c_arrays(prefix, shader)
    %i[spirv msl dxil].map { |format| c_array("#{prefix}_#{format}", shader[format]) }.join
  end

  # Returns a header with the arrays and a pxl_shader_desc for each shader.
  def header(shaders)
    body = shaders.map do |name, shader|
      <<~C
        #{c_arrays(name, shader)}
        static const pxl_shader_desc #{name} = {
          .spirv = {.code = #{name}_spirv, .size = sizeof #{name}_spirv},
          .dxil = {.code = #{name}_dxil, .size = sizeof #{name}_dxil},
          .msl = {.code = #{name}_msl, .size = sizeof #{name}_msl},
          .num_textures = #{[shader[:textures], 1].max},
          .has_uniforms = #{shader[:uniforms].positive?},
        };
      C
    end
    <<~C
      // Generated by tools/pxl/shaders.rb. Do not edit.
      // Include pxl.h before this file.

      // clang-format off
      #{body.join("\n")}// clang-format on
    C
  end
end

if $PROGRAM_NAME == __FILE__
  output = nil
  sources = []
  args = ARGV.dup
  until args.empty?
    arg = args.shift
    if arg == "-o"
      output = args.shift
    else
      sources << arg
    end
  end
  abort("usage: ruby #{__FILE__} -o OUTPUT.h SHADER.frag...") if output.nil? || sources.empty?

  work_dir = File.join(Dir.tmpdir, "pxl-shaders-#{Process.pid}")
  dxc = PxlShaders.dxc_command(File.join(Dir.home, ".cache", "pxl"), work_dir)
  shaders = sources.to_h { |source| [PxlShaders.c_name(source), PxlShaders.compile(source, work_dir, dxc)] }
  File.write(output, PxlShaders.header(shaders))
  FileUtils.rm_rf(work_dir)
end

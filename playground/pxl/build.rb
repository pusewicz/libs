# frozen_string_literal: true

# Builds the pxl playground into build/playground/site/pxl. The site is
# static: GitHub Pages can host it.
#
# The playground has three parts:
#
# - The host (host.c): SDL, pxl.h and the SDL GPU API on WebGL 2 (gpu.c).
#   Emscripten builds it as a main module.
# - The compiler: Clang and LLD in WebAssembly (YoWASP). It compiles the code
#   of the editor into a side module for the host.
# - The page: the editor, the preview frame and the output.
#
# It needs Emscripten (emcc in PATH), Node.js with npm, and git. The Rake
# tasks are in rakelib/pxl.rake.

require "fileutils"
require "json"
require "open3"
require "pathname"
require "rubygems/package"
require "zlib"

module PxlPlayground
  # run() of the Rakefile prints with the output methods of Rake.
  extend Rake::FileUtilsExt

  SOURCE = "playground/pxl"
  WORK = "build/playground"
  SITE = "#{WORK}/site".freeze
  OUTPUT = "#{SITE}/pxl".freeze
  TESTS = "#{WORK}/tests".freeze
  NODE_MODULES = "#{SOURCE}/node_modules".freeze
  CLANG = "#{NODE_MODULES}/@yowasp/clang/gen".freeze

  SPIRV_CROSS_URL = "https://github.com/KhronosGroup/SPIRV-Cross.git"
  SPIRV_CROSS_TAG = "vulkan-sdk-1.4.363.0"
  SPIRV_CROSS_FILES = %w[spirv_cfg spirv_cross spirv_cross_c spirv_cross_parsed_ir spirv_glsl spirv_parser].freeze

  EXAMPLES = %w[hello demo shader].freeze
  # The headers of the programs, besides libc and SDL: path in /pxl/include =>
  # the file in the repository.
  HEADERS = {
    "pxl.h" => "#{SOURCE}/include/pxl.h",
    "pxl/pxl.h" => "pxl.h",
    "art.h" => "examples/pxl/art.h",
    "example_shaders.h" => "examples/pxl/example_shaders.h",
  }.freeze
  # The directories of the Emscripten headers that C programs of the
  # playground do not need.
  SKIPPED_HEADERS = %w[c++ SDL X11 sanitizer GLFW].freeze
  # The Clang headers for other processors.
  SKIPPED_CLANG_HEADERS = /\Ainclude\/(opencl|altivec|hexagon|hvx_|__clang_(cuda|hip|spirv)|riscv_|arm_|vecintrin|
    htm|amx|avx|lasx|lsx|velintrin|s390|msa|ppc|hlsl)/x
  # Programs fail to load if the host does not export these.
  REQUIRED_EXPORTS = %w[printf puts putchar fputc qsort rand malloc free sinf __wasm_setjmp SDL_Init
                        SDL_CreateGPUDevice pxl_create pxl_draw_sprite].freeze

  # The flags of the host and of the test programs. The GPU layer needs wasm
  # exceptions for SPIRV-Cross.
  HOST_FLAGS = %w[-O2 -fPIC -fwasm-exceptions].freeze
  LINK_FLAGS = %w[-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB -sEXIT_RUNTIME=1 -sMAX_WEBGL_VERSION=2
                  -sMIN_WEBGL_VERSION=2 -sENVIRONMENT=web -sMODULARIZE=1 -sEXPORT_ES6=1].freeze

  module_function

  def check_tools
    missing = %w[emcc em++ emar emnm embuilder em-config node npm git].reject { which(_1) }
    abort "pxl:playground needs Emscripten, Node.js and git. Not found: #{missing.join(', ')}" unless missing.empty?
    FileUtils.mkdir_p(WORK)
  end

  def npm_install
    lock = "#{SOURCE}/package-lock.json"
    installed = "#{NODE_MODULES}/.package-lock.json"
    return if FileUtils.uptodate?(installed, [lock, "#{SOURCE}/package.json"])

    run("npm", "ci", "--prefix", SOURCE, "--no-audit", "--no-fund")
  end

  # Returns the directory of the SPIRV-Cross sources. Clones them once.
  def spirv_cross
    dir = "#{WORK}/SPIRV-Cross-#{SPIRV_CROSS_TAG}"
    unless File.exist?("#{dir}/spirv_cross_c.h")
      FileUtils.rm_rf(dir)
      run("git", "-c", "advice.detachedHead=false", "clone", "--quiet", "--depth", "1", "--branch",
          SPIRV_CROSS_TAG, SPIRV_CROSS_URL, dir)
    end
    dir
  end

  # Returns the output of a command. Stops the build if it fails.
  def capture(*command)
    output, status = Open3.capture2(*command, binmode: true)
    abort "#{command.join(' ')} failed" unless status.success?
    output
  end

  def emscripten_cache
    @emscripten_cache ||= capture("em-config", "CACHE").strip
  end

  # Builds the SDL port of Emscripten as position-independent code and copies
  # it without SDL_gpu.c, which gpu.c replaces. Returns the paths of the
  # archive and of the removed object.
  def sdl_archive
    # The port writes sdl3.pc into this directory, but in a new cache it does
    # not exist yet.
    FileUtils.mkdir_p("#{emscripten_cache}/sysroot/lib/pkgconfig")
    run("embuilder", "build", "sdl3", "--pic")
    source = "#{emscripten_cache}/sysroot/lib/wasm32-emscripten/pic/libSDL3.a"
    archive = "#{WORK}/libSDL3.a"
    removed = "#{WORK}/SDL_gpu.c.o"
    if !FileUtils.uptodate?(archive, [source]) || !File.exist?(removed)
      FileUtils.cp(source, archive)
      File.binwrite(removed, capture("emar", "p", archive, "SDL_gpu.c.o"))
      run("emar", "d", archive, "SDL_gpu.c.o")
    end
    [archive, removed]
  end

  # Compiles the C and C++ files of the host in parallel. Returns the objects.
  def compile_host(spirv_dir)
    objects = "#{WORK}/obj"
    FileUtils.mkdir_p(objects)
    jobs = SPIRV_CROSS_FILES.map do |name|
      source = "#{spirv_dir}/#{name}.cpp"
      ["#{objects}/#{name}.o", source, ["em++", *HOST_FLAGS, "-DSPIRV_CROSS_C_API_GLSL=1"]]
    end
    c_flags = ["emcc", "@compile_flags.txt", *HOST_FLAGS, "--use-port=sdl3", "-isystem", spirv_dir]
    jobs += %w[gpu host].map { |name| ["#{objects}/#{name}.o", "#{SOURCE}/#{name}.c", c_flags] }
    jobs.map do |object, source, command|
      Thread.new do
        inputs = [source, "compile_flags.txt", __FILE__]
        inputs << "pxl.h" if source.end_with?("host.c")
        run(*command, "-c", source, "-o", object) unless FileUtils.uptodate?(object, inputs)
      end
    end.each(&:join)
    jobs.map(&:first)
  end

  # Returns the global symbols that an object defines.
  def defined_symbols(object)
    capture("emnm", "--defined-only", "--extern-only", "--format=just-symbols", object).split
  end

  # Checks that gpu.c defines each public function of the SDL_gpu.c that it
  # replaces. A newer SDL can add GPU functions.
  def check_gpu_functions(gpu_object, sdl_object)
    missing = defined_symbols(sdl_object) - defined_symbols(gpu_object)
    headers = Dir["#{emscripten_cache}/sysroot/include/SDL3/*.h"].map { File.binread(_1) }.join
    public = missing.select { |name| headers.match?(/\b#{Regexp.escape(name)}\b/) }
    abort "#{SOURCE}/gpu.c does not define #{public.join(', ')}" unless public.empty?
  end

  def link_host(objects, archive)
    FileUtils.mkdir_p(OUTPUT)
    return if FileUtils.uptodate?("#{OUTPUT}/host.wasm", [*objects, archive, __FILE__])

    # MAIN_MODULE=1 keeps all of libc and SDL for the programs. Do not add
    # --whole-archive flags: emcc already adds them, and a
    # --no-whole-archive would drop most of libc.
    run("em++", *HOST_FLAGS, *LINK_FLAGS, "-sMAIN_MODULE=1", "-sEXPORT_NAME=createHost",
        "-sEXPORTED_RUNTIME_METHODS=FS", *objects, archive, "-o", "#{OUTPUT}/host.mjs")
  end

  # Writes the exports of the host, one per line. The linker of the
  # playground allows only these undefined symbols.
  def write_exports
    script = "const fs = require('fs');" \
             "const module = new WebAssembly.Module(fs.readFileSync(process.argv[1]));" \
             "fs.writeFileSync(process.argv[2], WebAssembly.Module.exports(module).map((e) => e.name).join('\\n'));"
    exports = "#{WORK}/host.exports"
    run("node", "-e", script, "#{OUTPUT}/host.wasm", exports)
    missing = REQUIRED_EXPORTS - File.read(exports).split
    abort "The host does not export #{missing.join(', ')}" unless missing.empty?
    exports
  end

  # Writes a gzip file of a tar archive. entries: path in the archive => data.
  # The archive has an entry for each directory too: YoWASP needs them.
  def write_tar_gzip(path, entries)
    FileUtils.mkdir_p(File.dirname(path))
    directories = entries.keys.flat_map { |name| Pathname(name).dirname.descend.map(&:to_s) }.uniq - ["."]
    Zlib::GzipWriter.open(path, Zlib::BEST_COMPRESSION) do |gzip|
      Gem::Package::TarWriter.new(gzip) do |tar|
        directories.sort.each { tar.mkdir(_1, 0o755) }
        entries.sort.each do |name, data|
          tar.add_file_simple(name, 0o644, data.bytesize) { _1.write(data) }
        end
      end
    end
  end

  # Writes the headers of the programs and the exports of the host for the
  # compiler.
  def write_include_archive(exports)
    sysroot = "#{emscripten_cache}/sysroot/include"
    entries = Dir.glob("**/*", base: sysroot).filter_map do |name|
      next if SKIPPED_HEADERS.include?(name.split("/").first) || File.directory?("#{sysroot}/#{name}")

      ["sysroot/include/#{name}", File.binread("#{sysroot}/#{name}")]
    end.to_h
    HEADERS.each { |name, file| entries["pxl/include/#{name}"] = File.binread(file) }
    entries["host.exports"] = File.binread(exports)
    write_tar_gzip("#{OUTPUT}/include.tar.gz", entries)
  end

  def gzip_file(source, output)
    Zlib::GzipWriter.open(output, Zlib::BEST_COMPRESSION) { |gzip| gzip.write(File.binread(source)) }
  end

  # Copies Clang. The large files are gzipped, with only the Clang headers
  # for WebAssembly.
  def copy_clang
    dir = "#{OUTPUT}/clang"
    FileUtils.mkdir_p(dir)
    FileUtils.cp(["#{CLANG}/bundle.js", *Dir["#{CLANG}/llvm.core[234].wasm"]], dir)
    core = "#{dir}/llvm.core.wasm.gz"
    gzip_file("#{CLANG}/llvm.core.wasm", core) unless FileUtils.uptodate?(core, ["#{CLANG}/llvm.core.wasm"])
    entries = {}
    File.open("#{CLANG}/llvm-resources.tar", "rb") do |file|
      Gem::Package::TarReader.new(file).each do |entry|
        name = entry.full_name
        next unless entry.file? && name.match?(%r{\Ainclude/[^/]+\z}) && !name.match?(SKIPPED_CLANG_HEADERS)

        entries[name] = entry.read
      end
    end
    write_tar_gzip("#{dir}/llvm-resources.tar.gz", entries)
  end

  # Returns the flags that compile a program of the playground: the language
  # and the warnings of compile_flags.txt, without -Werror.
  def program_flags
    flags = File.readlines("compile_flags.txt", chomp: true).grep(/\A-(std=|W)/) - ["-Werror"]
    %w[--target=wasm32-unknown-emscripten --sysroot=/sysroot -iwithsysroot/include/compat -I/pxl/include
       -O1 -fPIC -fvisibility=default -fwasm-exceptions -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh
       -DSDL_MAIN_NOIMPL -fno-color-diagnostics] + flags
  end

  # The flags that link a program into a side module. Undefined symbols must
  # be exports of the host.
  def link_flags
    %w[-shared --experimental-pic -Bdynamic --unresolved-symbols=report-all
       --allow-undefined-file=host.exports --export=__wasm_call_ctors --stack-first]
  end

  def write_manifest
    compressed = %w[clang/llvm.core.wasm clang/llvm-resources.tar include.tar].to_h do |name|
      [name, File.size("#{OUTPUT}/#{name}.gz")]
    end
    manifest = { compressed: compressed, compile: program_flags, link: link_flags }
    File.write("#{OUTPUT}/manifest.json", "#{JSON.pretty_generate(manifest)}\n")
  end

  def write_pages
    esbuild = "#{NODE_MODULES}/.bin/esbuild"
    run(esbuild, "#{SOURCE}/playground.js", "--bundle", "--format=esm", "--minify", "--target=es2022",
        "--log-level=warning", "--outfile=#{OUTPUT}/playground.js")
    FileUtils.cp(%w[index.html playground.css preview.html preview.js compiler.js].map { "#{SOURCE}/#{_1}" }, OUTPUT)
    FileUtils.mkdir_p("#{OUTPUT}/examples")
    EXAMPLES.each { FileUtils.cp("examples/pxl/#{_1}.c", "#{OUTPUT}/examples/") }
    File.write("#{SITE}/index.html", <<~HTML)
      <!doctype html>
      <meta charset="utf-8" />
      <title>libs</title>
      <meta http-equiv="refresh" content="0; url=pxl/" />
      <a href="pxl/">pxl playground</a>
    HTML
  end

  def build
    check_tools
    npm_install
    archive, removed = sdl_archive
    objects = compile_host(spirv_cross)
    check_gpu_functions("#{WORK}/obj/gpu.o", removed)
    link_host(objects, archive)
    write_include_archive(write_exports)
    copy_clang
    write_manifest
    write_pages
    puts "The playground is in #{OUTPUT}. Run rake pxl:playground:serve to try it."
  end

  # The test programs for the browser: the tests of pxl and of the GPU layer.
  TEST_SOURCES = %W[tests/pxl/test_pxl.c tests/pxl/test_options.c #{SOURCE}/test_gpu.c].freeze

  # Builds a test program for the browser, with the GPU layer of the host.
  # Returns the name of its module.
  def build_test(source, objects, archive)
    name = File.basename(source, ".c")
    object = "#{TESTS}/#{name}.o"
    run("emcc", "@compile_flags.txt", *HOST_FLAGS, "--use-port=sdl3", "-c", source, "-o", object)
    gpu = objects.reject { _1.end_with?("/host.o") }
    run("em++", *HOST_FLAGS, *LINK_FLAGS, "-sEXPORT_NAME=createTest", "-sEXPORTED_RUNTIME_METHODS=ENV",
        object, *gpu, archive, "-o", "#{TESTS}/#{name}.mjs")
    "#{name}.mjs"
  end

  def build_tests
    check_tools
    npm_install
    archive, = sdl_archive
    objects = compile_host(spirv_cross)
    FileUtils.mkdir_p(TESTS)
    FileUtils.cp("#{SOURCE}/test.html", TESTS)
    TEST_SOURCES.map { build_test(_1, objects, archive) }
  end
end

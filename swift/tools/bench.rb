# frozen_string_literal: true

# Runs the C and the Swift benchmark of pxl in turns, and prints the median
# of the runs for each workload, with the ratio of Swift to C.
#
#   ruby swift/tools/bench.rb C_PROGRAM SWIFT_PROGRAM [RUNS]

require "open3"

module PxlBench
  COLUMNS = %w[record end_frame].freeze

  module_function

  # Runs a benchmark program. Returns workload => { column => ms, "work" => [draws, vertices, indices] }.
  def measure(program)
    output, status = Open3.capture2e(program)
    abort("#{program} failed:\n#{output}") unless status.success?
    output.lines.drop(2).to_h do |line|
      name, record, end_frame, *work = line.split
      [name, { "record" => Float(record), "end_frame" => Float(end_frame), "work" => work }]
    end
  end

  def median(values)
    values.sort[values.size / 2]
  end

  def run(c_program, swift_program, runs)
    results = { "C" => [], "Swift" => [] }
    runs.times do
      results["C"] << measure(c_program)
      results["Swift"] << measure(swift_program)
    end
    report(results, runs)
  end

  def report(results, runs)
    puts "Median of #{runs} runs, each the median of its frames, in ms"
    puts format("%-8s %10s %10s %7s %10s %10s %7s", "workload", "C record", "Swift", "ratio",
                "C end", "Swift", "ratio")
    results["C"].first.each_key do |name|
      c, swift = %w[C Swift].map do |language|
        COLUMNS.to_h { |column| [column, median(results[language].map { |run| run[name][column] })] }
      end
      work = results.values.flat_map { |runs_of| runs_of.map { |run| run[name]["work"] } }.uniq
      abort("#{name}: C and Swift did different work: #{work}") unless work.size == 1
      puts format("%-8s %10.3f %10.3f %6.2fx %10.3f %10.3f %6.2fx", name,
                  c["record"], swift["record"], swift["record"] / c["record"],
                  c["end_frame"], swift["end_frame"], swift["end_frame"] / c["end_frame"])
    end
  end
end

if $PROGRAM_NAME == __FILE__
  c_program, swift_program, runs = ARGV
  abort("usage: bench.rb C_PROGRAM SWIFT_PROGRAM [RUNS]") unless c_program && swift_program
  PxlBench.run(c_program, swift_program, Integer(runs || 5))
end

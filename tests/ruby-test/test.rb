# Smoke test for CRuby running on the PS5. Prints the results; run it through websrv with --pipe.
puts "ruby #{RUBY_VERSION} (#{RUBY_PLATFORM}), #{RUBY_DESCRIPTION}"
puts "pid #{Process.pid}"

def check(name)
  result = yield
  puts "ok    #{name}: #{result}"
rescue Exception => e
  puts "FAIL  #{name}: #{e.class}: #{e.message}"
end

check("arithmetic")      { (1..1_000_000).reduce(:+) }
check("string encoding") { "こんにちは".encode("Shift_JIS").bytes.length }
check("marshal")         { Marshal.load(Marshal.dump({ a: [1, 2.5, "x"], b: nil })).inspect }
check("zlib")            { require "zlib"; Zlib::Inflate.inflate(Zlib::Deflate.deflate("hello " * 100)).length }
check("stringio")        { require "stringio"; io = StringIO.new; io.puts("x"); io.string.inspect }
check("time")            { Time.at(0).utc.to_s }
check("date")            { require "date"; Date.new(2026, 9, 29).strftime("%A") }
check("threads")         { Thread.new { 21 * 2 }.value }
check("file write")      { File.write("/data/homebrew/ruby-test/out.txt", "written by ruby\n"); File.read("/data/homebrew/ruby-test/out.txt") }
check("benchmark")       do
  t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  x = 0
  3_000_000.times { |i| x += i % 7 }
  "%.2f s for 3M block calls" % (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t)
end

puts "done"

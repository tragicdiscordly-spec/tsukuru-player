# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compatibility for games written for RPG Maker's Windows player (XP / VX / VX Ace), loaded before the game's own
# scripts (the launcher sets MKXP_PRELOAD to this file).
#
# * Zlib is part of RPG Maker's Ruby; here it has to be loaded.
# * Thread.critical (Ruby 1.8) does nothing.
# * Win32API is the class games use to call Windows DLLs (user32, kernel32, steam_api, ...). There is no Windows here,
#   so a call does nothing and answers 0, which is what most games expect when an optional feature (Steam
#   achievements, window tricks, key state) is not available. A few harmless functions are answered properly. The
#   first call of every function is written to the game's output, so it is visible what a game asked for.
begin
  require 'zlib'
rescue LoadError
end

# Thread.critical was removed from Ruby after 1.8; games use it to guard a few lines of code. With a single game thread
# there is nothing to guard against.
class Thread
  class << self
    def critical
      @critical ||= false
    end

    def critical=(value)
      @critical = value
    end
  end
end

class Win32API
  @@seen = {}

  def initialize(dll, func, import = nil, export = nil)
    @dll = dll.to_s.downcase.sub(/\.dll\z/, '')
    @func = func.to_s
    @export = export.to_s.downcase
  end

  def call(*args)
    key = "#{@dll}.#{@func}"
    unless @@seen[key]
      @@seen[key] = true
      STDERR.puts "Win32API stand-in: #{key} is not available (returns 0)"
    end
    case @func
    when /\AGetPrivateProfileInt/
      args[2].to_i # the default value
    when /\A(GetTickCount|timeGetTime)/
      (Process.clock_gettime(Process::CLOCK_MONOTONIC) * 1000).to_i & 0xffffffff
    when /\AGetCurrentProcessId|GetCurrentThreadId/
      1
    else
      0
    end
  end
end

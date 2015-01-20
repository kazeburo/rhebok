require 'stringio'
require 'tempfile'

class Rhebok
  class Buffered
    def initialize(length=0,memory_max=1048576)
      @length = length
      @memory_max = memory_max
      @size = 0
      if length > memory_max
        @buffer = Tempfile.open('r')
        @buffer.binmode
        @buffer.set_encoding('BINARY')
      else
        @buffer = StringIO.new("").set_encoding('BINARY')
      end
    end

    def print(buf)
      if @size + buf.bytesize > @memory_max && @buffer.instance_of?(StringIO)
        new_buffer = Tempfile.open('r')
        new_buffer.binmode
        new_buffer.set_encoding('BINARY')
        new_buffer << @buffer.string
        @buffer = new_buffer
      end
      @buffer << buf
      @size += buf.bytesize
    end

    def size
      @size
    end

    def rewind
      @buffer.rewind
      @buffer
    end

    def close
      if @buffer.instance_of?(Tempfile)
        @buffer.close!
      end
    end
  end
end
